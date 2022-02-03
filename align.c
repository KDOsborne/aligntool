#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <dirent.h>

#include "align.h"
#include "sqlite3.h"

char date[100];

static int callback_count(void *data, int argc, char **argv, char **azColName)
{
	*((int *)data) = atoi(argv[0]);
	return 0;
}

static int callback_date(void *data, int argc, char **argv, char **azColName)
{
	strcpy(date, argv[0]);
	return 0;
}

static int callback_events(void *data, int argc, char **argv, char **azColName)
{
	static int currentEvent = 0;
	
	((TimingEvent *)data)[currentEvent].timestamp = atof(argv[0]);
	((TimingEvent *)data)[currentEvent].code = atoi(argv[1]);
	((TimingEvent *)data)[currentEvent].sample_index = -1;
	
	currentEvent++;
	return 0;
}

int main(int argc, char *argv[])
{
	double coeffA = 0, coeffB = 0;
	char date[1000];
	
	if(argc == 3)
	{
		run_alignment(argv[1], argv[2], date, &coeffA, &coeffB);
		printf("%f %f", coeffA, coeffB);
	}
	else if(argc == 1)
	{
		OPENFILENAME ofn;
		char structurefile[1000], sessionfile[1000];

		//Select Structure File
		memset(&ofn, 0, sizeof(ofn));
		memset(structurefile, 0, sizeof(structurefile));

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		ofn.lpstrFilter = "Structure File (*.meta;*.oebin)\0*.meta;*.oebin\0";
		ofn.nFilterIndex = 1;
		ofn.lpstrFile = structurefile;
		ofn.nMaxFile = sizeof(structurefile);
		ofn.lpstrTitle = "Select the Structure File";
		ofn.lpstrInitialDir = ".";
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		if (!GetOpenFileName(&ofn))
		{
			fprintf(stderr, "OFN ERROR: 0x%lx\nPRESS ENTER TO EXIT", CommDlgExtendedError());
			getchar();
			return -1;
		}
		
		//Select Database File
		memset(&ofn, 0, sizeof(ofn));
		memset(sessionfile, 0, sizeof(sessionfile));
		
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = NULL;
		ofn.lpstrFilter = "SQLITE File (*.sqlite)\0*.sqlite\0";
		ofn.nFilterIndex = 1;
		ofn.lpstrFile = sessionfile;
		ofn.nMaxFile = sizeof(sessionfile);
		ofn.lpstrTitle = "Select the SQLITE File";
		ofn.lpstrInitialDir = ".";
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		if (!GetOpenFileName(&ofn))
		{
			fprintf(stderr, "OFN ERROR: 0x%lx\nPRESS ENTER TO EXIT", CommDlgExtendedError());
			getchar();
			return -1;
		}
		
		run_alignment(structurefile, sessionfile, date, &coeffA, &coeffB);
		printf("%f %f", coeffA, coeffB);
	}
	else
	{
		printf("usage: align.exe structurefile sessionfile\n");
		getchar();
		return -1;
	}
	printf("PRESS ENTER TO EXIT\n");
	getchar();
	
	return 0;
}

