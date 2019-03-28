// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include "ft2_header.h"
#include "ft2_config.h"
#include "ft2_scopes.h"
#include "ft2_video.h"
#include "ft2_gui.h"
#include "ft2_midi.h"
#include "ft2_wav_renderer.h"
#include "ft2_module_loader.h"
#include "ft2_mix.h"
#include "ft2_audio.h"

#define INITIAL_DITHER_SEED 0x12345000

static int8_t pmpCountDiv, pmpChannels = 2;
static uint16_t smpBuffSize;
static int32_t masterVol, amp, oldAudioFreq, speedVal, pmpLeft, randSeed = INITIAL_DITHER_SEED;
static uint32_t tickTimeLen;
static float fAudioAmpMul;
static voice_t voice[MAX_VOICES * 2];
static void (*sendAudSamplesFunc)(uint8_t *, int32_t, uint8_t); // "send mixed samples" routines

extern const uint32_t panningTab[257]; // defined at the bottom of this file

uint32_t getVoiceRate(uint8_t i)
{
	assert(i < MAX_VOICES);
	return (voice[i].SFrq);
}

void stopVoice(uint8_t i)
{
	voice_t *v;

	v = &voice[i];
	memset(v, 0, sizeof (voice_t));
	v->SPan = 128;

	// clear "fade out" voice too

	v = &voice[MAX_VOICES + i];
	memset(v, 0, sizeof (voice_t));
	v->SPan = 128;
}

bool setNewAudioSettings(void) // only call this from the main input/video thread
{
	uint32_t stringLen;

	pauseAudio();

	if (!setupAudio(CONFIG_HIDE_ERRORS))
	{
		// set back old known working settings

		config.audioFreq = audio.lastWorkingAudioFreq;
		config.specialFlags &= ~(BITDEPTH_16 + BITDEPTH_24 + BUFFSIZE_512 + BUFFSIZE_1024 + BUFFSIZE_2048 + BUFFSIZE_4096);
		config.specialFlags |= audio.lastWorkingAudioBits;

		if (audio.lastWorkingAudioDeviceName != NULL)
		{
			if (audio.currOutputDevice != NULL)
			{
				free(audio.currOutputDevice);
				audio.currOutputDevice = NULL;
			}

			stringLen = (uint32_t)(strlen(audio.lastWorkingAudioDeviceName));

			audio.currOutputDevice = (char *)(malloc(stringLen + 2));
			if (audio.currOutputDevice != NULL)
			{
				strcpy(audio.currOutputDevice, audio.lastWorkingAudioDeviceName);
				audio.currOutputDevice[stringLen + 1] = '\0'; // UTF-8 needs double null termination
			}
		}

		// also update config audio radio buttons if we're on that screen at the moment
		if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_IO_DEVICES))
			setConfigIORadioButtonStates();

		// if it didn't work to use the old settings again, then something is seriously wrong...
		if (!setupAudio(CONFIG_HIDE_ERRORS))
			okBox(0, "System message", "Couldn't find a working audio mode... You'll get no sound / replayer timer!");

		resumeAudio();
		return (false);
	}

	resumeAudio();
	return (true);
}

// ampFactor = 1..32, masterVol = 0..256
void setAudioAmp(int16_t ampFactor, int16_t master, bool bitDepth32Flag)
{
	int32_t newAmp, i;

	// voiceVolume = (vol(0..255) * pan(0..65536) * amp(0..256)) >> 4
	const float fAudioNorm = 1.0f / (float)(((255UL * 65536 * 256) / 16) / MAX_VOICES);

	assert((ampFactor >= 0) && (ampFactor <= 32) && (master >= 0) && (master <= 256));

	if (bitDepth32Flag)
	{
		// 32-bit floating point (24-bit)
		fAudioAmpMul = fAudioNorm * (master / 256.0f);
	}
	else
	{
		// 16-bit integer
		masterVol = master;
	}

	// calculate channel amp

	newAmp = ampFactor * (256 / 32);
	if (amp != newAmp)
	{
		amp = newAmp;

		// make all channels update volume because of amp change
		for (i = 0; i < song.antChn; ++i)
			stm[i].status |= IS_Vol;
	}
}

void setNewAudioFreq(uint32_t freq) // for song to WAV rendering
{
	if (freq > 0)
	{
		oldAudioFreq = audio.freq;
		audio.freq   = freq;

		calcReplayRate(audio.freq);
	}
}

void setBackOldAudioFreq(void) // for song to WAV rendering
{
	audio.freq = oldAudioFreq;
	calcReplayRate(audio.freq);
}

