#ifndef __FT2_CONFIG_H
#define __FT2_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "ft2_replayer.h"

#define CFG_ID_STR "FastTracker 2.0 configuration file\x1A"
#define CFG_VER 0x0101
#define CONFIG_FILE_SIZE 1736

enum
{
	CONFIG_SCREEN_IO_DEVICES,
	CONFIG_SCREEN_LAYOUT,
	CONFIG_SCREEN_MISCELLANEOUS,
	CONFIG_SCREEN_MIDI_INPUT,

	CONFIG_HIDE_ERRORS = 0,
	CONFIG_SHOW_ERRORS = 1,

	MOUSE_IDLE_SHAPE_NICE    = 0,
	MOUSE_IDLE_SHAPE_UGLY    = 1,
	MOUSE_IDLE_SHAPE_AWFUL   = 2,
	MOUSE_IDLE_SHAPE_USEABLE = 3,
	MOUSE_IDLE_TEXT_EDIT     = 4,

	MOUSE_BUSY_SHAPE_CLOCK = 0,
	MOUSE_BUSY_SHAPE_GLASS = 2,

	MAX_CHANS_SHOWN_4  = 0,
	MAX_CHANS_SHOWN_6  = 1,
	MAX_CHANS_SHOWN_8  = 2,
	MAX_CHANS_SHOWN_12 = 3,

	PATT_FONT_CAPITALS   = 0,
	PATT_FONT_LOWERCASE  = 1,
	PATT_FONT_FUTURE     = 2,
	PATT_FONT_BOLD       = 3,

	PAL_ARCTIC           = 0,
	PAL_AURORA_BOREALIS  = 1,
	PAL_BLUES            = 2,
	PAL_GOLD             = 3,
	PAL_HEAVY_METAL      = 4,
	PAL_JUNGLE           = 5,
	PAL_LITHE_DARK       = 6,
	PAL_ROSE             = 7,
	PAL_SPACE_PIGS       = 8,
	PAL_VIOLENT          = 9,
	PAL_WHY_COLORS       = 10, // default
	PAL_USER_DEFINED     = 11,

	FILESORT_EXT  = 0,
	FILESORT_NAME = 1,

	ONE_PLAYER  = 0,
	TWO_PLAYERS = 1,

	DIFFICULTY_NOVICE  = 0,
	DIFFICULTY_AVERAGE = 1,
	DIFFICULTY_PRO     = 2,
	DIFFICULTY_MANIAC  = 3,

	DONT_SHOW_S3M_LOAD_WARNING_FLAG        = 64,
	DONT_SHOW_NOT_YET_APPLIED_WARNING_FLAG = 32,

	NO_VOLRAMP_FLAG  = 1,
	BITDEPTH_16      = 2,
	BITDEPTH_24      = 4,
	BUFFSIZE_512     = 8,
	BUFFSIZE_1024    = 16,
	BUFFSIZE_2048    = 32,
	BUFFSIZE_4096    = 64,
	LINED_SCOPES     = 128,

	WINSIZE_AUTO     = 1,
	WINSIZE_1X       = 2,
	WINSIZE_2X       = 4,
	WINSIZE_3X       = 8,
	WINSIZE_4X       = 16,
	FILTERING        = 32,
	FORCE_VSYNC_OFF  = 64,
	START_IN_FULLSCR = 128,
};

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#endif
typedef struct highScoreType_t
{
	char name[1 + 22];
	int32_t score;
	uint8_t level;
}
#ifdef __GNUC__
__attribute__ ((packed))
#endif
highScoreType;