void run_alignment(char *structurefile, char *sessionfile, char *sessiondate, double *coeffA, double *coeffB)
{
	int neuralCount = 0, behavioralCount = 0;
	
	TimingEvent *neuralEvents = getNeuralTimingData(structurefile, &neuralCount);
	if(neuralEvents == NULL)
	{
		printf("ABORTING\n");
		//getchar();
		return;
	}
	
	TimingEvent *behavioralEvents = getBehavioralTimingData(sessionfile, &behavioralCount);
	if(behavioralEvents == NULL)
	{
		printf("ABORTING\n");
		//getchar();
		return;
	}
	
	int bIndex = 0, nIndex = 0, n_ = 0;
	double sumXX_ = 0.0, sumYY_ = 0.0, sumXY_ = 0.0;
	double sumX_ = 0.0, sumY_ = 0.0;

	for(int i = 0; i < behavioralCount; i++)
	{
		if(behavioralEvents[i].code == neuralEvents[0].code)
		{
			bIndex = i;
			break;
		}
	}
	
	while (bIndex < behavioralCount && nIndex < neuralCount)
	{
		n_++;
		sumXX_ += neuralEvents[nIndex].timestamp * neuralEvents[nIndex].timestamp;
		sumYY_ += behavioralEvents[bIndex].timestamp * behavioralEvents[bIndex].timestamp;
		sumXY_ += neuralEvents[nIndex].timestamp * behavioralEvents[bIndex].timestamp;
		sumX_ += neuralEvents[nIndex].timestamp;
		sumY_ += behavioralEvents[bIndex].timestamp;

		bIndex++;
		nIndex++;
	}

	double meanX = sumX_ / n_;
	double meanY = sumY_ / n_;

	double SSxx = sumXX_ - n_*meanX*meanX;
	double SSyy = sumYY_ - n_*meanY*meanY;
	double SSxy = sumXY_ - n_*meanX*meanY;

	*coeffB = SSxy / SSxx;
	*coeffA = meanY - SSxy/SSxx*meanX;
	
	free(neuralEvents);
	free(behavioralEvents);
	
	strcpy(sessiondate, date);
}

void calculateCorr(double *x, double *y, int size, double *coeffA, double *coeffB)
{
	double sumXX_ = 0.0, sumYY_ = 0.0, sumXY_ = 0.0;
	double sumX_ = 0.0, sumY_ = 0.0;
	
	for(int i = 0; i < size; i++)
	{
		sumXX_ += x[i] * x[i];
		sumYY_ += y[i] * y[i];
		sumXY_ += x[i] * y[i];
		sumX_ += x[i];
		sumY_ += y[i];
	}

	double meanX = sumX_ / size;
	double meanY = sumY_ / size;

	double SSxx = sumXX_ - size*meanX*meanX;
	double SSyy = sumYY_ - size*meanY*meanY;
	double SSxy = sumXY_ - size*meanX*meanY;

	*coeffB = SSxy / SSxx;
	*coeffA = meanY - SSxy / SSxx * meanX;
}

void *readNPY(char *filename, int *sz)
{
	char buffer[8];
	uint16_t header = 0;
	long int datasize = 0;
	void *ptr = NULL;
	
	FILE *fp = fopen(filename, "rb");
	
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", filename);
		return ptr;
	}
	
	fread(buffer, 1, 8, fp);
	fread(&header, 2, 1, fp);
	fseek(fp, 0, SEEK_END);
	
	datasize = ftell(fp) - header - 10;
	
	fseek(fp, header + 10, SEEK_SET);
	
	ptr = malloc(datasize);
	*sz = fread(ptr, 1, datasize, fp);
	
	//printf("ALLOCATED %d/%ld BYTES FROM %s\n", *sz, datasize, filename);
	
	fclose(fp);
	
	return ptr;
}

double *readIMEC(char *filename, double samplerate, int channel, int size)
{
	int curr = 0, last = -1;
	int16_t buffer = -1;
	double *ptr = NULL;
	long long count = 0;
	
	FILE *fp = fopen(filename, "rb");
	
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", filename);
		return ptr;
	}
	
	ptr = (double *)malloc(sizeof(double) * size);
	
	_fseeki64(fp, channel * 2, SEEK_SET);
	
	while(fread(&buffer, sizeof(buffer), 1, fp) && curr < size)
	{
		if(((buffer >> 6) & 1) == 1)
		{
			if(last == 0)
			{
				ptr[curr++] = (double)count / samplerate;
				last = 1;
				fprintf(stderr, "\rFOUND TIMING SIGNAL: %d/%d", curr, size);
			}
		}
		else
			last = 0;
		
		count++;
		_fseeki64(fp, channel * 2, SEEK_CUR);
	}
	printf("\n");
	
	fclose(fp);
	
	if(curr != size)
	{
		printf("WARNING: IMEC CHANNEL NUMBER OF TIMING SIGNALS NOT EQUAL TO NIDQ\n");
		printf("IMEC RECEIVED: %d SIGNALS, NIDQ RECEIVED: %d SIGNALS\n", curr, size);
	}
	
	return ptr;
}