void setSpeed(uint16_t bpm)
{
	double dTickTimeLen;

	if (bpm > 0)
	{
		speedVal = ((audio.freq + audio.freq) + (audio.freq >> 1)) / bpm;

		// calculate tick time length for audio/video sync timestamp
		if (speedVal > 0)
		{
			dTickTimeLen = ((double)(speedVal) / audio.freq) * editor.dPerfFreq;

			if (cpu.hasSSE2)
				sse2_double2int32_round(tickTimeLen, dTickTimeLen);
			else
				tickTimeLen = (uint32_t)(round(dTickTimeLen));
		}
	}
}

void audioSetVolRamp(bool volRamp)
{
	lockMixerCallback();
	audio.volumeRampingFlag = volRamp;
	unlockMixerCallback();
}

void audioSetInterpolation(bool interpolation)
{
	lockMixerCallback();
	audio.interpolationFlag = interpolation;
	unlockMixerCallback();
}

static inline void voiceUpdateVolumes(uint8_t i, uint8_t status)
{
	int32_t volL, volR;
	voice_t *v, *f;

	v = &voice[i];

	volL = v->SVol * amp;

	// 0..267386880
	volR = (volL * panningTab[      v->SPan]) >> (32 - 28);
	volL = (volL * panningTab[256 - v->SPan]) >> (32 - 28);

	if (!audio.volumeRampingFlag)
	{
		v->SLVol2 = volL;
		v->SRVol2 = volR;
	}
	else
	{
		v->SLVol1 = volL;
		v->SRVol1 = volR;

		if (status & IS_NyTon)
		{
			// sample is about to start, ramp out/in at the same time

			// setup "fade out" voice (only if current voice volume>0)
			if ((v->SLVol2 > 0) || (v->SRVol2 > 0))
			{
				f = &voice[MAX_VOICES + i];
				memcpy(f, v, sizeof (voice_t));

				f->SVolIPLen = audio.quickVolSizeVal;
				f->SLVolIP   = -f->SLVol2 / f->SVolIPLen;
				f->SRVolIP   = -f->SRVol2 / f->SVolIPLen;

				f->isFadeOutVoice = true;
			}

			// make current voice fade in when it starts
			v->SLVol2 = 0;
			v->SRVol2 = 0;
		}

		// ramp volume changes

		/*
		** FT2 has two internal volume ramping lengths:
		** IS_QuickVol: 5ms (audioFreq / 200)
		** Normal: The duration of a tick (speedVal)
		*/

		if ((volL == v->SLVol2) && (volR == v->SRVol2))
		{
			v->SVolIPLen = 0; // there is no volume change
		}
		else
		{
			v->SVolIPLen = (status & IS_QuickVol) ? audio.quickVolSizeVal : speedVal;
			v->SLVolIP   = (volL - v->SLVol2) / v->SVolIPLen;
			v->SRVolIP   = (volR - v->SRVol2) / v->SVolIPLen;
		}
	}
}

static void voiceTrigger(uint8_t i, const int8_t *sampleData,
	int32_t sampleLength,  int32_t sampleLoopBegin, int32_t sampleLoopLength,
	uint8_t loopType, bool sampleIs16Bit, int32_t position)
{
	voice_t *v;

	v = &voice[i];

	if ((sampleData == NULL) || (sampleLength < 1))
	{
		v->mixRoutine = NULL; // shut down voice (illegal parameters)
		return;
	}

	if (sampleIs16Bit)
	{
		assert(!(sampleLoopBegin  & 1));
		assert(!(sampleLength     & 1));
		assert(!(sampleLoopLength & 1));

		sampleLoopBegin  >>= 1;
		sampleLength     >>= 1;
		sampleLoopLength >>= 1;

		v->sampleData16 = (const int16_t *)(sampleData);
	}
	else
	{
		v->sampleData8 = sampleData;
	}

	if (sampleLoopLength < 1)
		loopType = 0;

	v->backwards = false;
	v->SLen      = (loopType > 0) ? (sampleLoopBegin + sampleLoopLength) : sampleLength;
	v->SRepS     = sampleLoopBegin;
	v->SRepL     = sampleLoopLength;
	v->SPos      = position;
	v->SPosDec   = 0; // position fraction

	// if 9xx position overflows, shut down voice (confirmed FT2 behavior)
	if (v->SPos >= v->SLen)
	{
		v->mixRoutine = NULL;
		return;
	}

	v->mixRoutine = mixRoutineTable[(sampleIs16Bit * 12) + (audio.volumeRampingFlag * 6) + (audio.interpolationFlag * 3) + loopType];
}

