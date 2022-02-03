#ifndef _ALIGN_H
#define _ALIGN_H

typedef struct TimingEvent {
	double timestamp;
	int sample_index;
	uint8_t code;
} TimingEvent;

void run_alignment(char *, char *, char *, double *, double *);
void calculateCorr(double *x, double *y, int size, double *coeffA, double *coeffB);

void *readNPY(char *filename, int *sz);

double *readIMEC(char *filename, double samplerate, int channel, int size);
int16_t *readNIDQ(char *filename, long long size);

TimingEvent *getNeuralTimingData(char *str, int *numEvents);
TimingEvent *getBehavioralTimingData(char *str, int *numEvents);

TimingEvent *openEphysData(char *, int *);
TimingEvent *spikeGLXData(char *, int *);

#endif 