int16_t *readNIDQ(char *filename, long long size)
{
	int16_t *ptr = NULL;
	
	FILE *fp = fopen(filename, "rb");
	
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", filename);
		return ptr;
	}
	
	ptr = (int16_t *)malloc(sizeof(int16_t) * size);
	
	fread(ptr, sizeof(int16_t), size, fp);
	
	fclose(fp);
	
	return ptr;
}

TimingEvent *getBehavioralTimingData(char *database, int *numEvents)
{
	sqlite3					*db;
	sqlite3_stmt			*stmt;
	char					*zErrMsg = 0;
	char					q[500];
	int						rc;
	int						row = 0;

	if(!strstr(database, ".sqlite"))
	{
		printf("INVALID FILE FOR ARGUMENT 2: %s\n", database);
		return NULL;
	}
	
	//Open the database
	rc = sqlite3_open(database, &db);
	if (rc)
	{
		printf("CANNOT OPEN DATABASE: %s\n", sqlite3_errmsg(db));
		return NULL;
	}
	
	//printf("\nPROCESSING DATABASE: %s\n", database);
	sprintf(q, "SELECT value FROM sessioninfo WHERE key = 'sessionstart'");
	rc = sqlite3_exec(db, q, callback_date, NULL, &zErrMsg);
	if (rc != SQLITE_OK)
	{
		printf("SQL error: %s\n%s\n", zErrMsg, q);
		sqlite3_free(zErrMsg);
	}
	
	//Get the number of events
	sprintf(q, "SELECT COUNT(id) FROM behavioralalignevents");
	rc = sqlite3_exec(db, q, callback_count, numEvents, &zErrMsg);
	if (rc != SQLITE_OK)
	{
		printf("SQL error: %s\n%s\n", zErrMsg, q);
		sqlite3_free(zErrMsg);
	}
	
	TimingEvent *events = (TimingEvent *)malloc(sizeof(TimingEvent) * (*numEvents));
	memset(events, 0, sizeof(TimingEvent) * (*numEvents));
	
	//Get the timestamps and align codes
	sprintf(q, "SELECT timestamp, aligncode FROM behavioralalignevents");
	rc = sqlite3_exec(db, q, callback_events, events, &zErrMsg);
	if (rc != SQLITE_OK)
	{
		printf("SQL ERROR: %s\n%s\n", zErrMsg, q);
		sqlite3_free(zErrMsg);
	}
	
	sqlite3_close(db);
	
	return events;
}

TimingEvent *getNeuralTimingData(char *str, int *numEvents)
{	
	if(strstr(str, ".oebin"))
		return openEphysData(str, numEvents);
	else if(strstr(str, ".meta"))
		return spikeGLXData(str, numEvents);
	else
		printf("INVALID FILE FOR ARGUMENT 1: %s\n", str);
		
	return NULL;
}