void mix_SaveIPVolumes(void) // for volume ramping
{
	int32_t i;
	voice_t *v;

	for (i = 0; i < song.antChn; ++i)
	{
		v = &voice[i];

		v->SLVol2 = v->SLVol1;
		v->SRVol2 = v->SRVol1;

		v->SVolIPLen = 0;
	}
}

void mix_UpdateChannelVolPanFrq(void)
{
	uint8_t i, status;
	uint16_t vol;
	int32_t playOffset;
	stmTyp *ch;
	voice_t *v;
	sampleTyp *s;

	for (i = 0; i < song.antChn; ++i)
	{
		ch = &stm[i];
		v = &voice[i];

		status = ch->tmpStatus = ch->status; // ch->tmpStatus is used for audio/video sync queue
		if (status != 0)
		{
			ch->status = 0;

			// volume change
			if (status & IS_Vol)
			{
				vol = ch->finalVol;
				if (vol > 0) // yes, FT2 does this! now it's 0..255 instead
					vol--;

				v->SVol = (uint8_t)(vol);
			}

			// panning change
			if (status & IS_Pan)
				v->SPan = ch->finalPan;

			// update mixing volumes if vol/pan change
			if (status & (IS_Vol | IS_Pan))
				voiceUpdateVolumes(i, status);

			// frequency change
			if (status & IS_Period)
				v->SFrq = getFrequenceValue(ch->finalPeriod);

			// sample trigger (note)
			if (status & IS_NyTon)
			{
				s = ch->smpPtr;

				playOffset = ch->smpStartPos;

				// if we triggered from sample editor ("Wave, "Range", "Display"), add range/display offset
				if (ch->instrNr == (MAX_INST + 1))
					playOffset += editor.samplePlayOffset;

				voiceTrigger(i, s->pek, s->len, s->repS, s->repL, s->typ & 3, (s->typ & 16) >> 4, playOffset);
			}
		}
	}
}

void resetDitherSeed(void)
{
	randSeed = INITIAL_DITHER_SEED;
}

// Delphi/Pascal LCG Random() (without limit). Suitable for 32-bit random numbers
static inline uint32_t random32(void)
{
	randSeed = randSeed * 134775813 + 1;
	return (randSeed);
}

static void sendSamples16BitStereo(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int16_t *streamPointer16;
	int32_t i, out32;

	streamPointer16 = (int16_t *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		out32 = ((audio.mixBufferL[i] >> 8) * masterVol) >> 8;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		// right channel
		out32 = ((audio.mixBufferR[i] >> 8) * masterVol) >> 8;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);
	}

	(void)(numAudioChannels); // make compiler happy
}

static void sendSamples16BitMultiChan(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int16_t *streamPointer16;
	int32_t i, j, out32;

	streamPointer16 = (int16_t *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		out32 = ((audio.mixBufferL[i] >> 8) * masterVol) >> 8;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		// right channel
		out32 = ((audio.mixBufferR[i] >> 8) * masterVol) >> 8;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		for (j = 2; j < numAudioChannels; ++j)
			*streamPointer16++ = 0;
	}
}

static void sendSamples16BitDitherStereo(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int16_t *streamPointer16;
	int32_t i, dither, out32;

	streamPointer16 = (int16_t *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		dither = (random32() % 3) - 1; // random 1.5-bit noise: -1, 0, 1
		out32  = ((audio.mixBufferL[i] >> 8) * masterVol) >> 8;
		out32 += dither;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		// right channel
		dither = (random32() % 3) - 1;
		out32  = ((audio.mixBufferR[i] >> 8) * masterVol) >> 8;
		out32 += dither;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);
	}

	(void)(numAudioChannels); // make compiler happy
}

static void sendSamples16BitDitherMultiChan(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int16_t *streamPointer16;
	int32_t i, j, dither, out32;

	streamPointer16 = (int16_t *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		dither = (random32() % 3) - 1; // random 1.5-bit noise: -1, 0, 1
		out32  = ((audio.mixBufferL[i] >> 8) * masterVol) >> 8;
		out32 += dither;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		// right channel
		dither = (random32() % 3) - 1;
		out32  = ((audio.mixBufferR[i] >> 8) * masterVol) >> 8;
		out32 += dither;
		CLAMP16(out32);
		*streamPointer16++ = (int16_t)(out32);

		for (j = 2; j < numAudioChannels; ++j)
			*streamPointer16++ = 0;
	}
}