struct config_t // exact FT2.CFG layout (with some modifications)
{
	char cfgID[35];
	uint16_t ver;
	uint32_t audioFreq; // was "BIOSSum" (never used in FT2)
	int16_t utEnhet;
	int16_t masterVol;
	int16_t inputVol;
	int16_t inputDev;
	uint8_t interpolation;
	uint8_t internMode;
	uint8_t stereoMode;
	uint8_t audioDither;         // was lo-byte of "sample16Bit" (used for sampling)
	uint8_t dontShowAgainFlags;  // was hi-byte of "sample16Bit" (used for sampling)
	int16_t inEnhet;
	int16_t sbPort, sbDMA, sbHiDMA, sbInt, sbOutFilter;
	uint8_t true16Bit;
	uint8_t ptnUnpressed, ptnHex, ptnInstrZero, ptnFrmWrk, ptnLineLight, ptnS3M, ptnChnNumbers;
	int16_t ptnLineLightStep, ptnFont, ptnAcc;
	uint8_t palBackgroundR, palBackgroundG, palBackgroundB;
	uint8_t palPattTextR, palPattTextG, palPattTextB;
	uint8_t palBlockMarkR, palBlockMarkG, palBlockMarkB;
	uint8_t palTextOnBlockR, palTextOnBlockG, palTextOnBlockB;
	uint8_t palDesktopR, palDesktopG, palDesktopB;
	uint8_t palForegroundR, palForegroundG, palForegroundB;
	uint8_t palButtonsR, palButtonsG, palButtonsB;
	uint8_t palButtonTextR, palButtonTextG, palButtonTextB;
	uint8_t palDesktop2R, palDesktop2G, palDesktop2B;
	uint8_t palDesktop1R, palDesktop1G, palDesktop1B;
	uint8_t palButtons2R, palButtons2G, palButtons2B;
	uint8_t palButtons1R, palButtons1G, palButtons1B;
	uint8_t palMouseR, palMouseG, palMouseB;
	uint8_t palUnused1R, palUnused1G, palUnused1B;
	uint8_t palUnused2R, palUnused2G, palUnused2B;
	uint8_t palUnused3R, palUnused3G, palUnused3B;
	uint16_t comMacro[10], volMacro[10];
	uint8_t multiRec, multiKeyJazz, multiEdit;
	uint8_t multiRecChn[MAX_VOICES];
	uint8_t recRelease, recQuant;
	int16_t recQuantRes;
	uint8_t recTrueInsert;
	int16_t recMIDIChn;
	uint8_t recMIDIAllChn;
	uint8_t recMIDITransp;
	int16_t recMIDITranspVal;
	uint8_t recMIDIVelosity;
	uint8_t recMIDIAftert;
	int16_t recMIDIVolSens;
	uint8_t recMIDIAllowPC;
	uint8_t smpCutToBuffer, ptnCutToBuffer;
	uint8_t killNotesOnStopPlay;
	uint8_t specialFlags, windowFlags; // was "int16_t PtnDefaultLen" (never used in FT2)
	char modulesPath[1+80], instrPath[1+80], samplesPath[1+80], patternsPath[1+80], tracksPath[1+80];
	uint8_t id_FastLogo, id_TritonProd;
	int16_t cfg_StdPalNr;
	uint8_t cfg_AutoSave;
	int16_t smpEd_SampleNote;
	highScoreType NI_HighScore[10];
	int16_t NI_AntPlayers, NI_Speed;
	uint8_t NI_Surround, NI_Grid, NI_Wrap;
	int32_t NI_HighScoreChecksum;
	int16_t mouseType, mouseAnimType, mouseSpeed, keyLayout;
	int16_t boostLevel;
	int16_t stdEnvP[6][2][12][2];
	uint16_t stdVolEnvAnt[6];
	uint16_t stdVolEnvSust[6];
	uint16_t stdVolEnvRepS[6];
	uint16_t stdVolEnvRepE[6];
	uint16_t stdPanEnvAnt[6];
	uint16_t stdPanEnvSust[6];
	uint16_t stdPanEnvRepS[6];
	uint16_t stdPanEnvRepE[6];
	uint16_t stdFadeOut[6];
	uint16_t stdVibRate[6];
	uint16_t stdVibDepth[6];
	uint16_t stdVibSweep[6];
	uint16_t stdVibTyp[6];
	uint16_t stdVolEnvTyp[6];
	uint16_t stdPanEnvTyp[6];
	int16_t antStars;
	int16_t ptnMaxChannels;
	uint16_t sampleRates[16];
	uint8_t cfg_OverwriteWarning;
	int16_t cfg_SortPriority;
	int16_t cfg_DPMIMemLimit;
	uint8_t cfg_DPMIMemLimitEnabled;
	uint8_t cdr_Sync;
}
#ifdef __GNUC__
__attribute__ ((packed))
#endif
config;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

