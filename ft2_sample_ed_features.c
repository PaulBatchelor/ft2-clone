/*
** This file contains the routines for the following sample editor functions:
** - Resampler
** - Echo
** - Mix
** - Volume
*/

// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "ft2_header.h"
#include "ft2_mouse.h"
#include "ft2_audio.h"
#include "ft2_gui.h"
#include "ft2_events.h"
#include "ft2_video.h"
#include "ft2_inst_ed.h"
#include "ft2_sample_ed.h"

static int8_t smpEd_RelReSmp, mix_Balance = 50;
static bool stopThread, echo_AddMemory, exitFlag, outOfMemory;
static int16_t vol_StartVol = 100, vol_EndVol = 100, echo_nEcho = 1, echo_VolChange = 30;
static int32_t echo_Distance = 0x100;
static SDL_Thread *thread;

static void pbExit(void)
{
	editor.ui.sysReqShown = false;
	exitFlag = true;
}

static void windowOpen(void)
{
	editor.ui.sysReqShown = true;

#ifndef __APPLE__
	if (!video.fullscreen) // release mouse button trap
		SDL_SetWindowGrab(video.window, SDL_FALSE);
#endif

	SDL_EventState(SDL_DROPFILE, SDL_DISABLE);

	unstuckAllGUIElements();

	mouse.lastUsedObjectType = OBJECT_NONE;
	mouse.lastUsedObjectID   = OBJECT_ID_NONE;
	mouse.leftButtonPressed  = 0;
	mouse.rightButtonPressed = 0;
}

static void windowClose(bool rewriteSample)
{
	mouse.lastUsedObjectID   = OBJECT_ID_NONE;
	mouse.lastUsedObjectType = OBJECT_NONE;

	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	if (exitFlag || rewriteSample)
		writeSample(true);
	else
		updateNewSample();

	mouseAnimOff();
}

static void sbSetResampleTones(uint32_t pos)
{
	int8_t val;

	val = (int8_t)(pos - 36);
	if (val != smpEd_RelReSmp)
		smpEd_RelReSmp = val;
}

static void pbResampleTonesDown(void)
{
	if (smpEd_RelReSmp > -36)
		smpEd_RelReSmp--;
}

static void pbResampleTonesUp(void)
{
	if (smpEd_RelReSmp < 36)
		smpEd_RelReSmp++;
}

static int32_t SDLCALL resampleThread(void *ptr)
{
	int8_t *p1, *p2, *src8, *dst8;
	int16_t *src16, *dst16;
	uint32_t newLen, mask, i, resampleLen;
	uint64_t posfrac64, delta64;
	double dNewLen, dLenMul;

	(void)(ptr);

	mask = (currSmp->typ & 16) ? 0xFFFFFFFE : 0xFFFFFFFF;
	dLenMul = pow(2.0, smpEd_RelReSmp / 12.0);

	dNewLen = currSmp->len * dLenMul;
	if (dNewLen > (double)(MAX_SAMPLE_LEN))
		dNewLen = (double)(MAX_SAMPLE_LEN);

	newLen = (int32_t)(dNewLen) & mask;

	p2 = (int8_t *)(malloc(newLen + 4));
	if (p2 == NULL)
	{
		outOfMemory = true;
		setMouseBusy(false);
		editor.ui.sysReqShown = false;
		return (true);
	}

	p1 = currSmp->pek;

	// don't use the potentially clamped newLen value here
	delta64 = ((uint64_t)(currSmp->len) << 32) / (uint64_t)(currSmp->len * dLenMul);

	posfrac64 = 0;

	pauseAudio();
	restoreSample(currSmp);

	if (newLen > 0)
	{
		if (currSmp->typ & 16)
		{
			src16 = (int16_t *)(p1);
			dst16 = (int16_t *)(p2);

			resampleLen = newLen / 2;
			for (i = 0; i < resampleLen; ++i)
			{
				dst16[i] = src16[posfrac64 >> 32];
				posfrac64 += delta64;
			}
		}
		else
		{
			src8 = p1;
			dst8 = p2;

			for (i = 0; i < newLen; ++i)
			{
				dst8[i] = src8[posfrac64 >> 32];
				posfrac64 += delta64;
			}
		}
	}

	free(p1);

	currSmp->relTon = CLAMP(currSmp->relTon + smpEd_RelReSmp, -48, 71);

	currSmp->len  = newLen;
	currSmp->pek  = p2;
	currSmp->repS = (int32_t)(currSmp->repS * dLenMul) & mask;
	currSmp->repL = (int32_t)(currSmp->repL * dLenMul) & mask;

	if (currSmp->repS > currSmp->len)
		currSmp->repS = currSmp->len;

	if ((currSmp->repS + currSmp->repL) > currSmp->len)
		currSmp->repL  = currSmp->len   - currSmp->repS;

	if (currSmp->typ & 16)
	{
		currSmp->len  &= 0xFFFFFFFE;
		currSmp->repS &= 0xFFFFFFFE;
		currSmp->repL &= 0xFFFFFFFE;
	}

	if (currSmp->repL == 0)
		currSmp->typ &= ~3; // disable loop

	fixSample(currSmp);
	resumeAudio();

	setSongModifiedFlag();
	setMouseBusy(false);

	editor.ui.sysReqShown = false;
	return (true);
}