static void sendSamples24BitStereo(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int32_t i;
	float fOut, *fStreamPointer24;

	fStreamPointer24 = (float *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		fOut = audio.mixBufferL[i] * fAudioAmpMul;
		fOut = CLAMP(fOut, -1.0f, 1.0f);
		*fStreamPointer24++ = fOut;

		// right channel
		fOut = audio.mixBufferR[i] * fAudioAmpMul;
		fOut = CLAMP(fOut, -1.0f, 1.0f);
		*fStreamPointer24++ = fOut;
	}

	(void)(numAudioChannels); // make compiler happy
}

static void sendSamples24BitMultiChan(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int32_t i, j;
	float fOut, *fStreamPointer24;

	fStreamPointer24 = (float *)(stream);
	for (i = 0; i < sampleBlockLength; ++i)
	{
		// left channel
		fOut = audio.mixBufferL[i] * fAudioAmpMul;
		fOut = CLAMP(fOut, -1.0f, 1.0f);
		*fStreamPointer24++ = fOut;

		// right channel
		fOut = audio.mixBufferR[i] * fAudioAmpMul;
		fOut = CLAMP(fOut, -1.0f, 1.0f);
		*fStreamPointer24++ = fOut;

		for (j = 2; j < numAudioChannels; ++j)
			*fStreamPointer24++ = 0.0f;
	}
}

static void mixAudio(uint8_t *stream, int32_t sampleBlockLength, uint8_t numAudioChannels)
{
	int32_t i;
	voice_t *v;

	assert(sampleBlockLength <= MAX_SAMPLES_PER_TICK);

	memset(audio.mixBufferL, 0, sampleBlockLength * sizeof (int32_t));
	memset(audio.mixBufferR, 0, sampleBlockLength * sizeof (int32_t));

	// mix channels
	for (i = 0; i < song.antChn; ++i)
	{
		// mix normal voice
		v = &voice[i];

		// call the mixing routine currently set for the voice
		if (v->mixRoutine != NULL)
		   (v->mixRoutine)((void *)(v), sampleBlockLength);

		// mix fade-out voice
		v = &voice[MAX_VOICES + i];

		// call the mixing routine currently set for the voice
		if (v->mixRoutine != NULL)
		   (v->mixRoutine)((void *)(v), sampleBlockLength);
	}

	// normalize mix buffer and send to audio stream
	(sendAudSamplesFunc)(stream, sampleBlockLength, numAudioChannels);
}

// used for song-to-WAV renderer
uint32_t mixReplayerTickToBuffer(uint8_t *stream, uint8_t bitDepth)
{
	int32_t i;
	voice_t *v;

	memset(audio.mixBufferL, 0, speedVal * sizeof (int32_t));
	memset(audio.mixBufferR, 0, speedVal * sizeof (int32_t));

	// mix channels
	for (i = 0; i < song.antChn; ++i)
	{
		// mix normal voice
		v = &voice[i];

		// call the mixing routine currently set for the voice
		if (v->mixRoutine != NULL)
		   (v->mixRoutine)((void *)(v), speedVal);

		// mix fade-out voice
		v = &voice[MAX_VOICES + i];

		// call the mixing routine currently set for the voice
		if (v->mixRoutine != NULL)
		   (v->mixRoutine)((void *)(v), speedVal);
	}

	// normalize mix buffer and send to audio stream
	if (bitDepth == 16)
	{
		if (config.audioDither)
			sendSamples16BitDitherStereo(stream, speedVal, 2);
		else
			sendSamples16BitStereo(stream, speedVal, 2);
	}
	else
	{
		sendSamples24BitStereo(stream, speedVal, 2);
	}

	return (speedVal);
}

int32_t pattQueueReadSize(void)
{
	if (pattSync.writePos > pattSync.readPos)
		return (pattSync.writePos - pattSync.readPos);
	else if (pattSync.writePos  < pattSync.readPos)
		return (pattSync.writePos - pattSync.readPos + SYNC_QUEUE_LEN + 1);
	else
		return (0);
}

int32_t pattQueueWriteSize(void)
{
	if (pattSync.writePos > pattSync.readPos)
		return (pattSync.readPos - pattSync.writePos + SYNC_QUEUE_LEN);
	else if (pattSync.writePos < pattSync.readPos)
		return (pattSync.readPos - pattSync.writePos - 1);
	else
		return (SYNC_QUEUE_LEN);
}

