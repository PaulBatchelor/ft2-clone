#ifndef __FT2_AUDIO_H
#define __FT2_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "ft2_replayer.h"

enum
{
	FREQ_TABLE_LINEAR = 0,
	FREQ_TABLE_AMIGA  = 1,
};

// for audio/video sync queue (must be 2^n-1 - don't mess with this! It's big enough as is.)
#define SYNC_QUEUE_LEN 2047

#define MAX_AUDIO_DEVICES 99

#define MIN_AUDIO_FREQ 44100
#define MAX_AUDIO_FREQ 96000
#define MAX_SAMPLES_PER_TICK (((MAX_AUDIO_FREQ * 5) / 2) / MIN_BPM)

struct audio_t
{
	char *currInputDevice, *currOutputDevice, *lastWorkingAudioDeviceName;
	char *inputDeviceNames[MAX_AUDIO_DEVICES], *outputDeviceNames[MAX_AUDIO_DEVICES];
	volatile bool locked;
	bool volumeRampingFlag, interpolationFlag, rescanAudioDevicesSupported;
	int8_t freqTable;
	int32_t inputDeviceNum, outputDeviceNum, lastWorkingAudioFreq, lastWorkingAudioBits;
	int32_t quickVolSizeVal, *mixBufferL, *mixBufferR;
	uint32_t freq, scopeFreqMul;
	uint64_t tickTime64;
	SDL_AudioDeviceID dev;
} audio;

typedef struct
{
	const int8_t *sampleData8;
	const int16_t *sampleData16;
	bool backwards, isFadeOutVoice;
	uint8_t SVol, SPan;
	int32_t SLVol1, SRVol1, SLVol2, SRVol2, SLVolIP, SRVolIP, SVolIPLen, SPos, SLen, SRepS, SRepL;
	uint32_t SPosDec, SFrq;
	void (*mixRoutine)(void *, int32_t); // function pointer to mix routine
} voice_t;

typedef struct pattSyncData_t
{
	// for pattern editor
	int16_t pattern, patternPos, globalVol, songPos;
	uint16_t timer, speed, tempo;
	uint64_t timestamp;
} pattSyncData_t;

struct pattSync
{
	int32_t readPos, writePos;
	pattSyncData_t data[SYNC_QUEUE_LEN + 1];
} pattSync;

typedef struct chSyncData_t
{
	channel_t channels[MAX_VOICES];
	uint64_t timestamp;
} chSyncData_t;

struct chSync
{
	int32_t readPos, writePos;
	chSyncData_t data[SYNC_QUEUE_LEN + 1];
} chSync;

int32_t pattQueueReadSize(void);
int32_t pattQueueWriteSize(void);
bool pattQueuePush(pattSyncData_t t);
bool pattQueuePop(void);
pattSyncData_t *pattQueuePeek(void);
uint64_t getPattQueueTimestamp(void);
int32_t chQueueReadSize(void);
int32_t chQueueWriteSize(void);
bool chQueuePush(chSyncData_t t);
bool chQueuePop(void);
chSyncData_t *chQueuePeek(void);
uint64_t getChQueueTimestamp(void);
uint32_t getVoiceRate(uint8_t i);
void setAudioAmp(int16_t ampFactor, int16_t master, bool bitDepth32Flag);
void setNewAudioFreq(uint32_t freq);
void setBackOldAudioFreq(void);
void setSpeed(uint16_t bpm);
void audioSetVolRamp(bool volRamp);
void audioSetInterpolation(bool interpolation);
void stopVoice(uint8_t i);
bool setupAudio(bool showErrorMsg);
void closeAudio(void);
void pauseAudio(void);
void resumeAudio(void);
bool setNewAudioSettings(void);
bool setupAudioBuffers(void);
void freeAudioBuffers(void);
void resetDitherSeed(void);
void lockAudio(void);
void unlockAudio(void);
void lockMixerCallback(void);
void unlockMixerCallback(void);
void updateSendAudSamplesRoutine(bool lockMixer);
void mix_SaveIPVolumes(void);
void mix_UpdateChannelVolPanFrq(void);
uint32_t mixReplayerTickToBuffer(uint8_t *stream, uint8_t bitDepth);
//void benchmarkAudioChannelMixer(void); // for development testing

pattSyncData_t *pattSyncEntry;
chSyncData_t *chSyncEntry;

#endif