TimingEvent *openEphysData(char *OEBINfile, int *numEvents)
{
	FILE *fp;
	char DAQfile[1000], PXIfile[1000], buffer[1000];
	int DAQrate = -1, PXIrate = -1;
	double coeffA = -1.0, coeffB = -1.0;
	
	fp = fopen(OEBINfile, "rb");
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", OEBINfile);
		fclose(fp);
		return NULL;
	}
		
	while(fgets(buffer, sizeof(buffer), fp) != NULL)
	{
		if(strstr(buffer, "\"events\": ["))
		{
			while(fgets(buffer, sizeof(buffer), fp) != NULL && !strstr(buffer, "\"spikes\""))
			{
				if(strstr(buffer, "folder_name") && strstr(buffer, "TTL_1"))
				{
					if(strstr(buffer, "Neuropix-PXI"))
					{
						sscanf(buffer, "%*s %s", PXIfile);
						while(fgets(buffer, sizeof(buffer), fp) != NULL)
						{
							if(strstr(buffer, "\"sample_rate\":"))
							{
								sscanf(buffer, "%*s %d", &PXIrate);
								break;
							}
						}
						if(PXIrate == -1)
						{
							printf("UNABLE TO READ PXI SAMPLE RATE\n");
							return NULL;
						}
					}
					else if(strstr(buffer, "NI-DAQmx"))
					{
						sscanf(buffer, "%*s %s", DAQfile);
						while(fgets(buffer, sizeof(buffer), fp) != NULL)
						{
							if(strstr(buffer, "\"sample_rate\":"))
							{
								sscanf(buffer, "%*s %d", &DAQrate);
								break;
							}
						}
						if(DAQrate == -1)
						{
							printf("UNABLE TO READ DAQ SAMPLE RATE\n");
							return NULL;
						}
					}
				}
			}
			break;
		}
	}

	PXIfile[0] = '\\';
	DAQfile[0] = '\\';
	for(int i = 0; i < strlen(PXIfile) + 1; i++)
		if(PXIfile[i] == '/')
			PXIfile[i] = '\\';
		
	for(int i = 0; i < strlen(DAQfile) + 1; i++)
		if(DAQfile[i] == '/')
			DAQfile[i] = '\\';
	
	strrchr(PXIfile, '"')[0] = '\0';
	strrchr(DAQfile, '"')[0] = '\0';
	
	char temp[1000];
	
	strrchr(OEBINfile, '\\')[0] = '\0';
	
	strcpy(temp, OEBINfile);
	strcat(temp, "\\events");
	strcat(temp, PXIfile);
	strcpy(PXIfile, temp);
	
	strcpy(temp, OEBINfile);
	strcat(temp, "\\events");
	strcat(temp, DAQfile);
	strcpy(DAQfile, temp);
	
	//Retrieve PXI timing signal
	int pxi_num_samples = 0, pxi_num_states = 0, pxi_num_timepoints = 0;
	
	strcat(PXIfile, "channel_states.npy");
	
	int16_t *pxi_states = (int16_t *)readNPY(PXIfile, &pxi_num_states);
	pxi_num_states /= sizeof(int16_t);
	
	if(pxi_states == NULL)
		return NULL;
	
	//Get the number of rising events
	for(int i = 0; i < pxi_num_states; i++)
	{
		if(pxi_states[i] == 1)
			pxi_num_timepoints++;
	}
	
	strrchr(PXIfile, '\\')[0] = '\0';
	strcat(PXIfile, "\\timestamps.npy");
	
	long long *pxi_samples = (long long *)readNPY(PXIfile, &pxi_num_samples);
	pxi_num_samples /= sizeof(long long);
	
	if(pxi_samples == NULL)
		return NULL;
	
	double *pxi_timestamps = (double *)malloc(sizeof(long long) * pxi_num_timepoints);
	
	//If these aren't equal, the numpy files weren't generated correctly
	if(pxi_num_states != pxi_num_samples)
	{
		printf("PXI ERROR: INCONSISTENT FILE LENGTH\n");
		return NULL;
	}

	//Convert rising samples to timestamps
	int curr_sample = 0;
	for(int i = 0; i < pxi_num_states; i++)
	{
		if(pxi_states[i] == 1 && curr_sample < pxi_num_timepoints)
			pxi_timestamps[curr_sample++] = (double)pxi_samples[i] / PXIrate;
	}
	
	//Retrieve DAQ timing signal
	int daq_num_samples = 0, daq_num_states = 0, daq_num_timepoints = 0;
	strcat(DAQfile, "channel_states.npy");
	
	int16_t *daq_states = (int16_t *)readNPY(DAQfile, &daq_num_states);
	daq_num_states /= sizeof(int16_t);
	
	if(daq_states == NULL)
		return NULL;
	for(int i = 0; i < daq_num_states; i++)	
	{
		if(daq_states[i] == 1)
		{
			daq_num_timepoints++;
		}
	}
	
	strrchr(DAQfile, '\\')[0] = '\0';
	strcat(DAQfile, "\\timestamps.npy");
	
	long long *daq_samples = (long long *)readNPY(DAQfile, &daq_num_samples);
	daq_num_samples /= sizeof(long long);
	
	if(daq_samples == NULL)
		return NULL;
	
	double *daq_timestamps = (double *)malloc(sizeof(double) * daq_num_timepoints);
	
	//If these aren't equal, the numpy files weren't generated correctly
	if(daq_num_states != daq_num_samples)
	{
		printf("DAQ ERROR: INCONSISTENT FILE LENGTH\n");
		return NULL;
	}

	//Convert rising samples to timestamps
	curr_sample = 0;
	for(int i = 0; i < daq_num_states; i++)
	{
		if(daq_states[i] == 1 && curr_sample < daq_num_timepoints)
			daq_timestamps[curr_sample++] = (double)daq_samples[i] / DAQrate;
	}
	
	if(pxi_num_timepoints != daq_num_timepoints)
		printf("TIMING SIGNAL WARNING: %s EVENT FILE MISSING %d PULSE(S)\n", (pxi_num_timepoints > daq_num_timepoints) ? "DAQ" : "PXI", abs(pxi_num_timepoints - daq_num_timepoints));
	
	calculateCorr(daq_timestamps, pxi_timestamps, (pxi_num_timepoints > daq_num_timepoints) ? daq_num_timepoints : pxi_num_timepoints, &coeffA, &coeffB);
	
	strrchr(DAQfile, '\\')[0] = '\0';
	strcat(DAQfile, "\\full_words.npy");
	
	int daq_num_words = 0, daq_num_events = 0, last = -1;
	uint8_t *daq_words = (uint8_t *)readNPY(DAQfile, &daq_num_words);
	
	if(daq_words == NULL)
		return NULL;
	
	for(int i = 0; i < daq_num_words; i++)	
	{
		if(daq_words[i] > 1)
		{
			if(daq_words[i] >> 1 != last)
			{
				daq_num_events++;
				last = daq_words[i] >> 1;
			}
		}
	}

	*numEvents = daq_num_events;
	
	TimingEvent *events = (TimingEvent *)malloc(sizeof(TimingEvent) * daq_num_events);
	memset(events, 0, sizeof(TimingEvent) * daq_num_events);
	
	int curr = 0;
	
	for(int i = 0; i < daq_num_words; i++)
	{
		if(daq_words[i] > 1)
		{
			if(daq_words[i] >> 1 != last)
			{
				events[curr].code = daq_words[i] >> 1;
				events[curr++].sample_index = i;
				last = daq_words[i] >> 1;
			}
		}
	}  
	
	for(int i = 0; i < daq_num_events; i++)
	{
		events[i].timestamp = coeffA + coeffB * ((double)(daq_samples[events[i].sample_index]) / DAQrate);
	}
	
	free(pxi_timestamps);
	free(pxi_samples);
	free(pxi_states);
	free(daq_timestamps);
	free(daq_samples);
	free(daq_states);
	free(daq_words); 
	
	return events;
}