bool pattQueuePush(pattSyncData_t t)
{
	if (!pattQueueWriteSize())
		return (false);

	assert(pattSync.writePos <= SYNC_QUEUE_LEN);

	pattSync.data[pattSync.writePos] = t;
	pattSync.writePos = (pattSync.writePos + 1) & SYNC_QUEUE_LEN;

	return (true);
}

bool pattQueuePop(void)
{
	if (!pattQueueReadSize())
		return (false);

	pattSync.readPos = (pattSync.readPos + 1) & SYNC_QUEUE_LEN;
	return (true);
}

pattSyncData_t *pattQueuePeek(void)
{
	if (!pattQueueReadSize())
		return (NULL);

	assert(pattSync.readPos <= SYNC_QUEUE_LEN);
	return (&pattSync.data[pattSync.readPos]);
}

uint64_t getPattQueueTimestamp(void)
{
	if (!pattQueueReadSize())
		return (0);

	assert(pattSync.readPos <= SYNC_QUEUE_LEN);
	return (pattSync.data[pattSync.readPos].timestamp);
}

int32_t chQueueReadSize(void)
{
	if (chSync.writePos > chSync.readPos)
		return (chSync.writePos - chSync.readPos);
	else if (chSync.writePos < chSync.readPos)
		return (chSync.writePos - chSync.readPos + SYNC_QUEUE_LEN + 1);
	else
		return (0);
}

int32_t chQueueWriteSize(void)
{
	if (chSync.writePos > chSync.readPos)
		return (chSync.readPos - chSync.writePos + SYNC_QUEUE_LEN);
	else if (chSync.writePos < chSync.readPos)
		return (chSync.readPos - chSync.writePos - 1);
	else
		return (SYNC_QUEUE_LEN);
}

bool chQueuePush(chSyncData_t t)
{
	if (!chQueueWriteSize())
		return (false);

	assert(chSync.writePos <= SYNC_QUEUE_LEN);

	chSync.data[chSync.writePos] = t;
	chSync.writePos = (chSync.writePos + 1) & SYNC_QUEUE_LEN;

	return (true);
}

bool chQueuePop(void)
{
	if (!chQueueReadSize())
		return (false);

	assert(chSync.readPos <= SYNC_QUEUE_LEN);

	chSync.readPos = (chSync.readPos + 1) & SYNC_QUEUE_LEN;
	return (true);
}

chSyncData_t *chQueuePeek(void)
{
	if (!chQueueReadSize())
		return (NULL);

	assert(chSync.readPos <= SYNC_QUEUE_LEN);
	return (&chSync.data[chSync.readPos]);
}

uint64_t getChQueueTimestamp(void)
{
	if (!chQueueReadSize())
		return (0);

	assert(chSync.readPos <= SYNC_QUEUE_LEN);
	return (chSync.data[chSync.readPos].timestamp);
}

void lockAudio(void)
{
	if (audio.dev != 0)
		SDL_LockAudioDevice(audio.dev);

	audio.locked = true;
}

void unlockAudio(void)
{
	if (audio.dev != 0)
		SDL_UnlockAudioDevice(audio.dev);

	audio.locked = false;
}

static void resetSyncQueues(void)
{
	pattSync.writePos = 0;
	pattSync.readPos  = 0;
	memset(&pattSync.data[pattSync.readPos], 0, sizeof (pattSyncData_t));

	chSync.writePos = 0;
	chSync.readPos  = 0;
	memset(&chSync.data[chSync.readPos], 0, sizeof (chSyncData_t));
}

void lockMixerCallback(void) // lock audio + clear voices/scopes (for short operations)
{
	if (!audio.locked)
		lockAudio();

	stopVoices(); // VERY important! prevents potential crashes
	// scopes, mixer and replayer are guaranteed to not be active at this point

	resetSyncQueues();
}

void unlockMixerCallback(void)
{
	stopVoices(); // VERY important! prevents potential crashes

	if (audio.locked)
		unlockAudio();
}

void pauseAudio(void) // lock audio + clear voices/scopes + render silence (for long operations)
{
	if (!audioPaused)
	{
		if (audio.dev > 0)
			SDL_PauseAudioDevice(audio.dev, true);

		audioPaused = true;

		stopVoices(); // VERY important! prevents potential crashes
		// scopes, mixer and replayer are guaranteed to not be active at this point

		resetSyncQueues();
	}
}

void resumeAudio(void) // unlock audio
{
	if (audioPaused)
	{
		if (audio.dev > 0)
			SDL_PauseAudioDevice(audio.dev, false);

		audioPaused = false;
	}
}