void resetConfig(void);
bool loadConfig(bool showErrorFlag);
void loadConfig2(void); // called by "Load config" button
bool saveConfig(bool showErrorFlag);
void saveConfig2(void); // called by "Save config" button
void loadConfigOrSetDefaults(void);
void showConfigScreen(void);
void hideConfigScreen(void);
void exitConfigScreen(void);
void setConfigIORadioButtonStates(void);
void configToggleS3MLoadWarning(void);
void configToggleNotYetAppliedWarning(void);
void drawAudioOutputList(void);
void drawAudioInputList(void);
void configAmpDown(void);
void configAmpUp(void);
void configMasterVolDown(void);
void configMasterVolUp(void);
void configPalRDown(void);
void configPalRUp(void);
void configPalGDown(void);
void configPalGUp(void);
void configPalBDown(void);
void configPalBUp(void);
void configPalContDown(void);
void configPalContUp(void);
void configQuantizeUp(void);
void configQuantizeDown(void);
void configMIDIChnUp(void);
void configMIDIChnDown(void);
void configMIDITransUp(void);
void configMIDITransDown(void);
void configMIDISensDown(void);
void configMIDISensUp(void);
void rbConfigIODevices(void);
void rbConfigLayout(void);
void rbConfigMiscellaneous(void);
void rbConfigMidiInput(void);
void rbConfigSbs512(void);
void rbConfigSbs1024(void);
void rbConfigSbs2048(void);
void rbConfigSbs4096(void);
void rbConfigAudio16bit(void);
void rbConfigAudio24bit(void);
void rbConfigAudio44kHz(void);
void rbConfigAudio48kHz(void);
void rbConfigAudio96kHz(void);
void rbConfigFreqTableAmiga(void);
void rbConfigFreqTableLinear(void);
void rbConfigMouseNice(void);
void rbConfigMouseUgly(void);
void rbConfigMouseAwful(void);
void rbConfigMouseUseable(void);
void rbConfigScopeOriginal(void);
void rbConfigMouseBusyVogue(void);
void rbConfigMouseBusyMrH(void);
void rbConfigScopeLined(void);
void rbConfigPatt4Chans(void);
void rbConfigPatt6Chans(void);
void rbConfigPatt8Chans(void);
void rbConfigPatt12Chans(void);
void rbConfigFontCapitals(void);
void rbConfigFontLowerCase(void);
void rbConfigFontFuture(void);
void rbConfigFontBold(void);
void rbConfigPalPatternText(void);
void rbConfigPalBlockMark(void);
void rbConfigPalTextOnBlock(void);
void rbConfigPalMouse(void);
void rbConfigPalDesktop(void);
void rbConfigPalButttons(void);
void rbConfigPalArctic(void);
void rbConfigPalLitheDark(void);
void rbConfigPalAuroraBorealis(void);
void rbConfigPalRose(void);
void rbConfigPalBlues(void);
void rbConfigPalSpacePigs(void);
void rbConfigPalGold(void);
void rbConfigPalViolent(void);
void rbConfigPalHeavyMetal(void);
void rbConfigPalWhyColors(void);
void rbConfigPalJungle(void);
void rbConfigPalUserDefined(void);
void rbFileSortExt(void);
void rbFileSortName(void);
void rbWinSizeAuto(void);
void rbWinSize1x(void);
void rbWinSize2x(void);
void rbWinSize3x(void);
void rbWinSize4x(void);
void cbToggleAutoSaveConfig(void);
void cbConfigInterpolation(void);
void cbConfigVolRamp(void);
void cbConfigDither(void);
void cbConfigPattStretch(void);
void cbConfigHexCount(void);
void cbConfigAccidential(void);
void cbConfigShowZeroes(void);
void cbConfigFramework(void);
void cbConfigLineColors(void);
void cbConfigChanNums(void);
void cbConfigShowVolCol(void);
void cbSampCutToBuff(void);
void cbPattCutToBuff(void);
void cbKillNotesAtStop(void);
void cbFileOverwriteWarn(void);
void cbMultiChanRec(void);
void cbMultiChanKeyJazz(void);
void cbMultiChanEdit(void);
void cbRecKeyOff(void);
void cbQuantisize(void);
void cbChangePattLenInsDel(void);
void cbMIDIAllowPC(void);
void cbMIDIEnable(void);
void cbMIDIRecTransp(void);
void cbMIDIRecAllChn(void);
void cbMIDIRecVelosity(void);
void cbMIDIRecAftert(void);
void cbVsyncOff(void);
void cbFullScreen(void);
void cbPixelFilter(void);
void sbAmp(uint32_t pos);
void sbMasterVol(uint32_t pos);
void sbPalRPos(uint32_t pos);
void sbPalGPos(uint32_t pos);
void sbPalBPos(uint32_t pos);
void sbPalContrastPos(uint32_t pos);
void sbMIDISens(uint32_t pos);

#endif
