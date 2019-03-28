// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ft2_gui.h"
#include "ft2_mouse.h"
#include "ft2_sample_ed.h"
#include "ft2_video.h"

// these may very well change after opening the audio input device
#define SAMPLING_BUFFER_SIZE 1024
#define SAMPLING_FREQUENCY 44100

static volatile bool drawSamplingBufferFlag, outOfMemoryFlag, noMoreRoomFlag;
static int16_t *currWriteBuf, displayBuffer1[SAMPLING_BUFFER_SIZE], displayBuffer2[SAMPLING_BUFFER_SIZE];
static volatile int32_t currSampleLen, displayBufferLen;
static SDL_AudioDeviceID recordDev;

static void SDLCALL samplingCallback(void *userdata, Uint8 *stream, int len)
{
	if ((currSmp == NULL) || (len < 0))
		return;

	currSmp->pek = (int8_t *)(realloc(currSmp->pek, (currSmp->len + len) + 4)); // +4 for interpolation loop fix
	if (currSmp->pek == NULL)
	{
		drawSamplingBufferFlag = false;
		outOfMemoryFlag = true;
		return;
	}

	displayBufferLen = MAX(SAMPLING_BUFFER_SIZE * 2, len);
	currSampleLen = currSmp->len;

	memcpy(&currSmp->pek[currSmp->len], stream, len);

	// fill display buffer
	memcpy(currWriteBuf, stream, displayBufferLen);

	// swap write buffer (double-buffering)
	if (currWriteBuf == displayBuffer1)
		currWriteBuf = displayBuffer2;
	else
		currWriteBuf = displayBuffer1;

	currSmp->len += len;
	if (currSmp->len > MAX_SAMPLE_LEN) // length overflow
	{
		currSmp->len -= len;

		drawSamplingBufferFlag = false;
		noMoreRoomFlag = true;

		return;
	}

	drawSamplingBufferFlag = true;

	(void)(userdata); // prevent compiler warning
}

void stopSampling(void)
{
	resumeAudio();
	mouseAnimOff();

	SDL_CloseAudioDevice(recordDev);
	editor.samplingAudioFlag = false;

	updateSampleEditorSample();
}

static uint8_t getDispBuffPeakSmp(const int16_t *smpData, int32_t pos, int32_t len)
{
	const int16_t *ptr16;
	int16_t smp16;
	uint32_t smpAbs, max;
	int32_t numSamples, i;

	assert(smpData != NULL);

	len /= 2; // convert from number of bytes to number of samples

	numSamples = len / SAMPLE_AREA_WIDTH;
	if (numSamples <= 0)
		return (0);

	pos   = (pos * len) / SAMPLE_AREA_WIDTH;
	ptr16 = &smpData[pos];

	max = 0;
	for (i = 0; i < numSamples; ++i)
	{
		smp16 = ptr16[i];

		smpAbs = ABS(smp16);
		if (smpAbs > max)
			max = smpAbs;
	}

	max = (max * SAMPLE_AREA_HEIGHT) >> 16;
	if (max > 76)
		max = 76;

	return ((uint8_t)(max));
}

void handleSamplingUpdates(void)
{
	uint8_t smpAbs;
	int16_t *readBuf;
	uint16_t x;
	uint32_t *dstPtr, pixVal;

	if (outOfMemoryFlag)
	{
		outOfMemoryFlag = false;

		stopSampling();
		okBox(0, "System message", "Not enough memory!");

		return;
	}

	if (noMoreRoomFlag)
	{
		noMoreRoomFlag = false;

		stopSampling();
		okBox(0, "System message", "Not more room in sample!");

		return;
	}

	if (drawSamplingBufferFlag)
	{
		drawSamplingBufferFlag = false;

		// clear sample data area (has to be *FAST*, so don't use clearRect())
		memset(&video.frameBuffer[174 * SCREEN_W], 0, SAMPLE_AREA_WIDTH * SAMPLE_AREA_HEIGHT * sizeof (int32_t));

		dstPtr = &video.frameBuffer[SAMPLE_AREA_Y_CENTER * SCREEN_W];
		pixVal = video.palette[PAL_PATTEXT];

		// select buffer currently not being written to (double-buffering)
		if (currWriteBuf == displayBuffer1)
			readBuf = displayBuffer2;
		else
			readBuf = displayBuffer1;

		for (x = 0; x < SAMPLE_AREA_WIDTH; x++)
		{
			smpAbs = getDispBuffPeakSmp(readBuf, x, displayBufferLen);
			if (smpAbs == 0)
				dstPtr[x] = pixVal;
			else
				vLine(x, SAMPLE_AREA_Y_CENTER - smpAbs, (smpAbs * 2) + 1, PAL_PATTEXT);
		}

		// clear and draw new sample length number
		fillRect(536, 362, 56, 10, PAL_DESKTOP);
		hexOut(536, 362, PAL_FORGRND, currSampleLen, 8);
	}
}

void startSampling(void)
{
#if SDL_PATCHLEVEL < 5
	okBox(2, "System message", "The program needs to be compiled with SDL 2.0.5 or later to support audio sampling.");
	return;
#else
	SDL_AudioSpec want, have;

	if (okBox(2, "System request", "Clear sample data and start sampling? While ongoing, press any key to stop.") != 1)
		return;

	if (editor.samplingAudioFlag || (editor.curInstr == 0))
		return;

	mouseAnimOn();

	currSmp = &instr[editor.curInstr].samp[editor.curSmp];

	memset(&want, 0, sizeof (SDL_AudioSpec));
	want.freq     = SAMPLING_FREQUENCY;
	want.format   = AUDIO_S16;
	want.channels = 1;
	want.callback = samplingCallback;
	want.userdata = NULL;
	want.samples  = SAMPLING_BUFFER_SIZE;

	recordDev = SDL_OpenAudioDevice(audio.currInputDevice, true, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (recordDev == 0)
	{
		okBox(0, "System message", "Couldn't open audio input device.");
		return;
	}

	pauseAudio();

	// wipe current sample and prepare it
	freeSample(currSmp);

	currSmp->typ |= 16; // we always sample in 16-bit
	currSmp->vol  = 64;
	currSmp->pan  = 128;

	tuneSample(currSmp, have.freq); // tune sample (relTone/finetune) to the sampling frequency we obtained
	updateSampleEditorSample();
	updateSampleEditor();

	setSongModifiedFlag();

	displayBufferLen = 0;
	currWriteBuf = displayBuffer1;
	memset(displayBuffer1, 0, sizeof (displayBuffer1));
	memset(displayBuffer2, 0, sizeof (displayBuffer2));

	editor.samplingAudioFlag = true;

	SDL_PauseAudioDevice(recordDev, false);
#endif
}