static void SDLCALL mixCallback(void *userdata, Uint8 *stream, int len)
{
	int32_t a, b, i;
	pattSyncData_t pattSyncData;
	chSyncData_t chSyncData;
	channel_t *c;
	stmTyp *s;

	assert(pmpCountDiv > 0);
	a = len / pmpCountDiv;
	if (a <= 0)
		return;

	audio.tickTime64 = SDL_GetPerformanceCounter();

	while (a > 0)
	{
		if (pmpLeft == 0)
		{
			// replayer tick

			replayerBusy = true;

			if (audio.volumeRampingFlag)
				mix_SaveIPVolumes();

			mainPlayer();
			mix_UpdateChannelVolPanFrq();

			// AUDIO/VIDEO SYNC

			if (songPlaying)
			{
				// push pattern variables to sync queue
				pattSyncData.timer      = song.curReplayerTimer;
				pattSyncData.patternPos = song.curReplayerPattPos;
				pattSyncData.pattern    = song.curReplayerPattNr;
				pattSyncData.songPos    = song.curReplayerSongPos;
				pattSyncData.speed      = song.speed;
				pattSyncData.tempo      = song.tempo;
				pattSyncData.globalVol  = song.globVol;
				pattSyncData.timestamp  = audio.tickTime64;
				pattQueuePush(pattSyncData);
			}

			// push channel variables to sync queue
			for (i = 0; i < song.antChn; ++i)
			{
				c = &chSyncData.channels[i];
				s = &stm[i];

				c->rate             = voice[i].SFrq;
				c->finalPeriod      = s->finalPeriod;
				c->fineTune         = s->fineTune;
				c->instrNr          = s->instrNr;
				c->sampleNr         = s->sampleNr;
				c->envSustainActive = s->envSustainActive;
				c->mute             = s->stOff | s->mute;
				c->status           = s->tmpStatus;
				c->finalVol         = s->finalVol;
				c->smpStartPos      = s->smpStartPos;
				c->smpPtr           = s->smpPtr;
				c->effTyp           = s->effTyp;
				c->eff              = s->eff;
				c->relTonNr         = s->relTonNr;
			}

			chSyncData.timestamp = audio.tickTime64;
			chQueuePush(chSyncData);

			audio.tickTime64 += tickTimeLen;

			pmpLeft = speedVal;
			replayerBusy = false;
		}

		b = a;
		if (b > pmpLeft)
			b = pmpLeft;

		mixAudio(stream, b, pmpChannels);
		stream += (b * pmpCountDiv);

		a -= b;
		pmpLeft -= b;
	}

	(void)(userdata); // make compiler happy
}

bool setupAudioBuffers(void)
{
	audio.mixBufferL = (int32_t *)(malloc(MAX_SAMPLES_PER_TICK * sizeof (int32_t)));
	audio.mixBufferR = (int32_t *)(malloc(MAX_SAMPLES_PER_TICK * sizeof (int32_t)));

	if ((audio.mixBufferL == NULL) || (audio.mixBufferR == NULL))
	{
		showErrorMsgBox("Not enough memory!");
		return (false); // allocated memory is free'd later
	}

	return (true);
}

void freeAudioBuffers(void)
{
	if (audio.mixBufferL != NULL)
	{
		free(audio.mixBufferL);
		audio.mixBufferL = NULL;
	}

	if (audio.mixBufferR != NULL)
	{
		free(audio.mixBufferR);
		audio.mixBufferR = NULL;
	}
}

void updateSendAudSamplesRoutine(bool lockMixer)
{
	if (lockMixer)
		lockMixerCallback();

	// force dither off if somehow set with 24-bit float (illegal)
	if (config.audioDither && (config.specialFlags & BITDEPTH_24))
		config.audioDither = false;

	if (config.audioDither)
	{
		if (config.specialFlags & BITDEPTH_16)
		{
			if (pmpChannels > 2)
				sendAudSamplesFunc = sendSamples16BitDitherMultiChan;
			else
				sendAudSamplesFunc = sendSamples16BitDitherStereo;
		}
	}
	else
	{
		if (config.specialFlags & BITDEPTH_16)
		{
			if (pmpChannels > 2)
				sendAudSamplesFunc = sendSamples16BitMultiChan;
			else
				sendAudSamplesFunc = sendSamples16BitStereo;
		}
		else
		{
			if (pmpChannels > 2)
				sendAudSamplesFunc = sendSamples24BitMultiChan;
			else
				sendAudSamplesFunc = sendSamples24BitStereo;
		}
	}

	if (lockMixer)
		unlockMixerCallback();
}

