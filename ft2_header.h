#ifndef __FT2_HEADER_H
#define __FT2_HEADER_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <emmintrin.h> // for intrinsics
#else
#include <limits.h> // PATH_MAX
#endif
#include "ft2_replayer.h"

#define BETA_VERSION 142

// do NOT change these! It will only mess things up...
#define VBLANK_HZ 60
#define SCREEN_W 632
#define SCREEN_H 400

#ifndef _WIN32
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define DIR_DELIMITER '/'
#else
#define DIR_DELIMITER '\\'
#define PATH_MAX 260
#endif

#define SGN(x) (((x) >= 0) ? 1 : -1)
#define ABS(a) (((a) < 0) ? -(a) : (a))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// fast 32-bit -> 8-bit clamp
#define CLAMP8(i) if ((int8_t)(i) != i) i = 0x7F ^ (i >> 31)

// fast 32-bit -> 16-bit clamp
#define CLAMP16(i) if ((int16_t)(i) != i) i = 0x7FFF ^ (i >> 31)

#define SWAP16(value) \
( \
	(((uint16_t)((value) & 0x00FF)) << 8) | \
	(((uint16_t)((value) & 0xFF00)) >> 8)   \
)

#define SWAP32(value) \
( \
	(((uint32_t)((value) & 0x000000FF)) << 24) | \
	(((uint32_t)((value) & 0x0000FF00)) <<  8) | \
	(((uint32_t)((value) & 0x00FF0000)) >>  8) | \
	(((uint32_t)((value) & 0xFF000000)) >> 24)   \
)

// round and convert double/float to int32_t
#if defined __APPLE__ || defined _WIN32 || defined __i386__ || defined __amd64__
#define sse2_double2int32_round(i, d) (i = _mm_cvtsd_si32(_mm_load_sd(&d)))
#define sse2_double2int32_trunc(i, d) (i = _mm_cvttsd_si32(_mm_load_sd(&d)))
#define sse_float2int32_round(i, f)   (i = _mm_cvt_ss2si(_mm_load_ss(&f)))
#define sse_float2int32_trunc(i, f)   (i = _mm_cvtt_ss2si(_mm_load_ss(&f)))
#else
#define sse2_double2int32_round(i, d) i = 0; (void)(d);
#define sse2_double2int32_trunc(i, d) i = 0; (void)(d);
#define sse_float2int32_round(i, f)   i = 0; (void)(f);
#define sse_float2int32_trunc(i, f)   i = 0; (void)(f);
#endif

struct cpu_t
{
	bool hasSSE, hasSSE2;
} cpu;

struct editor_t
{
	struct ui_t
	{
		volatile bool setMouseBusy, setMouseIdle;
		char fullscreenButtonText[24];
		int8_t buttonContrast, desktopContrast;

		// all screens
		bool extended, sysReqShown;

		// top screens
		bool instrSwitcherShown, aboutScreenShown, helpScreenShown, configScreenShown;
		bool scopesShown, diskOpShown, nibblesShown, transposeShown, instEditorExtShown;
		bool sampleEditorExtShown, advEditShown, wavRendererShown, trimScreenShown;
		bool drawBPMFlag, drawSpeedFlag, drawGlobVolFlag, drawPosEdFlag, drawPattNumLenFlag;
		bool updatePosSections;
		uint8_t oldTopLeftScreen;

		// bottom screens
		bool patternEditorShown, instEditorShown, sampleEditorShown, pattChanScrollShown;
		bool leftLoopPinMoving, rightLoopPinMoving;
		bool drawReplayerPianoFlag, drawPianoFlag, updatePatternEditor;
		uint8_t channelOffset, numChannelsShown, maxVisibleChannels;
		uint16_t patternChannelWidth;
		int32_t sampleDataOrLoopDrag;

		// backup flag for when entering/exiting extended pattern editor (TODO: this is lame and shouldn't be hardcoded)
		bool _aboutScreenShown, _helpScreenShown, _configScreenShown, _diskOpShown;
		bool _nibblesShown, _transposeShown, _instEditorShown;
		bool _instEditorExtShown, _sampleEditorExtShown, _patternEditorShown;
		bool _sampleEditorShown, _advEditShown, _wavRendererShown, _trimScreenShown;
		// -------------------------------------------------------------------------
	} ui;

	struct cursor_t
	{
		uint8_t ch;
		int8_t object;
	} cursor;

	UNICHAR *tmpFilenameU, *tmpInstrFilenameU; // used by saving/loading threads
	UNICHAR *configFileLocation, *audioDevConfigFileLocation, *midiConfigFileLocation;

	volatile bool busy, scopeThreadMutex, programRunning, wavIsRendering, wavReachedEndFlag;
	volatile bool updateCurSmp, updateCurInstr, diskOpReadDir, diskOpReadDone, updateWindowTitle;
	volatile uint8_t loadMusicEvent;
	volatile FILE *wavRendererFileHandle;

	bool autoPlayOnDrop, trimThreadWasDone, throwExit, editTextFlag;
	bool copyMaskEnable, diskOpReadOnOpen, samplingAudioFlag;
	bool instrBankSwapped, channelMute[MAX_VOICES], NI_Play;

	uint8_t currPanEnvPoint, currVolEnvPoint, currPaletteEdit;
	uint8_t copyMask[5], pasteMask[5], transpMask[5], smpEd_NoteNr, instrBankOffset, sampleBankOffset;
	uint8_t srcInstr, curInstr, srcSmp, curSmp, currHelpScreen, currConfigScreen, textCursorBlinkCounter;
	uint8_t keyOnTab[MAX_VOICES], ID_Add, curOctave, curSmpChannel;
	uint8_t sampleSaveMode, moduleSaveMode, ptnJumpPos[4];
	int16_t globalVol, songPos, pattPos;
	uint16_t tmpPattern, editPattern, speed, tempo, timer, ptnCursorY;
	int32_t samplePlayOffset, keyOffNr, keyOffTime[MAX_VOICES];
	uint32_t framesPassed, *currPaletteEntry, wavRendererTime;
	double dPerfFreq, dPerfFreqMulMicro;
} editor;

#endif