static void pbDoResampling(void)
{
	mouseAnimOn();
	thread = SDL_CreateThread(resampleThread, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static void drawResampleBox(void)
{
	char sign;
	const int16_t x = 209;
	const int16_t y = 230;
	const int16_t w = 214;
	const int16_t h = 54;
	uint16_t val;
	uint32_t mask;
	double dNewLen, dLenMul;

	// main fill
	fillRect(x + 1, y + 1, w - 2, h - 2, PAL_BUTTONS);

	// outer border
	vLine(x,         y,         h - 1, PAL_BUTTON1);
	hLine(x + 1,     y,         w - 2, PAL_BUTTON1);
	vLine(x + w - 1, y,         h,     PAL_BUTTON2);
	hLine(x,         y + h - 1, w - 1, PAL_BUTTON2);

	// inner border
	vLine(x + 2,     y + 2,     h - 5, PAL_BUTTON2);
	hLine(x + 3,     y + 2,     w - 6, PAL_BUTTON2);
	vLine(x + w - 3, y + 2,     h - 4, PAL_BUTTON1);
	hLine(x + 2,     y + h - 3, w - 4, PAL_BUTTON1);

	mask = (currSmp->typ & 16) ? 0xFFFFFFFE : 0xFFFFFFFF;
	dLenMul = pow(2.0, smpEd_RelReSmp / 12.0);

	dNewLen = currSmp->len * dLenMul;
	if (dNewLen > (double)(MAX_SAMPLE_LEN))
		dNewLen = (double)(MAX_SAMPLE_LEN);

	textOutShadow(215, 236, PAL_FORGRND, PAL_BUTTON2, "Rel. h.tones");
	textOutShadow(215, 250, PAL_FORGRND, PAL_BUTTON2, "New sample size");
	hexOut(361, 250, PAL_FORGRND, (uint32_t)(dNewLen) & mask, 8);

		 if (smpEd_RelReSmp == 0) sign = ' ';
	else if (smpEd_RelReSmp  < 0) sign = '-';
	else                          sign = '+';

	val = ABS(smpEd_RelReSmp);
	if (val > 9)
	{
		charOut(291, 236, PAL_FORGRND, sign);
		charOut(298, 236, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(305, 236, PAL_FORGRND, '0' + (val % 10));
	}
	else
	{
		charOut(298, 236, PAL_FORGRND, sign);
		charOut(305, 236, PAL_FORGRND, '0' + (val % 10));
	}
}

static void setupResampleBoxWidgets(void)
{
	pushButton_t *p;
	scrollBar_t *s;

	// "Apply" pushbutton
	p = &pushButtons[0];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Apply";
	p->x = 214;
	p->y = 264;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbDoResampling;
	p->visible = true;

	// "Exit" pushbutton
	p = &pushButtons[1];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Exit";
	p->x = 345;
	p->y = 264;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbExit;
	p->visible = true;

	// scrollbar buttons

	p = &pushButtons[2];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 314;
	p->y = 234;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbResampleTonesDown;
	p->visible = true;

	p = &pushButtons[3];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 395;
	p->y = 234;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbResampleTonesUp;
	p->visible = true;

	// echo num scrollbar
	s = &scrollBars[0];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 337;
	s->y = 234;
	s->w = 58;
	s->h = 13;
	s->callbackFunc = sbSetResampleTones;
	s->visible = true;
	setScrollBarPageLength(0, 1);
	setScrollBarEnd(0, 36 * 2);
}

void pbSampleResample(void)
{
	uint16_t i;

	if ((editor.curInstr == 0) || (currSmp->pek == NULL))
		return;

	setupResampleBoxWidgets();
	windowOpen();

	outOfMemory = false;

	exitFlag = false;
	while (editor.ui.sysReqShown)
	{
		readInput();
		setSyncedReplayerVars();
		handleRedrawing();

		drawResampleBox();
		setScrollBarPos(0, smpEd_RelReSmp + 36, false);
		drawCheckBox(0);
		for (i = 0; i < 4; ++i) drawPushButton(i);
		drawScrollBar(0);

		flipFrame();
	}

	for (i = 0; i < 4; ++i) hidePushButton(i);
	hideScrollBar(0);

	windowClose(false);

	if (outOfMemory)
		okBox(0, "System message", "Not enough memory!");
}

static void cbEchoAddMemory(void)
{
	echo_AddMemory ^= 1;
}

static void sbSetEchoNumPos(uint32_t pos)
{
	if (echo_nEcho != (int32_t)(pos))
		echo_nEcho = (int16_t)(pos);
}

static void sbSetEchoDistPos(uint32_t pos)
{
	if (echo_Distance != (int32_t)(pos))
		echo_Distance = (int32_t)(pos);
}

static void sbSetEchoFadeoutPos(uint32_t pos)
{
	if (echo_VolChange != (int32_t)(pos))
		echo_VolChange = (int16_t)(pos);
}

static void pbEchoNumDown(void)
{
	if (echo_nEcho > 0)
		echo_nEcho--;
}

static void pbEchoNumUp(void)
{
	if (echo_nEcho < 1024)
		echo_nEcho++;
}

static void pbEchoDistDown(void)
{
	if (echo_Distance > 0)
		echo_Distance--;
}

static void pbEchoDistUp(void)
{
	if (echo_Distance < 16384)
		echo_Distance++;
}

static void pbEchoFadeoutDown(void)
{
	if (echo_VolChange > 0)
		echo_VolChange--;
}

static void pbEchoFadeoutUp(void)
{
	if (echo_VolChange < 100)
		echo_VolChange++;
}

static int32_t SDLCALL createEchoThread(void *ptr)
{
	int8_t *readPtr, *writePtr;
	bool is16Bit;
	int32_t numEchoes, distance, readLen, writeLen, i, j;
	int32_t tmp32, smpOut, smpMul, echoRead, echoCycle, writeIdx;
	double dTmp;

	(void)(ptr); // prevent compiler warning

	readLen  = currSmp->len;
	readPtr  = currSmp->pek;
	is16Bit  = (currSmp->typ & 16) ? true : false;
	distance = is16Bit ? (echo_Distance * 32) : (echo_Distance * 16);

	// calculate real number of echoes
	j = is16Bit ? 32768 : 128; i = 0;
	while ((i < echo_nEcho) && (j > 0))
	{
		j = (j * echo_VolChange) / 100;
		i++;
	}
	numEchoes = i + 1;

	// set write length (either original length or full echo length)
	writeLen = readLen;
	if (echo_AddMemory)
	{
		dTmp = writeLen + ((double)(distance) * (numEchoes - 1));
		if (dTmp > (double)(MAX_SAMPLE_LEN))
			writeLen = MAX_SAMPLE_LEN;
		else
			writeLen += (distance * (numEchoes - 1));

		if (is16Bit)
			writeLen &= 0xFFFFFFFE;
	}

	writePtr = (int8_t *)(malloc(writeLen + 4));
	if (writePtr == NULL)
	{
		outOfMemory = true;
		setMouseBusy(false);
		editor.ui.sysReqShown = false;
		return (false);
	}

	pauseAudio();
	restoreSample(currSmp);

	writeIdx = 0;
	while (!stopThread && (writeIdx < writeLen))
	{
		tmp32  = 0;
		smpOut = 0;
		smpMul = 32768;

		echoRead  = writeIdx;
		echoCycle = numEchoes;

		while (!stopThread && (echoRead > 0) && (echoCycle-- > 0))
		{
			if (echoRead < readLen)
			{
				if (is16Bit)
					tmp32 = *((int16_t *)(&readPtr[echoRead & 0xFFFFFFFE]));
				else
					tmp32 = readPtr[echoRead] << 8;

				dTmp = (tmp32 * smpMul) / 32768.0;

				if (cpu.hasSSE2)
					sse2_double2int32_round(tmp32, dTmp);
				else
					tmp32 = (int32_t)(round(dTmp));

				smpOut += tmp32;
			}

			dTmp = (echo_VolChange * smpMul) / 100.0;

			if (cpu.hasSSE2)
				sse2_double2int32_round(smpMul, dTmp);
			else
				smpMul = (int32_t)(round(dTmp));

			echoRead -= distance;
		}
		CLAMP16(smpOut);

		if (is16Bit)
		{
			*((int16_t *)(&writePtr[writeIdx & 0xFFFFFFFE])) = (int16_t)(smpOut);
			writeIdx += 2;
		}
		else
		{
			writePtr[writeIdx++] = (int8_t)(smpOut >> 8);
		}
	}

	free(readPtr);

	if (stopThread)
	{
		writeLen = writeIdx;
		currSmp->pek = (int8_t *)(realloc(writePtr, writeIdx + 4));
		editor.updateCurSmp = true;
	}
	else
	{
		currSmp->pek = writePtr;
	}

	if (is16Bit)
		writeLen &= 0xFFFFFFFE;

	currSmp->len = writeLen;

	fixSample(currSmp);
	resumeAudio();

	setSongModifiedFlag();
	setMouseBusy(false);

	editor.ui.sysReqShown = false;
	return (true);
}

static void pbCreateEcho(void)
{
	stopThread = false;

	mouseAnimOn();
	thread = SDL_CreateThread(createEchoThread, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static void drawEchoBox(void)
{
	const int16_t x = 171;
	const int16_t y = 220;
	const int16_t w = 291;
	const int16_t h = 66;

	// main fill
	fillRect(x + 1, y + 1, w - 2, h - 2, PAL_BUTTONS);

	// outer border
	vLine(x,         y,         h - 1, PAL_BUTTON1);
	hLine(x + 1,     y,         w - 2, PAL_BUTTON1);
	vLine(x + w - 1, y,         h,     PAL_BUTTON2);
	hLine(x,         y + h - 1, w - 1, PAL_BUTTON2);

	// inner border
	vLine(x + 2,     y + 2,     h - 5, PAL_BUTTON2);
	hLine(x + 3,     y + 2,     w - 6, PAL_BUTTON2);
	vLine(x + w - 3, y + 2,     h - 4, PAL_BUTTON1);
	hLine(x + 2,     y + h - 3, w - 4, PAL_BUTTON1);

	textOutShadow(177, 226, PAL_FORGRND, PAL_BUTTON2, "Number of echoes");
	textOutShadow(177, 239, PAL_FORGRND, PAL_BUTTON2, "Echo distance");
	textOutShadow(177, 253, PAL_FORGRND, PAL_BUTTON2, "Fade out");
	textOutShadow(192, 270, PAL_FORGRND, PAL_BUTTON2, "Add memory to sample");

	assert(echo_nEcho <= 1024);

	charOut(315 + (0 * 7), 226, PAL_FORGRND, '0' + (echo_nEcho / 1000) % 10);
	charOut(315 + (1 * 7), 226, PAL_FORGRND, '0' + (echo_nEcho / 100) % 10);
	charOut(315 + (2 * 7), 226, PAL_FORGRND, '0' + (echo_nEcho / 10) % 10);
	charOut(315 + (3 * 7), 226, PAL_FORGRND, '0' + (echo_nEcho % 10));

	assert((echo_Distance * 16) <= 262144);

	hexOut(308, 240, PAL_FORGRND, echo_Distance * 16, 5);

	assert(echo_VolChange <= 100);

	charOut(312 + (0 * 7), 254, PAL_FORGRND, '0' + (echo_VolChange / 100) % 10);
	charOut(312 + (1 * 7), 254, PAL_FORGRND, '0' + (echo_VolChange / 10) % 10);
	charOut(312 + (2 * 7), 254, PAL_FORGRND, '0' + (echo_VolChange % 10));
	charOutShadow(313 + (3 * 7), 254, PAL_FORGRND, PAL_BUTTON2, '%');
}

static void setupEchoBoxWidgets(void)
{
	checkBox_t *c;
	pushButton_t *p;
	scrollBar_t *s;

	// "Add memory to sample" checkbox
	c = &checkBoxes[0];
	memset(c, 0, sizeof (checkBox_t));
	c->x = 176;
	c->y = 268;
	c->clickAreaWidth = 146;
	c->clickAreaHeight = 12;
	c->callbackFunc = cbEchoAddMemory;
	c->checked = echo_AddMemory ? CHECKBOX_CHECKED : CHECKBOX_UNCHECKED;
	c->visible = true;

	// "Apply" pushbutton
	p = &pushButtons[0];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Apply";
	p->x = 345;
	p->y = 266;
	p->w = 56;
	p->h = 16;
	p->callbackFuncOnUp = pbCreateEcho;
	p->visible = true;

	// "Exit" pushbutton
	p = &pushButtons[1];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Exit";
	p->x = 402;
	p->y = 266;
	p->w = 55;
	p->h = 16;
	p->callbackFuncOnUp = pbExit;
	p->visible = true;

	// scrollbar buttons

	p = &pushButtons[2];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 345;
	p->y = 224;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoNumDown;
	p->visible = true;

	p = &pushButtons[3];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 434;
	p->y = 224;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoNumUp;
	p->visible = true;

	p = &pushButtons[4];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 345;
	p->y = 238;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoDistDown;
	p->visible = true;

	p = &pushButtons[5];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 434;
	p->y = 238;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoDistUp;
	p->visible = true;

	p = &pushButtons[6];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 345;
	p->y = 252;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoFadeoutDown;
	p->visible = true;

	p = &pushButtons[7];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 434;
	p->y = 252;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbEchoFadeoutUp;
	p->visible = true;

	// echo num scrollbar
	s = &scrollBars[0];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 368;
	s->y = 224;
	s->w = 66;
	s->h = 13;
	s->callbackFunc = sbSetEchoNumPos;
	s->visible = true;
	setScrollBarPageLength(0, 1);
	setScrollBarEnd(0, 1024);

	// echo distance scrollbar
	s = &scrollBars[1];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 368;
	s->y = 238;
	s->w = 66;
	s->h = 13;
	s->callbackFunc = sbSetEchoDistPos;
	s->visible = true;
	setScrollBarPageLength(1, 1);
	setScrollBarEnd(1, 16384);

	// echo fadeout scrollbar
	s = &scrollBars[2];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 368;
	s->y = 252;
	s->w = 66;
	s->h = 13;
	s->callbackFunc = sbSetEchoFadeoutPos;
	s->visible = true;
	setScrollBarPageLength(2, 1);
	setScrollBarEnd(2, 100);
}

void handleEchoToolPanic(void)
{
	stopThread = true;
}

void pbSampleEcho(void)
{
	uint16_t i;

	if ((editor.curInstr == 0) || (currSmp->pek == NULL))
		return;

	setupEchoBoxWidgets();
	windowOpen();

	outOfMemory = false;

	exitFlag = false;
	while (editor.ui.sysReqShown)
	{
		readInput();
		setSyncedReplayerVars();
		handleRedrawing();

		drawEchoBox();
		setScrollBarPos(0, echo_nEcho,     false);
		setScrollBarPos(1, echo_Distance,  false);
		setScrollBarPos(2, echo_VolChange, false);
		drawCheckBox(0);
		for (i = 0; i < 8; ++i) drawPushButton(i);
		for (i = 0; i < 3; ++i) drawScrollBar(i);

		flipFrame();
	}

	hideCheckBox(0);
	for (i = 0; i < 8; ++i) hidePushButton(i);
	for (i = 0; i < 3; ++i) hideScrollBar(i);

	windowClose(echo_AddMemory ? false : true);

	if (outOfMemory)
		okBox(0, "System message", "Not enough memory!");
}

static int32_t SDLCALL mixThread(void *ptr)
{
	int8_t *dstPtr, *mixPtr, *p, dstRelTone;
	uint8_t mixTyp, dstTyp;
	int32_t smp32, x1, x2, i, dstLen, mixLen, maxLen, dst8Size, max8Size, mix8Size;
	instrTyp *srcIns, *dstIns;
	sampleTyp *srcSmp, *dstSmp;
	double dSmp;

	(void)(ptr);

	if ((editor.curInstr == editor.srcInstr) && (editor.curSmp == editor.srcSmp))
	{
		setMouseBusy(false);
		editor.ui.sysReqShown = false;
		return (true);
	}

	srcIns = &instr[editor.srcInstr];
	dstIns = &instr[editor.curInstr];

	srcSmp = &srcIns->samp[editor.srcSmp];
	dstSmp = &dstIns->samp[editor.curSmp];

	mixLen = srcSmp->len;
	mixPtr = srcSmp->pek;
	mixTyp = srcSmp->typ;

	if (mixPtr == NULL)
	{
		mixLen = 0;
		mixTyp = 0;
	}

	dstLen     = dstSmp->len;
	dstPtr     = dstSmp->pek;
	dstTyp     = dstSmp->typ;
	dstRelTone = dstSmp->relTon;

	if (dstPtr == NULL)
	{
		dstLen = 0;
		dstTyp = mixTyp;
		dstRelTone = srcSmp->relTon;
	}

	mix8Size = (mixTyp & 16) ? (mixLen / 2) : mixLen;
	dst8Size = (dstTyp & 16) ? (dstLen / 2) : dstLen;
	max8Size = (dst8Size > mix8Size) ? dst8Size : mix8Size;

	maxLen = (dstTyp & 16) ? (max8Size * 2) : max8Size;
	if (maxLen <= 0)
	{
		setMouseBusy(false);
		editor.ui.sysReqShown = false;
		return (true);
	}

	p = (int8_t *)(calloc(maxLen + 2, sizeof (int8_t)));
	if (p == NULL)
	{
		outOfMemory = true;
		setMouseBusy(false);
		editor.ui.sysReqShown = false;
		return (true);
	}

	pauseAudio();
	restoreSample(dstSmp);
	restoreSample(srcSmp);

	for (i = 0; i < max8Size; ++i)
	{
		x1 = (i >= mix8Size) ? 0 : getSampleValueNr(mixPtr, mixTyp, (mixTyp & 16) ? (i << 1) : i);
		x2 = (i >= dst8Size) ? 0 : getSampleValueNr(dstPtr, dstTyp, (dstTyp & 16) ? (i << 1) : i);

		if (!(mixTyp & 16)) x1 <<= 8;
		if (!(dstTyp & 16)) x2 <<= 8;

		dSmp = ((x1 * mix_Balance) + (x2 * (100 - mix_Balance))) / 100.0;

		if (cpu.hasSSE2)
			sse2_double2int32_round(smp32, dSmp);
		else
			smp32 = (int32_t)(round(dSmp));

		CLAMP16(smp32);

		if (!(dstTyp & 16))
			smp32 >>= 8;

		putSampleValueNr(p, dstTyp, (dstTyp & 16) ? (i << 1) : i, (int16_t)(smp32));
	}

	if (dstSmp->pek != NULL)
		free(dstSmp->pek);

	if (currSmp->typ & 16)
		maxLen &= 0xFFFFFFFE;

	dstSmp->pek    = p;
	dstSmp->len    = maxLen;
	dstSmp->typ    = dstTyp;
	dstSmp->relTon = dstRelTone;

	if (dstSmp->repL == 0)
		dstSmp->typ &= ~3; // disable loop

	fixSample(srcSmp);
	fixSample(dstSmp);
	resumeAudio();

	setSongModifiedFlag();
	setMouseBusy(false);

	editor.ui.sysReqShown = false;
	return (true);
}

static void pbMix(void)
{
	mouseAnimOn();
	thread = SDL_CreateThread(mixThread, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static void sbSetMixBalancePos(uint32_t pos)
{
	if ((int32_t)(pos) != mix_Balance)
		mix_Balance = (int8_t)(pos);
}

static void pbMixBalanceDown(void)
{
	if (mix_Balance > 0)
		mix_Balance--;
}

static void pbMixBalanceUp(void)
{
	if (mix_Balance < 100)
		mix_Balance++;
}

static void drawMixSampleBox(void)
{
	const int16_t x = 192;
	const int16_t y = 240;
	const int16_t w = 248;
	const int16_t h = 38;

	// main fill
	fillRect(x + 1, y + 1, w - 2, h - 2, PAL_BUTTONS);

	// outer border
	vLine(x,         y,         h - 1, PAL_BUTTON1);
	hLine(x + 1,     y,         w - 2, PAL_BUTTON1);
	vLine(x + w - 1, y,         h,     PAL_BUTTON2);
	hLine(x,         y + h - 1, w - 1, PAL_BUTTON2);

	// inner border
	vLine(x + 2,     y + 2,     h - 5, PAL_BUTTON2);
	hLine(x + 3,     y + 2,     w - 6, PAL_BUTTON2);
	vLine(x + w - 3, y + 2,     h - 4, PAL_BUTTON1);
	hLine(x + 2,     y + h - 3, w - 4, PAL_BUTTON1);

	textOutShadow(198, 246, PAL_FORGRND, PAL_BUTTON2, "Mixing balance");

	assert((mix_Balance >= 0) && (mix_Balance <= 100));

	charOut(299 + (0 * 7), 246, PAL_FORGRND, '0' + ((mix_Balance / 100) % 10));
	charOut(299 + (1 * 7), 246, PAL_FORGRND, '0' + ((mix_Balance / 10) % 10));
	charOut(299 + (2 * 7), 246, PAL_FORGRND, '0' + (mix_Balance % 10));
}

static void setupMixBoxWidgets(void)
{
	pushButton_t *p;
	scrollBar_t *s;

	// "Apply" pushbutton
	p = &pushButtons[0];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Apply";
	p->x = 197;
	p->y = 258;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbMix;
	p->visible = true;

	// "Exit" pushbutton
	p = &pushButtons[1];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Exit";
	p->x = 361;
	p->y = 258;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbExit;
	p->visible = true;

	// scrollbar buttons

	p = &pushButtons[2];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 322;
	p->y = 244;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbMixBalanceDown;
	p->visible = true;

	p = &pushButtons[3];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 411;
	p->y = 244;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbMixBalanceUp;
	p->visible = true;

	// mixing balance scrollbar
	s = &scrollBars[0];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 345;
	s->y = 244;
	s->w = 66;
	s->h = 13;
	s->callbackFunc = sbSetMixBalancePos;
	s->visible = true;
	setScrollBarPageLength(0, 1);
	setScrollBarEnd(0, 100);
}

void pbSampleMix(void)
{
	uint16_t i;

	if (editor.curInstr == 0)
		return;

	setupMixBoxWidgets();
	windowOpen();

	outOfMemory = false;

	exitFlag = false;
	while (editor.ui.sysReqShown)
	{
		readInput();
		setSyncedReplayerVars();
		handleRedrawing();

		drawMixSampleBox();
		setScrollBarPos(0, mix_Balance, false);
		for (i = 0; i < 4; ++i) drawPushButton(i);
		drawScrollBar(0);

		flipFrame();
	}

	for (i = 0; i < 4; ++i) hidePushButton(i);
	hideScrollBar(0);

	windowClose(false);

	if (outOfMemory)
		okBox(0, "System message", "Not enough memory!");
}

static void sbSetStartVolPos(uint32_t pos)
{
	int16_t val;

	val = (int16_t)(pos - 200);
	if (val != vol_StartVol)
	{
			 if (ABS(val)       < 10) val =    0;
		else if (ABS(val - 100) < 10) val =  100;
		else if (ABS(val + 100) < 10) val = -100;

		vol_StartVol = val;
	}
}

static void sbSetEndVolPos(uint32_t pos)
{
	int16_t val;

	val = (int16_t)(pos - 200);
	if (val != vol_EndVol)
	{
			 if (ABS(val)       < 10) val =    0;
		else if (ABS(val - 100) < 10) val =  100;
		else if (ABS(val + 100) < 10) val = -100;

		vol_EndVol = val;
	}
}

static void pbSampStartVolDown(void)
{
	if (vol_StartVol > -200)
		vol_StartVol--;
}

static void pbSampStartVolUp(void)
{
	if (vol_StartVol < 200)
		vol_StartVol++;
}

static void pbSampEndVolDown(void)
{
	if (vol_EndVol > -200)
		vol_EndVol--;
}

static void pbSampEndVolUp(void)
{
	if (vol_EndVol < 200)
		vol_EndVol++;
}

static int32_t SDLCALL applyVolumeThread(void *ptr)
{
	int8_t *ptr8;
	int16_t *ptr16;
	int32_t smp, x1, x2, len, i;
	double dSmp;

	(void)(ptr);

	if (smpEd_Rx1 < smpEd_Rx2)
	{
		x1 = smpEd_Rx1;
		x2 = smpEd_Rx2;

		if (x2 > currSmp->len)
			x2 = currSmp->len;

		if (x1 < 0)
			x1 = 0;

		if (x2 <= x1)
		{
			setMouseBusy(false);
			editor.ui.sysReqShown = false;
			return (true);
		}

		if (currSmp->typ & 16)
		{
			x1 &= 0xFFFFFFFE;
			x2 &= 0xFFFFFFFE;
		}
	}
	else
	{
		x1 = 0;
		x2 = currSmp->len;
	}

	if (currSmp->typ & 16)
	{
		x1 /= 2;
		x2 /= 2;
	}

	len = x2 - x1;

	pauseAudio();

	restoreSample(currSmp);
	if (currSmp->typ & 16)
	{
		ptr16 = (int16_t *)(currSmp->pek);
		for (i = x1; i < x2; ++i)
		{
			dSmp = (ptr16[i] * (vol_StartVol + (((vol_EndVol - vol_StartVol) * (i - x1)) / (double)(len)))) / 100.0;

			if (cpu.hasSSE2)
				sse2_double2int32_round(smp, dSmp);
			else
				smp = (int32_t)(round(dSmp));

			CLAMP16(smp);
			ptr16[i] = (int16_t)(smp);
		}
	}
	else
	{
		ptr8 = currSmp->pek;
		for (i = x1; i < x2; ++i)
		{
			dSmp = (ptr8[i] * (vol_StartVol + (((vol_EndVol - vol_StartVol) * (i - x1)) / (double)(len)))) / 100.0;

			if (cpu.hasSSE2)
				sse2_double2int32_round(smp, dSmp);
			else
				smp = (int32_t)(round(dSmp));

			CLAMP8(smp);
			ptr8[i] = (int8_t)(smp);
		}
	}
	fixSample(currSmp);

	resumeAudio();

	setSongModifiedFlag();
	setMouseBusy(false);

	editor.ui.sysReqShown = false;
	return (true);
}

static void pbApplyVolume(void)
{
	// test if we actually need to do anything
	if ((vol_StartVol == 100) && (vol_EndVol == 100))
	{
		editor.ui.sysReqShown = false;
		return;
	}

	mouseAnimOn();
	thread = SDL_CreateThread(applyVolumeThread, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static int32_t SDLCALL getMaxScaleThread(void *ptr)
{
	int8_t *ptr8;
	int16_t *ptr16;
	int32_t vol, absSmp, x1, x2, len, i, maxAmp;

	(void)(ptr);

	if (smpEd_Rx1 < smpEd_Rx2)
	{
		x1 = smpEd_Rx1;
		x2 = smpEd_Rx2;

		if (x2 > currSmp->len)
			x2 = currSmp->len;

		if (x1 < 0)
			x1 = 0;

		if (x2 <= x1)
		{
			setMouseBusy(false);
			return (true);
		}

		if (currSmp->typ & 16)
		{
			x1 &= 0xFFFFFFFE;
			x2 &= 0xFFFFFFFE;
		}
	}
	else
	{
		// no sample marking, operate on the whole sample
		x1 = 0;
		x2 = currSmp->len;
	}

	len = x2 - x1;
	if (currSmp->typ & 16)
		len /= 2;

	restoreSample(currSmp);

	maxAmp = 0;
	if (currSmp->typ & 16)
	{
		ptr16 = (int16_t *)(&currSmp->pek[x1]);
		for (i = 0; i < len; ++i)
		{
			absSmp = ABS(ptr16[i]);
			if (absSmp > maxAmp)
				maxAmp = absSmp;
		}
	}
	else
	{
		ptr8 = &currSmp->pek[x1];
		for (i = 0; i < len; ++i)
		{
			absSmp = ABS(ptr8[i]);
			if (absSmp > maxAmp)
				maxAmp = absSmp;
		}

		maxAmp <<= 8;
	}

	fixSample(currSmp);

	if (maxAmp <= 0)
	{
		vol_StartVol = 0;
		vol_EndVol   = 0;
	}
	else
	{
		vol = (100 * 32768) / maxAmp;
		if (vol > 200)
			vol = 200;

		vol_StartVol = (int16_t)(vol);
		vol_EndVol   = (int16_t)(vol);
	}

	setMouseBusy(false);

	return (true);
}

static void pbGetMaxScale(void)
{
	mouseAnimOn();
	thread = SDL_CreateThread(getMaxScaleThread, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static void drawSampleVolumeBox(void)
{
	char sign;
	const int16_t x = 166;
	const int16_t y = 230;
	const int16_t w = 301;
	const int16_t h = 52;
	uint16_t val;

	// main fill
	fillRect(x + 1, y + 1, w - 2, h - 2, PAL_BUTTONS);

	// outer border
	vLine(x,         y,         h - 1, PAL_BUTTON1);
	hLine(x + 1,     y,         w - 2, PAL_BUTTON1);
	vLine(x + w - 1, y,         h,     PAL_BUTTON2);
	hLine(x,         y + h - 1, w - 1, PAL_BUTTON2);

	// inner border
	vLine(x + 2,     y + 2,     h - 5, PAL_BUTTON2);
	hLine(x + 3,     y + 2,     w - 6, PAL_BUTTON2);
	vLine(x + w - 3, y + 2,     h - 4, PAL_BUTTON1);
	hLine(x + 2,     y + h - 3, w - 4, PAL_BUTTON1);

	textOutShadow(172, 236, PAL_FORGRND, PAL_BUTTON2, "Start volume");
	textOutShadow(172, 249, PAL_FORGRND, PAL_BUTTON2, "End volume");
	charOutShadow(282, 236, PAL_FORGRND, PAL_BUTTON2, '%');
	charOutShadow(282, 250, PAL_FORGRND, PAL_BUTTON2, '%');

		 if (vol_StartVol == 0) sign = ' ';
	else if (vol_StartVol  < 0) sign = '-';
	else                        sign = '+';

	val = ABS(vol_StartVol);
	if (val > 99)
	{
		charOut(253, 236, PAL_FORGRND, sign);
		charOut(260, 236, PAL_FORGRND, '0' + ((val / 100) % 10));
		charOut(267, 236, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(274, 236, PAL_FORGRND, '0' + (val % 10));
	}
	else if (val > 9)
	{
		charOut(260, 236, PAL_FORGRND, sign);
		charOut(267, 236, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(274, 236, PAL_FORGRND, '0' + (val % 10));
	}
	else
	{
		charOut(267, 236, PAL_FORGRND, sign);
		charOut(274, 236, PAL_FORGRND, '0' + (val % 10));
	}

		 if (vol_EndVol == 0) sign = ' ';
	else if (vol_EndVol  < 0) sign = '-';
	else                      sign = '+';

	val = ABS(vol_EndVol);
	if (val > 99)
	{
		charOut(253, 250, PAL_FORGRND, sign);
		charOut(260, 250, PAL_FORGRND, '0' + ((val / 100) % 10));
		charOut(267, 250, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(274, 250, PAL_FORGRND, '0' + (val % 10));
	}
	else if (val > 9)
	{
		charOut(260, 250, PAL_FORGRND, sign);
		charOut(267, 250, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(274, 250, PAL_FORGRND, '0' + (val % 10));
	}
	else
	{
		charOut(267, 250, PAL_FORGRND, sign);
		charOut(274, 250, PAL_FORGRND, '0' + (val % 10));
	}
}

static void setupVolumeBoxWidgets(void)
{
	pushButton_t *p;
	scrollBar_t *s;

	// "Apply" pushbutton
	p = &pushButtons[0];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Apply";
	p->x = 171;
	p->y = 262;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbApplyVolume;
	p->visible = true;

	// "Get maximum scale" pushbutton
	p = &pushButtons[1];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Get maximum scale";
	p->x = 245;
	p->y = 262;
	p->w = 143;
	p->h = 16;
	p->callbackFuncOnUp = pbGetMaxScale;
	p->visible = true;

	// "Exit" pushbutton
	p = &pushButtons[2];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = "Exit";
	p->x = 389;
	p->y = 262;
	p->w = 73;
	p->h = 16;
	p->callbackFuncOnUp = pbExit;
	p->visible = true;

	// scrollbar buttons

	p = &pushButtons[3];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 292;
	p->y = 234;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbSampStartVolDown;
	p->visible = true;

	p = &pushButtons[4];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 439;
	p->y = 234;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbSampStartVolUp;
	p->visible = true;

	p = &pushButtons[5];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_LEFT_STRING;
	p->x = 292;
	p->y = 248;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbSampEndVolDown;
	p->visible = true;

	p = &pushButtons[6];
	memset(p, 0, sizeof (pushButton_t));
	p->caption = ARROW_RIGHT_STRING;
	p->x = 439;
	p->y = 248;
	p->w = 23;
	p->h = 13;
	p->preDelay = 1;
	p->delayFrames = 3;
	p->callbackFuncOnDown = pbSampEndVolUp;
	p->visible = true;

	// volume start scrollbar
	s = &scrollBars[0];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 315;
	s->y = 234;
	s->w = 124;
	s->h = 13;
	s->callbackFunc = sbSetStartVolPos;
	s->visible = true;
	setScrollBarPageLength(0, 1);
	setScrollBarEnd(0, 200 * 2);
	setScrollBarPos(0, 200, false);

	// volume end scrollbar
	s = &scrollBars[1];
	memset(s, 0, sizeof (scrollBar_t));
	s->x = 315;
	s->y = 248;
	s->w = 124;
	s->h = 13;
	s->callbackFunc = sbSetEndVolPos;
	s->visible = true;
	setScrollBarPageLength(1, 1);
	setScrollBarEnd(1, 200 * 2);
	setScrollBarPos(1, 200, false);
}

void pbSampleVolume(void)
{
	uint16_t i;

	if ((editor.curInstr == 0) || (currSmp->pek == NULL))
		return;

	setupVolumeBoxWidgets();
	windowOpen();

	exitFlag = false;
	while (editor.ui.sysReqShown)
	{
		readInput();
		setSyncedReplayerVars();
		handleRedrawing();

		// this is needed for the "Get maximum scale" button
		if (editor.ui.setMouseIdle) mouseAnimOff();

		drawSampleVolumeBox();
		setScrollBarPos(0, 200 + vol_StartVol, false);
		setScrollBarPos(1, 200 + vol_EndVol,   false);
		for (i = 0; i < 7; ++i) drawPushButton(i);
		for (i = 0; i < 2; ++i) drawScrollBar(i);

		flipFrame();
	}

	for (i = 0; i < 7; ++i) hidePushButton(i);
	for (i = 0; i < 2; ++i) hideScrollBar(i);

	windowClose(true);
}