bool setupAudio(bool showErrorMsg)
{
	int8_t newBitDepth;
	uint8_t i;
	uint16_t configAudioBufSize;
	uint32_t stringLen;
	SDL_AudioSpec want, have;

	closeAudio();

	if ((config.audioFreq < MIN_AUDIO_FREQ) || (config.audioFreq > MAX_AUDIO_FREQ))
	{
		// set default rate
#ifdef __APPLE__
		config.audioFreq = 44100;
#else
		config.audioFreq = 48000;
#endif
	}

	// get audio buffer size from config special flags

	configAudioBufSize = 1024;
		 if (config.specialFlags & BUFFSIZE_512)  configAudioBufSize = 512;
	else if (config.specialFlags & BUFFSIZE_2048) configAudioBufSize = 2048;
	else if (config.specialFlags & BUFFSIZE_4096) configAudioBufSize = 4096;

	// set up audio device
	memset(&want, 0, sizeof (want));

	// these three may change after opening a device, but our mixer is dealing with it
	want.freq     = config.audioFreq;
	want.format   = (config.specialFlags & BITDEPTH_24) ? AUDIO_F32 : AUDIO_S16;
	want.channels = 2;
	// -------------------------------------------------------------------------------
	want.callback = mixCallback;
	want.samples  = configAudioBufSize;

	audio.dev = SDL_OpenAudioDevice(audio.currOutputDevice, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE); // prevent SDL2 from resampling
	if (audio.dev == 0)
	{
		if (showErrorMsg)
			showErrorMsgBox("Couldn't open audio device:\n%s\n\nDo you have any audio device enabled and plugged in?", SDL_GetError());

		return (false);
	}

	// test if the received audio format is compatible
	if ((have.format != AUDIO_S16) && (have.format != AUDIO_F32))
	{
		if (showErrorMsg)
			showErrorMsgBox("Couldn't open audio device:\nThe program doesn't support an SDL_AudioFormat of '%d' (not 16-bit or 24-bit float).",
				(uint32_t)(have.format));

		closeAudio();
		return (false);
	}

	// test if the received audio rate is compatible
	if ((have.freq != 44100) && (have.freq != 48000) && (have.freq != 96000))
	{
		if (showErrorMsg)
			showErrorMsgBox("Couldn't open audio device:\nThe program doesn't support an audio output rate of %dHz. Sorry!", have.freq);

		closeAudio();
		return (false);
	}

	// set new bit depth flag

	newBitDepth = 16;
	config.specialFlags &= ~BITDEPTH_24;
	config.specialFlags |=  BITDEPTH_16;

	if (have.format == AUDIO_F32)
	{
		newBitDepth = 24;
		config.specialFlags &= ~BITDEPTH_16;
		config.specialFlags |=  BITDEPTH_24;
	}

	// set a few variables

	config.audioFreq = have.freq;
	audio.freq       = have.freq;
	smpBuffSize      = have.samples;

	if (config.audioDither && (newBitDepth == 24))
		config.audioDither = false;

	if (song.speed == 0)
		song.speed = 125;

	setSpeed(song.speed);

	pmpChannels = have.channels;
	pmpCountDiv = pmpChannels * ((newBitDepth == 16) ? sizeof (int16_t) : sizeof (float));

	// make a copy of the new known working audio settings

	audio.lastWorkingAudioFreq = config.audioFreq;
	audio.lastWorkingAudioBits = config.specialFlags & (BITDEPTH_16   + BITDEPTH_24   + BUFFSIZE_512 +
														BUFFSIZE_1024 + BUFFSIZE_2048 + BUFFSIZE_4096);

	// store last working audio output device name

	if (audio.lastWorkingAudioDeviceName != NULL)
	{
		free(audio.lastWorkingAudioDeviceName);
		audio.lastWorkingAudioDeviceName = NULL;
	}

	if (audio.currOutputDevice != NULL)
	{
		stringLen = (uint32_t)(strlen(audio.currOutputDevice));

		audio.lastWorkingAudioDeviceName = (char *)(malloc(stringLen + 2));
		if (audio.lastWorkingAudioDeviceName != NULL)
		{
			if (stringLen > 0)
				strcpy(audio.lastWorkingAudioDeviceName, audio.currOutputDevice);
			audio.lastWorkingAudioDeviceName[stringLen + 1] = '\0'; // UTF-8 needs double null termination
		}
	}

	// update config audio radio buttons if we're on that screen at the moment
	if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_IO_DEVICES))
		showConfigScreen();

	updateWavRendererSettings();
	setAudioAmp(config.boostLevel, config.masterVol, (config.specialFlags & BITDEPTH_24) ? true : false);

	for (i = 0; i < MAX_VOICES; ++i)
		stopVoice(i);

	stopAllScopes();

	pmpLeft = 0; // reset sample counter

	calcReplayRate(audio.freq);

	updateSendAudSamplesRoutine(false);
	return (true);
}