TimingEvent *spikeGLXData(char *NIDQdir, int *numEvents)
{
	FILE *fp = NULL;
	long long NIDQbytes = 0, IMECbytes = 0;
	int IMECap = 0, IMEClf = 0, IMECsy = 0, IMECchannel = 0;
	double NIDQsrate = 0.0, IMECsrate  = 0.0, coeffA = -1, coeffB = -1;
	char buffer[1000], NIDQbin[1000], NIDQmeta[1000], IMECdir[1000], IMECbin[1000], IMECmeta[1000];
	
	
	strrchr(NIDQdir, '\\')[1] = '\0';
	
	strcpy(NIDQbin, NIDQdir);
	strcpy(NIDQmeta, NIDQdir);
	strcpy(IMECdir, NIDQdir);
	
	DIR *d;
    struct dirent *dir;

    d = opendir(NIDQdir);
	
	if(d)
	{
		while ((dir = readdir(d)) != NULL)
        {
			if(strstr(dir->d_name, ".nidq.bin"))
				strcat(NIDQbin, dir->d_name);
			else if(strstr(dir->d_name, ".nidq.meta"))
				strcat(NIDQmeta, dir->d_name);
			else if(strstr(dir->d_name, "imec0"))
			{
				strcat(IMECdir, dir->d_name);
				strcat(IMECdir, "\\");
				
				DIR *d2 = opendir(IMECdir);
				
				if(d2)
				{
					strcpy(IMECbin, IMECdir);
					strcpy(IMECmeta, IMECdir);
					
					while((dir = readdir(d2)) != NULL)
					{
						if(strstr(dir->d_name, ".ap.bin"))
							strcat(IMECbin, dir->d_name);
						else if(strstr(dir->d_name, ".ap.meta"))
							strcat(IMECmeta, dir->d_name);
					}
					closedir(d2);
				}
				else
				{
					printf("COULD NOT OPEN DIRECTORY: %s\n", IMECdir);
					return NULL;
				}
			}
		}
		closedir(d);
	}
	else
	{
		printf("COULD NOT OPEN DIRECTORY: %s\n", NIDQdir);
		return NULL;
	}
	
	fp = fopen(NIDQmeta, "rb");
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", NIDQmeta);
		return NULL;
	}
	
	memset(buffer, 0, sizeof(buffer));
	
	while(fgets(buffer, sizeof(buffer), fp) != NULL)
	{
		if(strstr(buffer, "fileSizeBytes="))
			sscanf(buffer, "fileSizeBytes=%I64d", &NIDQbytes);
		else if(strstr(buffer, "niSampRate="))
			sscanf(buffer, "niSampRate=%lf", &NIDQsrate);
	}
	
	fclose(fp);
	
	fp = fopen(IMECmeta, "rb");
	if(!fp)
	{
		printf("COULD NOT OPEN FILE: %s\n", IMECmeta);
		return NULL;
	}
	
	memset(buffer, 0, sizeof(buffer));
	
	while(fgets(buffer, sizeof(buffer), fp) != NULL)
	{
		if(strstr(buffer, "fileSizeBytes="))
			sscanf(buffer, "fileSizeBytes=%I64d", &IMECbytes);
		else if(strstr(buffer, "imSampRate="))
			sscanf(buffer, "imSampRate=%lf", &IMECsrate);
		else if(strstr(buffer, "snsApLfSy="))
			sscanf(buffer, "snsApLfSy=%d,%d,%d", &IMECap, &IMEClf, &IMECsy);
	}
	
	IMECchannel = IMECap + IMEClf;
	
	fclose(fp);
	
	//Get the raw NIDQ samples
	int16_t *daq_events = readNIDQ(NIDQbin, NIDQbytes);
	int daq_num_events = 0, last = 0;
	
	for(int i = 0; i < NIDQbytes / 2; i++)
	{
		if((daq_events[i] & 1) == 1)
		{
			if(last == 0)
			{
				daq_num_events++;
				last = 1;
			}
		}
		else
			last = 0;
	}
	*numEvents = daq_num_events;
	
	//Calculate the timestamps in seconds
	double *pxi_timestamps = readIMEC(IMECbin, IMECsrate, IMECchannel, daq_num_events);
	if(pxi_timestamps == NULL)
	{
		free(daq_events);
		return NULL;
	}
	
	double *daq_timestamps = (double *)malloc(sizeof(double) * daq_num_events);
	if(daq_timestamps == NULL)
	{
		free(daq_events);
		free(pxi_timestamps);
		return NULL;
	}
	
	TimingEvent *events = (TimingEvent *)malloc(sizeof(TimingEvent) * daq_num_events);
	
	int curr_timestamp = 0;
	last = 0;
	for(int i = 0; i < NIDQbytes / 2; i++)
	{
		if((daq_events[i] & 1) == 1)
		{
			if(last == 0)
			{
				daq_timestamps[curr_timestamp] = i / NIDQsrate;
				events[curr_timestamp].sample_index = i;
				events[curr_timestamp].code = (uint8_t)daq_events[i] >> 1;
				curr_timestamp++;
				last = 1;
			}
		}
		else
			last = 0;
	}
	
	//Align the timestamps
	calculateCorr(daq_timestamps, pxi_timestamps, daq_num_events, &coeffA, &coeffB);
	
	for(int i = 0; i < daq_num_events; i++)
	{
		events[i].timestamp = coeffA + coeffB * daq_timestamps[i];
	}
	
	free(daq_events);
	free(pxi_timestamps);
	free(daq_timestamps);
	
	return events;
}