void closeAudio(void)
{
	if (audio.dev > 0)
	{
		SDL_PauseAudioDevice(audio.dev, true);
		SDL_CloseAudioDevice(audio.dev);
		audio.dev = 0;
	}
}

/*
void benchmarkAudioChannelMixer(void) // for development testing
{
#define TICKS_TO_MIX 50000

#ifdef _WIN32
#define DEBUG_MOD_FILENAME L"C:\\programming\\debug.xm"
#elif __APPLE__
#define DEBUG_MOD_FILENAME "/users/olav/debug.xm"
#else
#define DEBUG_MOD_FILENAME "/home/olav/debug.xm"
#endif

	char str[128];
	uint8_t result, buffer[MAX_SAMPLES_PER_TICK * 2 * sizeof (float)];
	uint32_t i, start, end;

	result = loadMusicUnthreaded(DEBUG_MOD_FILENAME);
	if (!result)
	{
		editor.programRunning = false;
		showErrorMsgBox("Couldn't load debug module for mixer benchmarking!\n");
		return;
	}

	pauseAudio();
	editor.wavIsRendering = true;

	setPos(0, 0);
	playMode = PLAYMODE_SONG;
	songPlaying = true;

	resetChannels();
	setNewAudioFreq(config.audioFreq);
	setAudioAmp(config.boostLevel, config.masterVol, false);

	stopVoices();
	song.globVol = 64;
	setSpeed(song.speed);

	start = SDL_GetTicks();
	for (i = 0; i < TICKS_TO_MIX; ++i)
		dump_RenderTick(buffer);
	end = SDL_GetTicks();

	stopPlaying();
	resumeAudio();

	sprintf(str, "It took approximately %dms to mix %d ticks.\nRun this test again to confirm.", end - start, TICKS_TO_MIX);
	SDL_ShowSimpleMessageBox(0, "Channel mixer benchmark result", str, video.window);

	editor.programRunning = false;
}
*/

// panning table from FT2 code
const uint32_t panningTab[257] =
{
		0, 4096, 5793, 7094, 8192, 9159,10033,10837,11585,12288,12953,13585,14189,14768,15326,15864,
	16384,16888,17378,17854,18318,18770,19212,19644,20066,20480,20886,21283,21674,22058,22435,22806,
	23170,23530,23884,24232,24576,24915,25249,25580,25905,26227,26545,26859,27170,27477,27780,28081,
	28378,28672,28963,29251,29537,29819,30099,30377,30652,30924,31194,31462,31727,31991,32252,32511,
	32768,33023,33276,33527,33776,34024,34270,34514,34756,34996,35235,35472,35708,35942,36175,36406,
	36636,36864,37091,37316,37540,37763,37985,38205,38424,38642,38858,39073,39287,39500,39712,39923,
	40132,40341,40548,40755,40960,41164,41368,41570,41771,41972,42171,42369,42567,42763,42959,43154,
	43348,43541,43733,43925,44115,44305,44494,44682,44869,45056,45242,45427,45611,45795,45977,46160,
	46341,46522,46702,46881,47059,47237,47415,47591,47767,47942,48117,48291,48465,48637,48809,48981,
	49152,49322,49492,49661,49830,49998,50166,50332,50499,50665,50830,50995,51159,51323,51486,51649,
	51811,51972,52134,52294,52454,52614,52773,52932,53090,53248,53405,53562,53719,53874,54030,54185,
	54340,54494,54647,54801,54954,55106,55258,55410,55561,55712,55862,56012,56162,56311,56459,56608,
	56756,56903,57051,57198,57344,57490,57636,57781,57926,58071,58215,58359,58503,58646,58789,58931,
	59073,59215,59357,59498,59639,59779,59919,60059,60199,60338,60477,60615,60753,60891,61029,61166,
	61303,61440,61576,61712,61848,61984,62119,62254,62388,62523,62657,62790,62924,63057,63190,63323,
	63455,63587,63719,63850,63982,64113,64243,64374,64504,64634,64763,64893,65022,65151,65279,65408,
	65536
};
