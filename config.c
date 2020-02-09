// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#define _WIN32_IE 0x0500
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "header.h"
#include "video.h"
#include "audio.h"
#include "config.h"
#include "gui.h"
#include "pattern_ed.h"
#include "mouse.h"
#include "wav_renderer.h"
#include "audioselector.h"
#include "midi.h"
#include "gfxdata.h"

// hide POSIX warnings
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

static uint8_t configBuffer[CONFIG_FILE_SIZE];

// defined at bottom of this file
extern const uint8_t defaultConfigBuffer[CONFIG_FILE_SIZE];

static void xorConfigBuffer(uint8_t *ptr8)
{
	int32_t i;

	for (i = 0; i < CONFIG_FILE_SIZE; ++i)
		ptr8[i] ^= (uint8_t)(i * 7);
}

static int32_t calcChecksum(uint8_t *p, uint16_t len) // for nibbles highscore data
{
	uint16_t data;
	uint32_t checksum;

	if (len == 0)
		return (0);

	data = 0;
	checksum = 0;

	do
	{
		data = ((data | *p++) + len) ^ len;
		checksum += data;
		data <<= 8;
	}
	while (--len != 0);

	return (checksum);
}

static void loadConfigFromBuffer(uint8_t defaults)
{
	uint8_t *ptr8;
	int32_t i, checksum;

	lockMixerCallback();

	memcpy(&config, configBuffer, CONFIG_FILE_SIZE);

#ifdef __APPLE__
	if (defaults)
		config.audioFreq = 44100;
#else
	(void)(defaults); // prevent warning
#endif

	// if Nibbles highscore table checksum is incorrect, load default highscore table instead
	checksum = calcChecksum((uint8_t *)(&config.NI_HighScore), sizeof (config.NI_HighScore));
	if (config.NI_HighScoreChecksum != checksum)
		memcpy(&config.NI_HighScore, &defaultConfigBuffer[636], sizeof (config.NI_HighScore));

	// clamp palette entries to 0..64

	ptr8 = (uint8_t *)(&config.palBackgroundR);
	for (i = 0; i < (16 * 3); ++i)
	{
		if (ptr8[i] > 64)
			ptr8[i] = 64;
	}

	// sanitize certain values

	config.modulesPath[80 - 1]  = '\0';
	config.instrPath[80 - 1]    = '\0';
	config.samplesPath[80 - 1]  = '\0';
	config.patternsPath[80 - 1] = '\0';
	config.tracksPath[80 - 1]   = '\0';

	config.boostLevel       = CLAMP(config.boostLevel,       1,  32);
	config.masterVol        = CLAMP(config.masterVol,        0, 256);
	config.ptnMaxChannels   = CLAMP(config.ptnMaxChannels,   0,   3);
	config.ptnFont          = CLAMP(config.ptnFont,          0,   3);
	config.mouseType        = CLAMP(config.mouseType,        0,   3);
	config.cfg_StdPalNr     = CLAMP(config.cfg_StdPalNr,     0,  11);
	config.cfg_SortPriority = CLAMP(config.cfg_SortPriority, 0,   1);
	config.NI_AntPlayers    = CLAMP(config.NI_AntPlayers,    0,   1);
	config.NI_Speed         = CLAMP(config.NI_Speed,         0,   3);
	config.recMIDIVolSens   = CLAMP(config.recMIDIVolSens,   0, 200);
	config.recMIDIChn       = CLAMP(config.recMIDIChn,       1,  16);

	if (config.recTrueInsert > 1)
		config.recTrueInsert = 1;

	if ((config.mouseAnimType != 0) && (config.mouseAnimType != 2))
		config.mouseAnimType = 0;

	if ((config.recQuantRes != 1) && (config.recQuantRes != 2) && (config.recQuantRes != 4) &&
		(config.recQuantRes != 8) && (config.recQuantRes != 16)
	   )
	{
		config.recQuantRes = 16;
	}

	if ((config.audioFreq != 44100) && (config.audioFreq != 48000) && (config.audioFreq != 96000))
	{
		// set default
#ifdef __APPLE__
		config.audioFreq = 44100;
#else
		config.audioFreq = 48000;
#endif
	}

	if (config.specialFlags == 64) // default value from FT2 (this was ptnDefaultLen byte #1) - set defaults
		config.specialFlags = BUFFSIZE_1024 | BITDEPTH_16;

	if (config.windowFlags == 0) // default value from FT2 (this was ptnDefaultLen byte #2) - set defaults
		config.windowFlags = WINSIZE_AUTO;

	// audio bit depth - remove 24-bit flag if both are enabled
	if ((config.specialFlags & BITDEPTH_16) && (config.specialFlags & BITDEPTH_24))
		config.specialFlags &= ~BITDEPTH_24;

	if (audio.dev != 0)
		setNewAudioSettings();

	// reset temporary contrast settings for custom preset
	video.customPaletteContrasts[0] = 52;
	video.customPaletteContrasts[1] = 57;
	setPalettePreset(config.cfg_StdPalNr);

	audioSetInterpolation(config.interpolation ? true : false);
	audioSetVolRamp((config.specialFlags & NO_VOLRAMP_FLAG) ? false : true);
	setAudioAmp(config.boostLevel, config.masterVol, config.specialFlags & BITDEPTH_24);

	setMouseShape(config.mouseType);
	changeLogoType(config.id_FastLogo);
	changeBadgeType(config.id_TritonProd);

	editor.ui.maxVisibleChannels = (uint8_t)(2 + ((config.ptnMaxChannels + 1) * 2));

	unlockMixerCallback();
}

static uint8_t getCurrentPaletteEntry(void)
{
	switch (editor.currPaletteEdit)
	{
		default:
		case 0: return PAL_PATTEXT;
		case 1: return PAL_BLCKMRK;
		case 2: return PAL_BLCKTXT;
		case 3: return PAL_MOUSEPT;
		case 4: return PAL_DESKTOP;
		case 5: return PAL_BUTTONS;
	}
}

static void drawCurrentPaletteColor(void)
{
	uint8_t palIndex;

	if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_LAYOUT))
	{
		palIndex = getCurrentPaletteEntry();

		textOutShadow(516, 3, PAL_FORGRND, PAL_DSKTOP2, "Palette:");
		hexOutBg(573, 3, PAL_FORGRND, PAL_DESKTOP, video.palette[palIndex], 6);
		clearRect(616, 2, 12, 10);
		fillRect(617, 3, 10, 8, palIndex);
	}
}

static void drawContrastText(void)
{
	char str[10];
	int32_t percent;

	if (config.cfg_StdPalNr == PAL_USER_DEFINED)
	{
		if (editor.currPaletteEdit == 4)
			percent = editor.ui.desktopContrast;
		else
			percent = editor.ui.buttonContrast;

		sprintf(str, "%3d", percent);
		textOutFixed(599, 59, PAL_FORGRND, PAL_DESKTOP, str);
	}
}

static void showPaletteEditor(void)
{
	if (config.cfg_StdPalNr == PAL_USER_DEFINED)
	{
		charOutShadow(503, 17, PAL_FORGRND, PAL_DSKTOP2, 'R');
		charOutShadow(503, 31, PAL_FORGRND, PAL_DSKTOP2, 'G');
		charOutShadow(503, 45, PAL_FORGRND, PAL_DSKTOP2, 'B');

		showScrollBar(SB_PAL_R);
		showScrollBar(SB_PAL_G);
		showScrollBar(SB_PAL_B);
		showPushButton(PB_CONFIG_PAL_R_DOWN);
		showPushButton(PB_CONFIG_PAL_R_UP);
		showPushButton(PB_CONFIG_PAL_G_DOWN);
		showPushButton(PB_CONFIG_PAL_G_UP);
		showPushButton(PB_CONFIG_PAL_B_DOWN);
		showPushButton(PB_CONFIG_PAL_B_UP);

		showRadioButtonGroup(RB_GROUP_CONFIG_PAL_ENTRIES);

		if (editor.currPaletteEdit >= 4)
		{
			textOutShadow(516, 59, PAL_FORGRND, PAL_DSKTOP2, "Contrast:");

			if (editor.currPaletteEdit == 4)
				setScrollBarPos(SB_PAL_CONTRAST, editor.ui.desktopContrast, false);
			else if (editor.currPaletteEdit == 5)
				setScrollBarPos(SB_PAL_CONTRAST, editor.ui.buttonContrast, false);

			showScrollBar(SB_PAL_CONTRAST);
			showPushButton(PB_CONFIG_PAL_CONT_DOWN);
			showPushButton(PB_CONFIG_PAL_CONT_UP);

			charOutShadow(620, 59, PAL_FORGRND, PAL_DSKTOP2, '%');
			drawContrastText();
		}
		else
		{
			// clear contrast stuff
			fillRect(513, 59, 116, 25, PAL_DESKTOP);
		}

		drawCurrentPaletteColor();
	}
	else
	{
		hideScrollBar(SB_PAL_R);
		hideScrollBar(SB_PAL_G);
		hideScrollBar(SB_PAL_B);
		hideScrollBar(SB_PAL_CONTRAST);
		hidePushButton(PB_CONFIG_PAL_R_DOWN);
		hidePushButton(PB_CONFIG_PAL_R_UP);
		hidePushButton(PB_CONFIG_PAL_G_DOWN);
		hidePushButton(PB_CONFIG_PAL_G_UP);
		hidePushButton(PB_CONFIG_PAL_B_DOWN);
		hidePushButton(PB_CONFIG_PAL_B_UP);
		hidePushButton(PB_CONFIG_PAL_CONT_DOWN);
		hidePushButton(PB_CONFIG_PAL_CONT_UP);

		hideRadioButtonGroup(RB_GROUP_CONFIG_PAL_ENTRIES);

		fillRect(398, 2, 232, 82, PAL_DESKTOP);
		textOutShadow(453, 22, PAL_FORGRND, PAL_DSKTOP2, "The palette editor is");
		textOutShadow(431, 33, PAL_FORGRND, PAL_DSKTOP2, "disabled on built-in presets.");
		textOutShadow(427, 44, PAL_FORGRND, PAL_DSKTOP2, "Select \"User defined\" to make");
		textOutShadow(454, 55, PAL_FORGRND, PAL_DSKTOP2, "your own FT2 palette.");
	}
}

static void configDrawAmp(void)
{
	char str[8];

	sprintf(str, "%02d", config.boostLevel);
	textOutFixed(607, 120, PAL_FORGRND, PAL_DESKTOP, str);
}

static void setDefaultConfigSettings(void)
{
	memcpy(configBuffer, defaultConfigBuffer, CONFIG_FILE_SIZE);
	loadConfigFromBuffer(true);
}

void resetConfig(void)
{
	uint8_t oldWindowFlags;

	if (okBox(2, "System request", "Are you sure you want to completely reset your FT2 configuration?") != 1)
		return;

	oldWindowFlags = config.windowFlags;

	setDefaultConfigSettings();
	setToDefaultAudioOutputDevice();
	setToDefaultAudioInputDevice();

	saveConfig(false);

	// redraw new changes
	showTopScreen(false);
	showBottomScreen();

	setWindowSizeFromConfig(true);

	// handle pixel filter change
	if ((oldWindowFlags & FILTERING) != (config.windowFlags & FILTERING))
	{
		recreateTexture();

		if (video.fullscreen)
		{
			leaveFullScreen();
			enterFullscreen();
		}
	}
}

bool loadConfig(bool showErrorFlag)
{
	size_t fileSize;
	FILE *in;

	// this routine can be called at any time, so make sure we free these first...

	if (audio.currOutputDevice != NULL)
	{
		free(audio.currOutputDevice);
		audio.currOutputDevice = NULL;
	}

	if (audio.currInputDevice != NULL)
	{
		free(audio.currInputDevice);
		audio.currInputDevice = NULL;
	}

	// now we can get the audio devices from audiodev.ini

	audio.currOutputDevice = getAudioOutputDeviceFromConfig();
	audio.currInputDevice  = getAudioInputDeviceFromConfig();

	if (midi.initThreadDone)
	{
		setMidiInputDeviceFromConfig();
		if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_MIDI_INPUT))
			drawMidiInputList();
	}

	if (editor.configFileLocation == NULL)
	{
		if (showErrorFlag)
			okBox(0, "System message", "Error opening config file for reading!");

		return (false);
	}

	in = UNICHAR_FOPEN(editor.configFileLocation, "rb");
	if (in == NULL)
	{
		if (showErrorFlag)
			okBox(0, "System message", "Error opening config file for reading!");

		return (false);
	}

	fseek(in, 0, SEEK_END);
	fileSize = (int32_t)(ftell(in));
	rewind(in);

	if (fileSize > CONFIG_FILE_SIZE)
	{
		fclose(in);

		if (showErrorFlag)
			okBox(0, "System message", "Error loading config: the config file is not valid!");

		return (false);
	}

	// not a valid FT2 config file (FT2.CFG filesize varies depending on version)
	if ((fileSize < 1732) || (fileSize > CONFIG_FILE_SIZE))
	{
		fclose(in);

		if (showErrorFlag)
			okBox(0, "System message", "Error loading config: the config file is not valid!");

		return (false);
	}

	if (fileSize < CONFIG_FILE_SIZE)
		memset(configBuffer, 0, CONFIG_FILE_SIZE);

	// read to config buffer and close file handle
	if (fread(configBuffer, fileSize, 1, in) != 1)
	{
		fclose(in);

		if (showErrorFlag)
			okBox(0, "System message", "Error opening config file for reading!");

		return (false);
	}

	fclose(in);

	// decrypt config buffer
	xorConfigBuffer(configBuffer);

	if (memcmp(&configBuffer[0], CFG_ID_STR, 35) != 0)
	{
		if (showErrorFlag)
			okBox(0, "System message", "Error loading config: the config file is not valid!");

		return (false);
	}

	loadConfigFromBuffer(false);
	return (true);
}

void loadConfig2(void) // called by "Load config" button
{
	uint8_t oldWindowFlags;

	oldWindowFlags = config.windowFlags;

	loadConfig(CONFIG_SHOW_ERRORS);

	// redraw new changes
	showTopScreen(false);
	showBottomScreen();

	// handle pixel filter change
	if ((oldWindowFlags & FILTERING) != (config.windowFlags & FILTERING))
	{
		recreateTexture();

		if (video.fullscreen)
		{
			leaveFullScreen();
			enterFullscreen();
		}
	}
}

// used to calculate length of text in config's default paths (Pascal strings)
static char getDefPathLen(char *ptr)
{
	int32_t i;
	
	ptr++;
	for (i = 0; i < 80; ++i)
	{
		if (ptr[i] == '\0')
			break;
	}

	return ((char)(i));
}

bool saveConfig(bool showErrorFlag)
{
	FILE *out;

	if (editor.configFileLocation == NULL)
	{
		if (showErrorFlag)
			okBox(0, "System message", "General I/O error during saving! Is the file in use?");

		return (false);
	}

	saveAudioDevicesToConfig(audio.currOutputDevice, audio.currInputDevice);
	saveMidiInputDeviceToConfig();

	out = UNICHAR_FOPEN(editor.configFileLocation, "wb");
	if (out == NULL)
	{
		if (showErrorFlag)
			okBox(0, "System message", "General I/O error during saving! Is the file in use?");

		return (false);
	}

	config.NI_HighScoreChecksum = calcChecksum((uint8_t *)(config.NI_HighScore), sizeof (config.NI_HighScore));

	// set default path lengths (Pascal strings)
	config.modulesPath[0]  = getDefPathLen(config.modulesPath);
	config.instrPath[0]    = getDefPathLen(config.instrPath);
	config.samplesPath[0]  = getDefPathLen(config.samplesPath);
	config.patternsPath[0] = getDefPathLen(config.patternsPath);
	config.tracksPath[0]   = getDefPathLen(config.tracksPath);

	memcpy(configBuffer, &config, CONFIG_FILE_SIZE);

	// encrypt config buffer
	xorConfigBuffer(configBuffer);

	// write config buffer and close file handle
	if (fwrite(configBuffer, 1, CONFIG_FILE_SIZE, out) != CONFIG_FILE_SIZE)
	{
		fclose(out);

		if (showErrorFlag)
			okBox(0, "System message", "General I/O error during saving! Is the file in use?");

		return (false);
	}

	fclose(out);
	return (true);
}

void saveConfig2(void) // called by "Save config" button
{
	saveConfig(CONFIG_SHOW_ERRORS);
}

static UNICHAR *getFullAudDevConfigPath(void) // kinda hackish
{
	UNICHAR *filePath;
	int32_t ft2ConfPathLen, stringOffset, audiodevDotIniStrLen, ft2DotCfgStrLen;

	if (editor.configFileLocation == NULL)
		return (NULL);

	ft2ConfPathLen = (int32_t)(UNICHAR_STRLEN(editor.configFileLocation));

#ifdef _WIN32
	audiodevDotIniStrLen = (int32_t)(UNICHAR_STRLEN(L"audiodev.ini"));
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN(L"FT2.CFG"));
#else
	audiodevDotIniStrLen = (int32_t)(UNICHAR_STRLEN("audiodev.ini"));
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN("FT2.CFG"));
#endif

	filePath = (UNICHAR *)(calloc(ft2ConfPathLen + audiodevDotIniStrLen + 2, sizeof (UNICHAR)));

	UNICHAR_STRCPY(filePath, editor.configFileLocation);

	stringOffset = ft2ConfPathLen - ft2DotCfgStrLen;
	filePath[stringOffset + 0] = '\0';
	filePath[stringOffset + 1] = '\0';

#ifdef _WIN32
	UNICHAR_STRCAT(filePath, L"audiodev.ini");
#else
	UNICHAR_STRCAT(filePath, "audiodev.ini");
#endif

	return (filePath);
}

static UNICHAR *getFullMidiDevConfigPath(void) // kinda hackish
{
	UNICHAR *filePath;
	int32_t ft2ConfPathLen, stringOffset, mididevDotIniStrLen, ft2DotCfgStrLen;

	if (editor.configFileLocation == NULL)
		return (NULL);

	ft2ConfPathLen = (int32_t)(UNICHAR_STRLEN(editor.configFileLocation));

#ifdef _WIN32
	mididevDotIniStrLen = (int32_t)(UNICHAR_STRLEN(L"mididev.ini"));
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN(L"FT2.CFG"));
#else
	mididevDotIniStrLen = (int32_t)(UNICHAR_STRLEN("mididev.ini"));
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN("FT2.CFG"));
#endif

	filePath = (UNICHAR *)(calloc(ft2ConfPathLen + mididevDotIniStrLen + 2, sizeof (UNICHAR)));

	UNICHAR_STRCPY(filePath, editor.configFileLocation);

	stringOffset = ft2ConfPathLen - ft2DotCfgStrLen;
	filePath[stringOffset + 0] = '\0';
	filePath[stringOffset + 1] = '\0';

#ifdef _WIN32
	UNICHAR_STRCAT(filePath, L"mididev.ini");
#else
	UNICHAR_STRCAT(filePath, "mididev.ini");
#endif

	return (filePath);
}

static void setConfigFileLocation(void) // kinda hackish
{
	int32_t result, ft2DotCfgStrLen;
	FILE *f;

	// Windows
#ifdef _WIN32
	UNICHAR *tmpPath, *oldPath;

	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN(L"FT2.CFG"));

	oldPath = (UNICHAR *)(calloc(PATH_MAX + 8 + 2, sizeof (UNICHAR)));
	tmpPath = (UNICHAR *)(calloc(PATH_MAX + 8 + 2, sizeof (UNICHAR)));
	editor.configFileLocation = (UNICHAR *)(calloc(PATH_MAX + ft2DotCfgStrLen + 2, sizeof (UNICHAR)));

	if ((oldPath == NULL) || (tmpPath == NULL) || (editor.configFileLocation == NULL))
	{
		if (oldPath != NULL) free(oldPath);
		if (tmpPath != NULL) free(tmpPath);
		if (editor.configFileLocation != NULL) free(editor.configFileLocation);

		editor.configFileLocation = NULL;
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	if (GetCurrentDirectoryW(PATH_MAX - ft2DotCfgStrLen - 1, oldPath) == 0)
	{
		free(oldPath);
		free(tmpPath);
		free(editor.configFileLocation);

		editor.configFileLocation = NULL;
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	UNICHAR_STRCPY(editor.configFileLocation, oldPath);
	if ((f = fopen("FT2.CFG", "rb")) == NULL)
	{
		result = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, tmpPath);
		if (result == S_OK)
		{
			if (SetCurrentDirectoryW(tmpPath) != 0)
			{
				result = chdir("FT2 clone");
				if (result != 0)
				{
					_mkdir("FT2 clone");
					result = chdir("FT2 clone");
				}

				if (result == 0)
					GetCurrentDirectoryW(PATH_MAX - ft2DotCfgStrLen - 1, editor.configFileLocation); // we can, set it
			}
		}
	}
	else
	{
		fclose(f);
	}

	free(tmpPath);
	SetCurrentDirectoryW(oldPath);
	free(oldPath);

	UNICHAR_STRCAT(editor.configFileLocation, L"\\FT2.CFG");

	// OS X / macOS
#elif defined __APPLE__
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN("FT2.CFG"));

	editor.configFileLocation = (UNICHAR *)(calloc(PATH_MAX + ft2DotCfgStrLen + 2, sizeof (UNICHAR)));
	if (editor.configFileLocation == NULL)
	{
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	if (getcwd(editor.configFileLocation, PATH_MAX - ft2DotCfgStrLen - 1) == NULL)
	{
		free(editor.configFileLocation);
		editor.configFileLocation = NULL;
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	if ((f = fopen("FT2.CFG", "rb")) == NULL)
	{
		if (chdir(getenv("HOME")) == 0)
		{
			result = chdir("Library/Application Support");
			if (result == 0)
			{
				result = chdir("FT2 clone");
				if (result != 0)
				{
					mkdir("FT2 clone", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
					result = chdir("FT2 clone");
				}

				if (result == 0)
					getcwd(editor.configFileLocation, PATH_MAX - ft2DotCfgStrLen - 1);
			}
		}
	}
	else
	{
		fclose(f);
	}

	strcat(editor.configFileLocation, "/FT2.CFG");

	// Linux etc
#else
	ft2DotCfgStrLen = (int32_t)(UNICHAR_STRLEN("FT2.CFG"));

	editor.configFileLocation = (UNICHAR *)(calloc(PATH_MAX + ft2DotCfgStrLen + 2, sizeof (UNICHAR)));
	if (editor.configFileLocation == NULL)
	{
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	if (getcwd(editor.configFileLocation, PATH_MAX - ft2DotCfgStrLen - 1) == NULL)
	{
		free(editor.configFileLocation);
		editor.configFileLocation = NULL;
		showErrorMsgBox("Error: Couldn't set config file location. You can't load/save the config!");
		return;
	}

	if ((f = fopen("FT2.CFG", "rb")) == NULL)
	{
		if (chdir(getenv("HOME")) == 0)
		{
			result = chdir(".config");
			if (result != 0)
			{
				mkdir(".config", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
				result = chdir(".config");
			}

			if (result == 0)
			{
				result = chdir("FT2 clone");
				if (result != 0)
				{
					mkdir("FT2 clone", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
					result = chdir("FT2 clone");
				}

				if (result == 0)
					getcwd(editor.configFileLocation, PATH_MAX - ft2DotCfgStrLen - 1);
			}
		}
	}
	else
	{
		fclose(f);
	}

	strcat(editor.configFileLocation, "/FT2.CFG");
#endif

	editor.midiConfigFileLocation     = getFullMidiDevConfigPath();
	editor.audioDevConfigFileLocation = getFullAudDevConfigPath();
}

void loadConfigOrSetDefaults(void)
{
	size_t fileSize;
	FILE *in;
    char cwd[256];

    /* paul: store CWD before going to config */
    getcwd(cwd, 256);

	setConfigFileLocation();

	if (editor.configFileLocation == NULL)
	{
		setDefaultConfigSettings();
		return;
	}

	in = UNICHAR_FOPEN(editor.configFileLocation, "rb");
	if (in == NULL)
	{
		setDefaultConfigSettings();
		return;
	}

	fseek(in, 0, SEEK_END);
	fileSize = ftell(in);
	rewind(in);

	// not a valid FT2 config file (FT2.CFG filesize varies depending on version)
	if ((fileSize < 1732) || (fileSize > CONFIG_FILE_SIZE))
	{
		fclose(in);
		setDefaultConfigSettings();
		showErrorMsgBox("Config file was not valid, default settings loaded.");
		return;
	}

	if (fileSize < CONFIG_FILE_SIZE)
		memset(configBuffer, 0, CONFIG_FILE_SIZE);

	if (fread(configBuffer, fileSize, 1, in) != 1)
	{
		fclose(in);
		setDefaultConfigSettings();
		showErrorMsgBox("I/O error while reading config file, default settings loaded.");
		return;
	}

	fclose(in);

	// decrypt config buffer
	xorConfigBuffer(configBuffer);

	if (memcmp(&configBuffer[0], CFG_ID_STR, 35) != 0)
	{
		setDefaultConfigSettings();
		showErrorMsgBox("Config file was not valid, default settings loaded.");
		return;
	}

    /* Paul: flip back to CWD */
    chdir(cwd);
	loadConfigFromBuffer(false);
}

static void updatePaletteSelection(void)
{
	uint8_t r, g, b;
	uint32_t pixel;

	pixel = video.palette[getCurrentPaletteEntry()];

	r = P8_TO_P6(RGB_R(pixel));
	g = P8_TO_P6(RGB_G(pixel));
	b = P8_TO_P6(RGB_B(pixel));

	setScrollBarPos(SB_PAL_R, r, false);
	setScrollBarPos(SB_PAL_G, g, false);
	setScrollBarPos(SB_PAL_B, b, false);

	if (editor.currPaletteEdit == 4)
	{
		editor.ui.desktopContrast = video.customPaletteContrasts[0];
		setScrollBarPos(SB_PAL_CONTRAST, editor.ui.desktopContrast, false);
	}
	else if (editor.currPaletteEdit == 5)
	{
		editor.ui.buttonContrast = video.customPaletteContrasts[1];
		setScrollBarPos(SB_PAL_CONTRAST, editor.ui.buttonContrast, false);
	}
}

static void drawQuantValue(void)
{
	char str[8];

	sprintf(str, "%02d", config.recQuantRes);
	textOutFixed(354, 123, PAL_FORGRND, PAL_DESKTOP, str);
}

static void drawMIDIChanValue(void)
{
	char str[8];

	sprintf(str, "%02d", config.recMIDIChn);
	textOutFixed(578, 109, PAL_FORGRND, PAL_DESKTOP, str);
}

static void drawMIDITransp(void)
{
	char sign;
	int8_t val;

	fillRect(571, 123, 20, 8, PAL_DESKTOP);

	sign = (config.recMIDITranspVal < 0) ? '-' : '+';

	val = (int8_t)(ABS(config.recMIDITranspVal));
	if (val >= 10)
	{
		charOut(571, 123, PAL_FORGRND, sign);
		charOut(578, 123, PAL_FORGRND, '0' + ((val / 10) % 10));
		charOut(585, 123, PAL_FORGRND, '0' + (val % 10));
	}
	else
	{
		if (val > 0)
			charOut(578, 123, PAL_FORGRND, sign);

		charOut(585, 123, PAL_FORGRND, '0' + (val % 10));
	}
}

static void drawMIDISens(void)
{
	char str[8];

	sprintf(str, "%03d", config.recMIDIVolSens);
	textOutFixed(525, 160, PAL_FORGRND, PAL_DESKTOP, str);
}

static void setConfigRadioButtonStates(void)
{
	uint16_t tmpID;

	uncheckRadioButtonGroup(RB_GROUP_CONFIG_SELECT);
	switch (editor.currConfigScreen)
	{
		default:
		case CONFIG_SCREEN_IO_DEVICES:    tmpID = RB_CONFIG_IO_DEVICES;    break;
		case CONFIG_SCREEN_LAYOUT:        tmpID = RB_CONFIG_LAYOUT;        break;
		case CONFIG_SCREEN_MISCELLANEOUS: tmpID = RB_CONFIG_MISCELLANEOUS; break;
		case CONFIG_SCREEN_MIDI_INPUT:    tmpID = RB_CONFIG_MIDI_INPUT;    break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	showRadioButtonGroup(RB_GROUP_CONFIG_SELECT);
}

void setConfigIORadioButtonStates(void) // accessed by other .c files
{
	uint16_t tmpID;

	// AUDIO BUFFER SIZE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_SOUND_BUFF_SIZE);

	tmpID = RB_CONFIG_SBS_1024;
		 if (config.specialFlags & BUFFSIZE_512)  tmpID = RB_CONFIG_SBS_512;
	else if (config.specialFlags & BUFFSIZE_2048) tmpID = RB_CONFIG_SBS_2048;
	else if (config.specialFlags & BUFFSIZE_4096) tmpID = RB_CONFIG_SBS_4096;

	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// AUDIO BIT DEPTH
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_BIT_DEPTH);

	tmpID = RB_CONFIG_AUDIO_16BIT;
	if (config.specialFlags & BITDEPTH_24)
		tmpID = RB_CONFIG_AUDIO_24BIT;

	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// AUDIO FREQUENCY
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_FREQ);
	switch (config.audioFreq)
	{
#ifdef __APPLE__
		default: case 44100: tmpID = RB_CONFIG_AUDIO_44KHZ; break;
				 case 48000: tmpID = RB_CONFIG_AUDIO_48KHZ; break;
#else
				 case 44100: tmpID = RB_CONFIG_AUDIO_44KHZ; break;
		default: case 48000: tmpID = RB_CONFIG_AUDIO_48KHZ; break;
#endif
				 case 96000: tmpID = RB_CONFIG_AUDIO_96KHZ; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// FREQUENCY TABLE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_FREQ_TABLE);
	switch (audio.freqTable)
	{
		default:
		case FREQ_TABLE_LINEAR: tmpID = RB_CONFIG_FREQ_LINEAR; break;
		case FREQ_TABLE_AMIGA:  tmpID = RB_CONFIG_FREQ_AMIGA;  break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// show result

	showRadioButtonGroup(RB_GROUP_CONFIG_SOUND_BUFF_SIZE);
	showRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_BIT_DEPTH);
	showRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_FREQ);
	showRadioButtonGroup(RB_GROUP_CONFIG_FREQ_TABLE);
}

static void setConfigIOCheckButtonStates(void)
{
	checkBoxes[CB_CONF_INTERPOLATION].checked = config.interpolation;
	checkBoxes[CB_CONF_VOL_RAMP].checked      = (config.specialFlags & NO_VOLRAMP_FLAG) ? false : true;
	checkBoxes[CB_CONF_DITHER].checked        = (config.specialFlags & BITDEPTH_24) ? false : config.audioDither;

	showCheckBox(CB_CONF_INTERPOLATION);
	showCheckBox(CB_CONF_VOL_RAMP);
	showCheckBox(CB_CONF_DITHER);
}

static void setConfigLayoutCheckButtonStates(void)
{
	checkBoxes[CB_CONF_PATTSTRETCH].checked = config.ptnUnpressed;
	checkBoxes[CB_CONF_HEXCOUNT].checked    = config.ptnHex;
	checkBoxes[CB_CONF_ACCIDENTAL].checked  = config.ptnAcc ? true : false;
	checkBoxes[CB_CONF_SHOWZEROES].checked  = config.ptnInstrZero;
	checkBoxes[CB_CONF_FRAMEWORK].checked   = config.ptnFrmWrk;
	checkBoxes[CB_CONF_LINECOLORS].checked  = config.ptnLineLight;
	checkBoxes[CB_CONF_CHANNUMS].checked    = config.ptnChnNumbers;
	checkBoxes[CB_CONF_SHOW_VOLCOL].checked = config.ptnS3M;

	showCheckBox(CB_CONF_PATTSTRETCH);
	showCheckBox(CB_CONF_HEXCOUNT);
	showCheckBox(CB_CONF_ACCIDENTAL);
	showCheckBox(CB_CONF_SHOWZEROES);
	showCheckBox(CB_CONF_FRAMEWORK);
	showCheckBox(CB_CONF_LINECOLORS);
	showCheckBox(CB_CONF_CHANNUMS);
	showCheckBox(CB_CONF_SHOW_VOLCOL);
}

static void setConfigLayoutRadioButtonStates(void)
{
	uint16_t tmpID;

	// MOUSE SHAPE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_MOUSE);
	switch (config.mouseType)
	{
		default:
		case MOUSE_IDLE_SHAPE_NICE:    tmpID = RB_CONFIG_MOUSE_NICE;    break;
		case MOUSE_IDLE_SHAPE_UGLY:    tmpID = RB_CONFIG_MOUSE_UGLY;    break;
		case MOUSE_IDLE_SHAPE_AWFUL:   tmpID = RB_CONFIG_MOUSE_AWFUL;   break;
		case MOUSE_IDLE_SHAPE_USEABLE: tmpID = RB_CONFIG_MOUSE_USEABLE; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// MOUSE BUSY SHAPE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_MOUSE_BUSY);
	switch (config.mouseAnimType)
	{
		default:
		case MOUSE_BUSY_SHAPE_CLOCK: tmpID = RB_CONFIG_MOUSE_BUSY_CLOCK; break;
		case MOUSE_BUSY_SHAPE_GLASS: tmpID = RB_CONFIG_MOUSE_BUSY_GLASS; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// SCOPE STYLE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_SCOPE);
	tmpID = RB_CONFIG_SCOPE_NORMAL;
	if (config.specialFlags & LINED_SCOPES) tmpID = RB_CONFIG_SCOPE_LINED;
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	switch (config.mouseType)
	{
		default:
		case MOUSE_IDLE_SHAPE_NICE:    tmpID = RB_CONFIG_MOUSE_NICE;    break;
		case MOUSE_IDLE_SHAPE_UGLY:    tmpID = RB_CONFIG_MOUSE_UGLY;    break;
		case MOUSE_IDLE_SHAPE_AWFUL:   tmpID = RB_CONFIG_MOUSE_AWFUL;   break;
		case MOUSE_IDLE_SHAPE_USEABLE: tmpID = RB_CONFIG_MOUSE_USEABLE; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// MAX VISIBLE CHANNELS
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_PATTERN_CHANS);
	switch (config.ptnMaxChannels)
	{
		default:
		case MAX_CHANS_SHOWN_4:  tmpID = RB_CONFIG_MAXCHAN_4;  break;
		case MAX_CHANS_SHOWN_6:  tmpID = RB_CONFIG_MAXCHAN_6;  break;
		case MAX_CHANS_SHOWN_8:  tmpID = RB_CONFIG_MAXCHAN_8;  break;
		case MAX_CHANS_SHOWN_12: tmpID = RB_CONFIG_MAXCHAN_12; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// PATTERN FONT
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_FONT);
	switch (config.ptnFont)
	{
		default:
		case PATT_FONT_CAPITALS:  tmpID = RB_CONFIG_FONT_CAPITALS;  break;
		case PATT_FONT_LOWERCASE: tmpID = RB_CONFIG_FONT_LOWERCASE; break;
		case PATT_FONT_FUTURE:    tmpID = RB_CONFIG_FONT_FUTURE;    break;
		case PATT_FONT_BOLD:      tmpID = RB_CONFIG_FONT_BOLD;      break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// PALETTE ENTRIES
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_PAL_ENTRIES);

	if (config.cfg_StdPalNr == PAL_USER_DEFINED)
	{
		radioButtons[RB_CONFIG_PAL_PATTERNTEXT + editor.currPaletteEdit].state = RADIOBUTTON_CHECKED;
		showRadioButtonGroup(RB_GROUP_CONFIG_PAL_ENTRIES);
	}

	// PALETTE PRESET
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_PAL_PRESET);
	switch (config.cfg_StdPalNr)
	{
		default:
		case PAL_ARCTIC:          tmpID = RB_CONFIG_PAL_ARCTIC;          break;
		case PAL_AURORA_BOREALIS: tmpID = RB_CONFIG_PAL_AURORA_BOREALIS; break;
		case PAL_BLUES:           tmpID = RB_CONFIG_PAL_BLUES;           break;
		case PAL_GOLD:            tmpID = RB_CONFIG_PAL_GOLD;            break;
		case PAL_HEAVY_METAL:     tmpID = RB_CONFIG_PAL_HEAVY_METAL;     break;
		case PAL_JUNGLE:          tmpID = RB_CONFIG_PAL_JUNGLE;          break;
		case PAL_LITHE_DARK:      tmpID = RB_CONFIG_PAL_LITHE_DARK;      break;
		case PAL_ROSE:            tmpID = RB_CONFIG_PAL_ROSE;            break;
		case PAL_SPACE_PIGS:      tmpID = RB_CONFIG_PAL_SPACE_PIGS;      break;
		case PAL_VIOLENT:         tmpID = RB_CONFIG_PAL_VIOLENT;         break;
		case PAL_WHY_COLORS:      tmpID = RB_CONFIG_PAL_WHY_COLORS;      break;
		case PAL_USER_DEFINED:    tmpID = RB_CONFIG_PAL_USER_DEFINED;    break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// show result

	showRadioButtonGroup(RB_GROUP_CONFIG_MOUSE);
	showRadioButtonGroup(RB_GROUP_CONFIG_MOUSE_BUSY);
	showRadioButtonGroup(RB_GROUP_CONFIG_SCOPE);
	showRadioButtonGroup(RB_GROUP_CONFIG_PATTERN_CHANS);
	showRadioButtonGroup(RB_GROUP_CONFIG_FONT);
	showRadioButtonGroup(RB_GROUP_CONFIG_PAL_PRESET);
}

static void setConfigMiscCheckButtonStates(void)
{
	checkBoxes[CB_CONF_SAMP_CUT_TO_BUF].checked        = config.smpCutToBuffer;
	checkBoxes[CB_CONF_PATT_CUT_TO_BUF].checked        = config.ptnCutToBuffer;
	checkBoxes[CB_CONF_KILL_NOTES_AT_STOP].checked     = config.killNotesOnStopPlay;
	checkBoxes[CB_CONF_FILE_OVERWRITE_WARN].checked    = config.cfg_OverwriteWarning;
	checkBoxes[CB_CONF_MULTICHAN_REC].checked          = config.multiRec;
	checkBoxes[CB_CONF_MULTICHAN_JAZZ].checked         = config.multiKeyJazz;
	checkBoxes[CB_CONF_MULTICHAN_EDIT].checked         = config.multiEdit;
	checkBoxes[CB_CONF_REC_KEYOFF].checked             = config.recRelease;
	checkBoxes[CB_CONF_QUANTIZATION].checked           = config.recQuant;
	checkBoxes[CB_CONF_CHANGE_PATTLEN_INS_DEL].checked = config.recTrueInsert;
	checkBoxes[CB_CONF_MIDI_ALLOW_PC].checked          = config.recMIDIAllowPC;
	checkBoxes[CB_CONF_MIDI_ENABLE].checked            = midi.enable;
	checkBoxes[CB_CONF_MIDI_REC_ALL].checked           = config.recMIDIAllChn;
	checkBoxes[CB_CONF_MIDI_REC_TRANS].checked         = config.recMIDITransp;
	checkBoxes[CB_CONF_MIDI_REC_VELOC].checked         = config.recMIDIVelosity;
	checkBoxes[CB_CONF_MIDI_REC_AFTERTOUCH].checked    = config.recMIDIAftert;
	checkBoxes[CB_CONF_FORCE_VSYNC_OFF].checked        = (config.windowFlags & FORCE_VSYNC_OFF)  ? true : false;
	checkBoxes[CB_CONF_START_IN_FULLSCREEN].checked    = (config.windowFlags & START_IN_FULLSCR) ? true : false;
	checkBoxes[CB_CONF_FILTERING].checked              = (config.windowFlags & FILTERING) ? true : false;

	showCheckBox(CB_CONF_SAMP_CUT_TO_BUF);
	showCheckBox(CB_CONF_PATT_CUT_TO_BUF);
	showCheckBox(CB_CONF_KILL_NOTES_AT_STOP);
	showCheckBox(CB_CONF_FILE_OVERWRITE_WARN);
	showCheckBox(CB_CONF_MULTICHAN_REC);
	showCheckBox(CB_CONF_MULTICHAN_JAZZ);
	showCheckBox(CB_CONF_MULTICHAN_EDIT);
	showCheckBox(CB_CONF_REC_KEYOFF);
	showCheckBox(CB_CONF_QUANTIZATION);
	showCheckBox(CB_CONF_CHANGE_PATTLEN_INS_DEL);
	showCheckBox(CB_CONF_MIDI_ALLOW_PC);
	showCheckBox(CB_CONF_MIDI_ENABLE);
	showCheckBox(CB_CONF_MIDI_REC_ALL);
	showCheckBox(CB_CONF_MIDI_REC_TRANS);
	showCheckBox(CB_CONF_MIDI_REC_VELOC);
	showCheckBox(CB_CONF_MIDI_REC_AFTERTOUCH);
	showCheckBox(CB_CONF_FORCE_VSYNC_OFF);
	showCheckBox(CB_CONF_START_IN_FULLSCREEN);
	showCheckBox(CB_CONF_FILTERING);
}

static void setConfigMiscRadioButtonStates(void)
{
	uint16_t tmpID;

	// FILE SORTING
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_FILESORT);
	switch (config.cfg_SortPriority)
	{
		default:
		case FILESORT_EXT:  tmpID = RB_CONFIG_FILESORT_EXT;  break;
		case FILESORT_NAME: tmpID = RB_CONFIG_FILESORT_NAME; break;
	}
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// WINDOW SIZE
	uncheckRadioButtonGroup(RB_GROUP_CONFIG_WIN_SIZE);

		 if (config.windowFlags & WINSIZE_AUTO) tmpID = RB_CONFIG_WIN_SIZE_AUTO;
	else if (config.windowFlags & WINSIZE_1X)   tmpID = RB_CONFIG_WIN_SIZE_1X;
	else if (config.windowFlags & WINSIZE_2X)   tmpID = RB_CONFIG_WIN_SIZE_2X;
	else if (config.windowFlags & WINSIZE_3X)   tmpID = RB_CONFIG_WIN_SIZE_3X;
	else if (config.windowFlags & WINSIZE_4X)   tmpID = RB_CONFIG_WIN_SIZE_4X;
	radioButtons[tmpID].state = RADIOBUTTON_CHECKED;

	// show result

	showRadioButtonGroup(RB_GROUP_CONFIG_FILESORT);
	showRadioButtonGroup(RB_GROUP_CONFIG_WIN_SIZE);
}

void showConfigScreen(void)
{
	if (editor.ui.extended)
		exitPatternEditorExtended();

	hideTopScreen();
	editor.ui.configScreenShown = true;

	drawFramework(0, 0, 110, 173, FRAMEWORK_TYPE1);

	setConfigRadioButtonStates();

	checkBoxes[CB_CONF_AUTOSAVE].checked = config.cfg_AutoSave;
	showCheckBox(CB_CONF_AUTOSAVE);

	showPushButton(PB_CONFIG_RESET);
	showPushButton(PB_CONFIG_LOAD);
	showPushButton(PB_CONFIG_SAVE);
	showPushButton(PB_CONFIG_EXIT);

	textOutShadow(5,    4, PAL_FORGRND, PAL_DSKTOP2, "Configuration:");
	textOutShadow(22,  20, PAL_FORGRND, PAL_DSKTOP2, "I/O Devices");
	textOutShadow(22,  36, PAL_FORGRND, PAL_DSKTOP2, "Layout");
	textOutShadow(22,  52, PAL_FORGRND, PAL_DSKTOP2, "Miscellaneous");
	textOutShadow(22,  68, PAL_FORGRND, PAL_DSKTOP2, "MIDI input");

	textOutShadow(19,  92, PAL_FORGRND, PAL_DSKTOP2, "Auto save");

	switch (editor.currConfigScreen)
	{
		default:
		case CONFIG_SCREEN_IO_DEVICES:
		{
			drawFramework(110,   0, 276, 87, FRAMEWORK_TYPE1);
			drawFramework(110,  87, 276, 86, FRAMEWORK_TYPE1);

			drawFramework(386,   0, 119,  73, FRAMEWORK_TYPE1);
			drawFramework(386,  73, 119,  44, FRAMEWORK_TYPE1);
			drawFramework(386, 117, 119,  56, FRAMEWORK_TYPE1);

			drawFramework(505,   0, 127,  73, FRAMEWORK_TYPE1);
			drawFramework(505, 117, 127,  56, FRAMEWORK_TYPE1);
			drawFramework(505,  73, 127,  44, FRAMEWORK_TYPE1);

			drawFramework(112,  16, AUDIO_SELECTORS_BOX_WIDTH+4, 69, FRAMEWORK_TYPE2);
			drawFramework(112, 103, AUDIO_SELECTORS_BOX_WIDTH+4, 68, FRAMEWORK_TYPE2);

			drawAudioOutputList();
			drawAudioInputList();

			if (audio.rescanAudioDevicesSupported)
				showPushButton(PB_CONFIG_AUDIO_RESCAN);

			showPushButton(PB_CONFIG_AUDIO_OUTPUT_DOWN);
			showPushButton(PB_CONFIG_AUDIO_OUTPUT_UP);
			showPushButton(PB_CONFIG_AUDIO_INPUT_DOWN);
			showPushButton(PB_CONFIG_AUDIO_INPUT_UP);
			showPushButton(PB_CONFIG_AMP_DOWN);
			showPushButton(PB_CONFIG_AMP_UP);
			showPushButton(PB_CONFIG_MASTVOL_DOWN);
			showPushButton(PB_CONFIG_MASTVOL_UP);

			textOutShadow(114,   4, PAL_FORGRND, PAL_DSKTOP2, "Audio output devices:");
			textOutShadow(114,  91, PAL_FORGRND, PAL_DSKTOP2, "Audio input devices (sampling):");

			textOutShadow(390,   3, PAL_FORGRND, PAL_DSKTOP2, "Audio buffer size:");
			textOutShadow(406,  17, PAL_FORGRND, PAL_DSKTOP2, "Small");
			textOutShadow(406,  31, PAL_FORGRND, PAL_DSKTOP2, "Medium (default)");
			textOutShadow(406,  45, PAL_FORGRND, PAL_DSKTOP2, "Large");
			textOutShadow(406,  59, PAL_FORGRND, PAL_DSKTOP2, "Very large");

			textOutShadow(390,  76, PAL_FORGRND, PAL_DSKTOP2, "Audio bit depth:");
			textOutShadow(406,  90, PAL_FORGRND, PAL_DSKTOP2, "16-bit (default)");
			textOutShadow(406, 104, PAL_FORGRND, PAL_DSKTOP2, "24-bit float");

			textOutShadow(390, 120, PAL_FORGRND, PAL_DSKTOP2, "Mixing device ctrl.:");
			textOutShadow(406, 134, PAL_FORGRND, PAL_DSKTOP2, "Interpolation");
			textOutShadow(406, 147, PAL_FORGRND, PAL_DSKTOP2, "Volume ramping");
			textOutShadow(406, 160, PAL_FORGRND, PAL_DSKTOP2, "1.5-bit dither");

			textOutShadow(509,   3, PAL_FORGRND, PAL_DSKTOP2, "Mixing frequency:");
#ifdef __APPLE__
			textOutShadow(525,  17, PAL_FORGRND, PAL_DSKTOP2, "44100Hz (default)");
			textOutShadow(525,  31, PAL_FORGRND, PAL_DSKTOP2, "48000Hz");
#else
			textOutShadow(525,  17, PAL_FORGRND, PAL_DSKTOP2, "44100Hz");
			textOutShadow(525,  31, PAL_FORGRND, PAL_DSKTOP2, "48000Hz (default)");
#endif
			textOutShadow(525,  45, PAL_FORGRND, PAL_DSKTOP2, "96000Hz");

			textOutShadow(509,  76, PAL_FORGRND, PAL_DSKTOP2, "Frequency table:");
			textOutShadow(525,  90, PAL_FORGRND, PAL_DSKTOP2, "Amiga freq.-table");
			textOutShadow(525, 104, PAL_FORGRND, PAL_DSKTOP2, "Linear freq.-table");

			textOutShadow(509, 120, PAL_FORGRND, PAL_DSKTOP2, "Amplification:");
			charOutShadow(621, 120, PAL_FORGRND, PAL_DSKTOP2, 'X');
			textOutShadow(509, 148, PAL_FORGRND, PAL_DSKTOP2, "Master volume:");

			setConfigIORadioButtonStates();
			setConfigIOCheckButtonStates();

			configDrawAmp();

			setScrollBarPos(SB_AMP_SCROLL,       config.boostLevel - 1, false);
			setScrollBarPos(SB_MASTERVOL_SCROLL, config.masterVol,      false);

			showScrollBar(SB_AUDIO_INPUT_SCROLL);
			showScrollBar(SB_AUDIO_OUTPUT_SCROLL);
			showScrollBar(SB_AMP_SCROLL);
			showScrollBar(SB_MASTERVOL_SCROLL);
		}
		break;

		case CONFIG_SCREEN_LAYOUT:
		{
			drawFramework(110,   0, 142, 106, FRAMEWORK_TYPE1);
			drawFramework(252,   0, 142,  98, FRAMEWORK_TYPE1);
			drawFramework(394,   0, 238,  86, FRAMEWORK_TYPE1);
			drawFramework(110, 106, 142,  67, FRAMEWORK_TYPE1);
			drawFramework(252,  98, 142,  45, FRAMEWORK_TYPE1);
			drawFramework(394,  86, 238, 87, FRAMEWORK_TYPE1);

			drawFramework(252, 143, 142,  30, FRAMEWORK_TYPE1);

			textOutShadow(114, 109, PAL_FORGRND, PAL_DSKTOP2, "Mouse shape:");
			textOutShadow(130, 121, PAL_FORGRND, PAL_DSKTOP2, "Nice");
			textOutShadow(194, 121, PAL_FORGRND, PAL_DSKTOP2, "Ugly");
			textOutShadow(130, 135, PAL_FORGRND, PAL_DSKTOP2, "Awful");
			textOutShadow(194, 135, PAL_FORGRND, PAL_DSKTOP2, "Useable");
			textOutShadow(114, 148, PAL_FORGRND, PAL_DSKTOP2, "Mouse busy shape:");
			textOutShadow(130, 160, PAL_FORGRND, PAL_DSKTOP2, "Vogue");
			textOutShadow(194, 160, PAL_FORGRND, PAL_DSKTOP2, "Mr. H");

			textOutShadow(114,   3, PAL_FORGRND, PAL_DSKTOP2, "Pattern layout:");
			textOutShadow(130,  16, PAL_FORGRND, PAL_DSKTOP2, "Pattern stretch");
			textOutShadow(130,  29, PAL_FORGRND, PAL_DSKTOP2, "Hex count");
			textOutShadow(130,  42, PAL_FORGRND, PAL_DSKTOP2, "Accidential");
			textOutShadow(130,  55, PAL_FORGRND, PAL_DSKTOP2, "Show zeroes");
			textOutShadow(130,  68, PAL_FORGRND, PAL_DSKTOP2, "Framework");
			textOutShadow(130,  81, PAL_FORGRND, PAL_DSKTOP2, "Line number colors");
			textOutShadow(130,  94, PAL_FORGRND, PAL_DSKTOP2, "Channel numbering");

			textOutShadow(256,   3, PAL_FORGRND, PAL_DSKTOP2, "Pattern modes:");
			textOutShadow(271,  16, PAL_FORGRND, PAL_DSKTOP2, "Show volume column");
			textOutShadow(256,  30, PAL_FORGRND, PAL_DSKTOP2, "Maximum visible chn.:");
			textOutShadow(272,  43, PAL_FORGRND, PAL_DSKTOP2, "4 channels");
			textOutShadow(272,  57, PAL_FORGRND, PAL_DSKTOP2, "6 channels");
			textOutShadow(272,  71, PAL_FORGRND, PAL_DSKTOP2, "8 channels");
			textOutShadow(272,  85, PAL_FORGRND, PAL_DSKTOP2, "12 channels");

			textOutShadow(257, 101, PAL_FORGRND, PAL_DSKTOP2, "Pattern font:");
			textOutShadow(272, 115, PAL_FORGRND, PAL_DSKTOP2, "Capitals");
			textOutShadow(338, 114, PAL_FORGRND, PAL_DSKTOP2, "Lower-c.");
			textOutShadow(272, 130, PAL_FORGRND, PAL_DSKTOP2, "Future");
			textOutShadow(338, 129, PAL_FORGRND, PAL_DSKTOP2, "Bold");

			textOutShadow(256, 146, PAL_FORGRND, PAL_DSKTOP2, "Scope style:");
			textOutShadow(272, 159, PAL_FORGRND, PAL_DSKTOP2, "Original");
			textOutShadow(338, 159, PAL_FORGRND, PAL_DSKTOP2, "Lined");

			textOutShadow(414,   3, PAL_FORGRND, PAL_DSKTOP2, "Pattern text");
			textOutShadow(414,  17, PAL_FORGRND, PAL_DSKTOP2, "Block mark");
			textOutShadow(414,  31, PAL_FORGRND, PAL_DSKTOP2, "Text on block");
			textOutShadow(414,  45, PAL_FORGRND, PAL_DSKTOP2, "Mouse");
			textOutShadow(414,  59, PAL_FORGRND, PAL_DSKTOP2, "Desktop");
			textOutShadow(414,  73, PAL_FORGRND, PAL_DSKTOP2, "Buttons");

			textOutShadow(414,  90, PAL_FORGRND, PAL_DSKTOP2, "Arctic");
			textOutShadow(528,  90, PAL_FORGRND, PAL_DSKTOP2, "LiTHe dark");
			textOutShadow(414, 104, PAL_FORGRND, PAL_DSKTOP2, "Aurora Borealis");
			textOutShadow(528, 104, PAL_FORGRND, PAL_DSKTOP2, "Rose");
			textOutShadow(414, 118, PAL_FORGRND, PAL_DSKTOP2, "Blues");
			textOutShadow(528, 118, PAL_FORGRND, PAL_DSKTOP2, "Space Pigs");
			textOutShadow(414, 132, PAL_FORGRND, PAL_DSKTOP2, "Gold");
			textOutShadow(528, 132, PAL_FORGRND, PAL_DSKTOP2, "Violent");
			textOutShadow(414, 146, PAL_FORGRND, PAL_DSKTOP2, "Heavy Metal");
			textOutShadow(528, 146, PAL_FORGRND, PAL_DSKTOP2, "Why colors ?");
			textOutShadow(414, 160, PAL_FORGRND, PAL_DSKTOP2, "Jungle");
			textOutShadow(528, 160, PAL_FORGRND, PAL_DSKTOP2, "User defined");

			if (config.cfg_StdPalNr == PAL_USER_DEFINED)
				updatePaletteSelection();

			showPaletteEditor();

			setConfigLayoutCheckButtonStates();
			setConfigLayoutRadioButtonStates();
		}
		break;

		case CONFIG_SCREEN_MISCELLANEOUS:
		{
			drawFramework(110,   0,  99,  43, FRAMEWORK_TYPE1);
			drawFramework(209,   0, 199,  55, FRAMEWORK_TYPE1);
			drawFramework(408,   0, 224,  91, FRAMEWORK_TYPE1);

			drawFramework(110,  43,  99,  57, FRAMEWORK_TYPE1);
			drawFramework(209,  55, 199, 118, FRAMEWORK_TYPE1);
			drawFramework(408,  91, 224,  82, FRAMEWORK_TYPE1);

			drawFramework(110, 100,  99,  73, FRAMEWORK_TYPE1);

			// text boxes
			drawFramework(485,  15, 145,  14, FRAMEWORK_TYPE2);
			drawFramework(485,  30, 145,  14, FRAMEWORK_TYPE2);
			drawFramework(485,  45, 145,  14, FRAMEWORK_TYPE2);
			drawFramework(485,  60, 145,  14, FRAMEWORK_TYPE2);
			drawFramework(485,  75, 145,  14, FRAMEWORK_TYPE2);

			textOutShadow(114,   3, PAL_FORGRND, PAL_DSKTOP2, "Dir. sorting pri.:");
			textOutShadow(130,  16, PAL_FORGRND, PAL_DSKTOP2, "Ext.");
			textOutShadow(130,  30, PAL_FORGRND, PAL_DSKTOP2, "Name");

			textOutShadow(228,   4, PAL_FORGRND, PAL_DSKTOP2, "Sample cut-to-buffer");
			textOutShadow(228,  17, PAL_FORGRND, PAL_DSKTOP2, "Pattern cut-to-buffer");
			textOutShadow(228,  30, PAL_FORGRND, PAL_DSKTOP2, "Kill notes at music stop");
			textOutShadow(228,  43, PAL_FORGRND, PAL_DSKTOP2, "File-overwrite warning");

			textOutShadow(464,   3, PAL_FORGRND, PAL_DSKTOP2, "Default directories:");
			textOutShadow(413,  17, PAL_FORGRND, PAL_DSKTOP2, "Modules");
			textOutShadow(413,  32, PAL_FORGRND, PAL_DSKTOP2, "Instruments");
			textOutShadow(413,  47, PAL_FORGRND, PAL_DSKTOP2, "Samples");
			textOutShadow(413,  62, PAL_FORGRND, PAL_DSKTOP2, "Patterns");
			textOutShadow(413,  77, PAL_FORGRND, PAL_DSKTOP2, "Tracks");

			textOutShadow(114,  46, PAL_FORGRND, PAL_DSKTOP2, "Window size:");
			textOutShadow(130,  59, PAL_FORGRND, PAL_DSKTOP2, "Auto fit");
			textOutShadow(130,  73, PAL_FORGRND, PAL_DSKTOP2, "1x");
			textOutShadow(172,  73, PAL_FORGRND, PAL_DSKTOP2, "3x");
			textOutShadow(130,  87, PAL_FORGRND, PAL_DSKTOP2, "2x");
			textOutShadow(172,  87, PAL_FORGRND, PAL_DSKTOP2, "4x");
			textOutShadow(114, 103, PAL_FORGRND, PAL_DSKTOP2, "Video settings:");
			textOutShadow(130, 117, PAL_FORGRND, PAL_DSKTOP2, "Vsync off");
			textOutShadow(130, 130, PAL_FORGRND, PAL_DSKTOP2, "Fullscreen");
			textOutShadow(130, 143, PAL_FORGRND, PAL_DSKTOP2, "Pixel filter");

			textOutShadow(213,  58, PAL_FORGRND, PAL_DSKTOP2, "Rec./Edit/Play:");
			textOutShadow(228,  71, PAL_FORGRND, PAL_DSKTOP2, "Multichannel record");
			textOutShadow(228,  84, PAL_FORGRND, PAL_DSKTOP2, "Multichannel \"keyjazz\"");
			textOutShadow(228,  97, PAL_FORGRND, PAL_DSKTOP2, "Multichannel edit");
			textOutShadow(228, 110, PAL_FORGRND, PAL_DSKTOP2, "Record keyrelease notes");
			textOutShadow(228, 123, PAL_FORGRND, PAL_DSKTOP2, "Quantisize");
			textOutShadow(338, 123, PAL_FORGRND, PAL_DSKTOP2, "1/");
			textOutShadow(228, 136, PAL_FORGRND, PAL_DSKTOP2, "Change pattern length when");
			textOutShadow(228, 147, PAL_FORGRND, PAL_DSKTOP2, "inserting/deleting line.");
			textOutShadow(228, 161, PAL_FORGRND, PAL_DSKTOP2, "Allow MIDI-in program change");

			textOutShadow(428,  95, PAL_FORGRND, PAL_DSKTOP2, "MIDI Enable");
			textOutShadow(412, 108, PAL_FORGRND, PAL_DSKTOP2, "Record MIDI-chn.");
			charOutShadow(523, 108, PAL_FORGRND, PAL_DSKTOP2, '(');
			textOutShadow(546, 108, PAL_FORGRND, PAL_DSKTOP2, "All )");
			textOutShadow(428, 121, PAL_FORGRND, PAL_DSKTOP2, "Record transpose");
			textOutShadow(428, 134, PAL_FORGRND, PAL_DSKTOP2, "Record velosity");
			textOutShadow(428, 147, PAL_FORGRND, PAL_DSKTOP2, "Record aftertouch");
			textOutShadow(412, 160, PAL_FORGRND, PAL_DSKTOP2, "Vel./A.t. Senstvty.");
			charOutShadow(547, 160, PAL_FORGRND, PAL_DSKTOP2, '%');

			setConfigMiscCheckButtonStates();
			setConfigMiscRadioButtonStates();

			drawQuantValue();
			drawMIDIChanValue();
			drawMIDITransp();
			drawMIDISens();

			showPushButton(PB_CONFIG_TOGGLE_FULLSCREEN);
			showPushButton(PB_CONFIG_QUANTIZE_UP);
			showPushButton(PB_CONFIG_QUANTIZE_DOWN);
			showPushButton(PB_CONFIG_MIDICHN_UP);
			showPushButton(PB_CONFIG_MIDICHN_DOWN);
			showPushButton(PB_CONFIG_MIDITRANS_UP);
			showPushButton(PB_CONFIG_MIDITRANS_DOWN);
			showPushButton(PB_CONFIG_MIDISENS_DOWN);
			showPushButton(PB_CONFIG_MIDISENS_UP);

			showTextBox(TB_CONF_DEF_MODS_DIR);
			showTextBox(TB_CONF_DEF_INSTRS_DIR);
			showTextBox(TB_CONF_DEF_SAMPS_DIR);
			showTextBox(TB_CONF_DEF_PATTS_DIR);
			showTextBox(TB_CONF_DEF_TRACKS_DIR);
			drawTextBox(TB_CONF_DEF_MODS_DIR);
			drawTextBox(TB_CONF_DEF_INSTRS_DIR);
			drawTextBox(TB_CONF_DEF_SAMPS_DIR);
			drawTextBox(TB_CONF_DEF_PATTS_DIR);
			drawTextBox(TB_CONF_DEF_TRACKS_DIR);

			setScrollBarPos(SB_MIDI_SENS, config.recMIDIVolSens, false);
			showScrollBar(SB_MIDI_SENS);
		}
		break;

		case CONFIG_SCREEN_MIDI_INPUT:
		{
			drawFramework(110, 0, 394, 173, FRAMEWORK_TYPE1);
			drawFramework(112, 2, 369, 169, FRAMEWORK_TYPE2);
			drawFramework(504, 0, 128, 173, FRAMEWORK_TYPE1);

			textOutShadow(528, 112, PAL_FORGRND, PAL_DSKTOP2, "Input Devices");

			blitFast(517, 51, midiLogo, 103, 55);

			showPushButton(PB_CONFIG_MIDI_INPUT_DOWN);
			showPushButton(PB_CONFIG_MIDI_INPUT_UP);

			rescanMidiInputDevices();
			drawMidiInputList();

			showScrollBar(SB_MIDI_INPUT_SCROLL);
		}
		break;
	}
}

void hideConfigScreen(void)
{
	// CONFIG LEFT SIDE
	hideRadioButtonGroup(RB_GROUP_CONFIG_SELECT);
	hideCheckBox(CB_CONF_AUTOSAVE);
	hidePushButton(PB_CONFIG_RESET);
	hidePushButton(PB_CONFIG_LOAD);
	hidePushButton(PB_CONFIG_SAVE);
	hidePushButton(PB_CONFIG_EXIT);

	// CONFIG AUDIO
	hideRadioButtonGroup(RB_GROUP_CONFIG_SOUND_BUFF_SIZE);
	hideRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_BIT_DEPTH);
	hideRadioButtonGroup(RB_GROUP_CONFIG_AUDIO_FREQ);
	hideRadioButtonGroup(RB_GROUP_CONFIG_FREQ_TABLE);
	hideCheckBox(CB_CONF_INTERPOLATION);
	hideCheckBox(CB_CONF_VOL_RAMP);
	hideCheckBox(CB_CONF_DITHER);
	hidePushButton(PB_CONFIG_AUDIO_RESCAN);
	hidePushButton(PB_CONFIG_AUDIO_OUTPUT_DOWN);
	hidePushButton(PB_CONFIG_AUDIO_OUTPUT_UP);
	hidePushButton(PB_CONFIG_AUDIO_INPUT_DOWN);
	hidePushButton(PB_CONFIG_AUDIO_INPUT_UP);
	hidePushButton(PB_CONFIG_AMP_DOWN);
	hidePushButton(PB_CONFIG_AMP_UP);
	hidePushButton(PB_CONFIG_MASTVOL_DOWN);
	hidePushButton(PB_CONFIG_MASTVOL_UP);
	hideScrollBar(SB_AUDIO_INPUT_SCROLL);
	hideScrollBar(SB_AUDIO_OUTPUT_SCROLL);
	hideScrollBar(SB_AMP_SCROLL);
	hideScrollBar(SB_MASTERVOL_SCROLL);

	// CONFIG LAYOUT
	hideRadioButtonGroup(RB_GROUP_CONFIG_MOUSE);
	hideRadioButtonGroup(RB_GROUP_CONFIG_MOUSE_BUSY);
	hideRadioButtonGroup(RB_GROUP_CONFIG_SCOPE);
	hideRadioButtonGroup(RB_GROUP_CONFIG_PATTERN_CHANS);
	hideRadioButtonGroup(RB_GROUP_CONFIG_FONT);
	hideRadioButtonGroup(RB_GROUP_CONFIG_PAL_ENTRIES);
	hideRadioButtonGroup(RB_GROUP_CONFIG_PAL_PRESET);
	hideCheckBox(CB_CONF_PATTSTRETCH);
	hideCheckBox(CB_CONF_HEXCOUNT);
	hideCheckBox(CB_CONF_ACCIDENTAL);
	hideCheckBox(CB_CONF_SHOWZEROES);
	hideCheckBox(CB_CONF_FRAMEWORK);
	hideCheckBox(CB_CONF_LINECOLORS);
	hideCheckBox(CB_CONF_CHANNUMS);
	hideCheckBox(CB_CONF_SHOW_VOLCOL);
	hidePushButton(PB_CONFIG_PAL_R_DOWN);
	hidePushButton(PB_CONFIG_PAL_R_UP);
	hidePushButton(PB_CONFIG_PAL_G_DOWN);
	hidePushButton(PB_CONFIG_PAL_G_UP);
	hidePushButton(PB_CONFIG_PAL_B_DOWN);
	hidePushButton(PB_CONFIG_PAL_B_UP);
	hidePushButton(PB_CONFIG_PAL_CONT_DOWN);
	hidePushButton(PB_CONFIG_PAL_CONT_UP);
	hideScrollBar(SB_PAL_R);
	hideScrollBar(SB_PAL_G);
	hideScrollBar(SB_PAL_B);
	hideScrollBar(SB_PAL_CONTRAST);

	// CONFIG MISCELLANEOUS
	hideRadioButtonGroup(RB_GROUP_CONFIG_FILESORT);
	hideRadioButtonGroup(RB_GROUP_CONFIG_WIN_SIZE);
	hidePushButton(PB_CONFIG_TOGGLE_FULLSCREEN);
	hidePushButton(PB_CONFIG_QUANTIZE_UP);
	hidePushButton(PB_CONFIG_QUANTIZE_DOWN);
	hidePushButton(PB_CONFIG_MIDICHN_UP);
	hidePushButton(PB_CONFIG_MIDICHN_DOWN);
	hidePushButton(PB_CONFIG_MIDITRANS_UP);
	hidePushButton(PB_CONFIG_MIDITRANS_DOWN);
	hidePushButton(PB_CONFIG_MIDISENS_DOWN);
	hidePushButton(PB_CONFIG_MIDISENS_UP);
	hideCheckBox(CB_CONF_FORCE_VSYNC_OFF);
	hideCheckBox(CB_CONF_START_IN_FULLSCREEN);
	hideCheckBox(CB_CONF_FILTERING);
	hideCheckBox(CB_CONF_SAMP_CUT_TO_BUF);
	hideCheckBox(CB_CONF_PATT_CUT_TO_BUF);
	hideCheckBox(CB_CONF_KILL_NOTES_AT_STOP);
	hideCheckBox(CB_CONF_FILE_OVERWRITE_WARN);
	hideCheckBox(CB_CONF_MULTICHAN_REC);
	hideCheckBox(CB_CONF_MULTICHAN_JAZZ);
	hideCheckBox(CB_CONF_MULTICHAN_EDIT);
	hideCheckBox(CB_CONF_REC_KEYOFF);
	hideCheckBox(CB_CONF_QUANTIZATION);
	hideCheckBox(CB_CONF_CHANGE_PATTLEN_INS_DEL);
	hideCheckBox(CB_CONF_MIDI_ALLOW_PC);
	hideCheckBox(CB_CONF_MIDI_ENABLE);
	hideCheckBox(CB_CONF_MIDI_REC_ALL);
	hideCheckBox(CB_CONF_MIDI_REC_TRANS);
	hideCheckBox(CB_CONF_MIDI_REC_VELOC);
	hideCheckBox(CB_CONF_MIDI_REC_AFTERTOUCH);
	hideTextBox(TB_CONF_DEF_MODS_DIR);
	hideTextBox(TB_CONF_DEF_INSTRS_DIR);
	hideTextBox(TB_CONF_DEF_SAMPS_DIR);
	hideTextBox(TB_CONF_DEF_PATTS_DIR);
	hideTextBox(TB_CONF_DEF_TRACKS_DIR);
	hideScrollBar(SB_MIDI_SENS);

	// CONFIG MIDI
	hidePushButton(PB_CONFIG_MIDI_INPUT_DOWN);
	hidePushButton(PB_CONFIG_MIDI_INPUT_UP);
	hideScrollBar(SB_MIDI_INPUT_SCROLL);

	editor.ui.configScreenShown = false;
}

void exitConfigScreen(void)
{
	hideConfigScreen();
	showTopScreen(true);
}

// CONFIG AUDIO

void configToggleS3MLoadWarning(void)
{
	config.dontShowAgainFlags ^= DONT_SHOW_S3M_LOAD_WARNING_FLAG;
}

void configToggleNotYetAppliedWarning(void)
{
	config.dontShowAgainFlags ^= DONT_SHOW_NOT_YET_APPLIED_WARNING_FLAG;
}

void rbConfigIODevices(void)
{
	checkRadioButton(RB_CONFIG_IO_DEVICES);
	editor.currConfigScreen = CONFIG_SCREEN_IO_DEVICES;

	hideConfigScreen();
	showConfigScreen();
}

void rbConfigLayout(void)
{
	checkRadioButton(RB_CONFIG_LAYOUT);
	editor.currConfigScreen = CONFIG_SCREEN_LAYOUT;

	hideConfigScreen();
	showConfigScreen();
}

void rbConfigMiscellaneous(void)
{
	checkRadioButton(RB_CONFIG_MISCELLANEOUS);
	editor.currConfigScreen = CONFIG_SCREEN_MISCELLANEOUS;

	hideConfigScreen();
	showConfigScreen();
}

void rbConfigMidiInput(void)
{
	checkRadioButton(RB_CONFIG_MIDI_INPUT);
	editor.currConfigScreen = CONFIG_SCREEN_MIDI_INPUT;

	hideConfigScreen();
	showConfigScreen();
}

void rbConfigSbs512(void)
{
	config.specialFlags &= ~(BUFFSIZE_1024 + BUFFSIZE_2048 + BUFFSIZE_4096);
	config.specialFlags |= BUFFSIZE_512;

	setNewAudioSettings();
}

void rbConfigSbs1024(void)
{
	config.specialFlags &= ~(BUFFSIZE_512 + BUFFSIZE_2048 + BUFFSIZE_4096);
	config.specialFlags |= BUFFSIZE_1024;

	setNewAudioSettings();
}

void rbConfigSbs2048(void)
{
	config.specialFlags &= ~(BUFFSIZE_512 + BUFFSIZE_1024 + BUFFSIZE_4096);
	config.specialFlags |= BUFFSIZE_2048;

	setNewAudioSettings();
}

void rbConfigSbs4096(void)
{
	config.specialFlags &= ~(BUFFSIZE_512 + BUFFSIZE_1024 + BUFFSIZE_2048);
	config.specialFlags |= BUFFSIZE_4096;

	setNewAudioSettings();
}

void rbConfigAudio16bit(void)
{
	config.specialFlags &= ~BITDEPTH_24;
	config.specialFlags |=  BITDEPTH_16;

	setNewAudioSettings();
}

void rbConfigAudio24bit(void)
{
	config.specialFlags &= ~BITDEPTH_16;
	config.specialFlags |=  BITDEPTH_24;

	// no dither in float mode
	config.audioDither = 0;

	checkBoxes[CB_CONF_DITHER].checked = false;
	if (editor.currConfigScreen == CONFIG_SCREEN_IO_DEVICES)
		drawCheckBox(CB_CONF_DITHER);

	setNewAudioSettings();
}

void rbConfigAudio44kHz(void)
{
	config.audioFreq = 44100;
	setNewAudioSettings();
}

void rbConfigAudio48kHz(void)
{
	config.audioFreq = 48000;
	setNewAudioSettings();
}

void rbConfigAudio96kHz(void)
{
	config.audioFreq = 96000;
	setNewAudioSettings();
}

void rbConfigFreqTableAmiga(void)
{
	lockMixerCallback();
	setFrqTab(false);
	unlockMixerCallback();
}

void rbConfigFreqTableLinear(void)
{
	lockMixerCallback();
	setFrqTab(true);
	unlockMixerCallback();
}

void cbToggleAutoSaveConfig(void)
{
	config.cfg_AutoSave ^= 1;
}

void cbConfigInterpolation(void)
{
	config.interpolation ^= 1;
	audioSetInterpolation(config.interpolation);
}

void cbConfigVolRamp(void)
{
	config.specialFlags ^= NO_VOLRAMP_FLAG;
	audioSetVolRamp((config.specialFlags & NO_VOLRAMP_FLAG) ? false : true);
}

void cbConfigDither(void)
{
	if (config.specialFlags & BITDEPTH_24) // no dither in float mode, force off
	{
		config.audioDither = 0;

		checkBoxes[CB_CONF_DITHER].checked = false;
		if (editor.currConfigScreen == CONFIG_SCREEN_IO_DEVICES)
			drawCheckBox(CB_CONF_DITHER);
	}
	else
	{
		config.audioDither ^= 1;
		updateSendAudSamplesRoutine(true);
	}
}

// CONFIG LAYOUT

static void redrawPatternEditor(void) // called after changing some pattern editor settings in config
{
	// if the cursor was on the volume column while we turned volume column off, move it to effect type slot
	if (!config.ptnS3M && ((editor.cursor.object == CURSOR_VOL1) || (editor.cursor.object == CURSOR_VOL2)))
		editor.cursor.object = CURSOR_EFX0;

	updateChanNums();
	editor.ui.updatePatternEditor = true;
}

void cbConfigPattStretch(void)
{
	config.ptnUnpressed ^= 1;
	redrawPatternEditor();
}

void cbConfigHexCount(void)
{
	config.ptnHex ^= 1;
	redrawPatternEditor();
}

void cbConfigAccidential(void)
{
	config.ptnAcc ^= 1;
	showCheckBox(CB_CONF_ACCIDENTAL);
	redrawPatternEditor();
}

void cbConfigShowZeroes(void)
{
	config.ptnInstrZero ^= 1;
	redrawPatternEditor();
}

void cbConfigFramework(void)
{
	config.ptnFrmWrk ^= 1;
	redrawPatternEditor();
}

void cbConfigLineColors(void)
{
	config.ptnLineLight ^= 1;
	redrawPatternEditor();
}

void cbConfigChanNums(void)
{
	config.ptnChnNumbers ^= 1;
	redrawPatternEditor();
}

void cbConfigShowVolCol(void)
{
	config.ptnS3M ^= 1;
	redrawPatternEditor();
}

void rbConfigMouseNice(void)
{
	config.mouseType = MOUSE_IDLE_SHAPE_NICE;
	checkRadioButton(RB_CONFIG_MOUSE_NICE);
	setMouseShape(config.mouseType);
}

void rbConfigMouseUgly(void)
{
	config.mouseType = MOUSE_IDLE_SHAPE_UGLY;
	checkRadioButton(RB_CONFIG_MOUSE_UGLY);
	setMouseShape(config.mouseType);
}

void rbConfigMouseAwful(void)
{
	config.mouseType = MOUSE_IDLE_SHAPE_AWFUL;
	checkRadioButton(RB_CONFIG_MOUSE_AWFUL);
	setMouseShape(config.mouseType);
}

void rbConfigMouseUseable(void)
{
	config.mouseType = MOUSE_IDLE_SHAPE_USEABLE;
	checkRadioButton(RB_CONFIG_MOUSE_USEABLE);
	setMouseShape(config.mouseType);
}

void rbConfigMouseBusyVogue(void)
{
	config.mouseAnimType = MOUSE_BUSY_SHAPE_GLASS;
	checkRadioButton(RB_CONFIG_MOUSE_BUSY_GLASS);
	resetMouseBusyAnimation();
}

void rbConfigMouseBusyMrH(void)
{
	config.mouseAnimType = MOUSE_BUSY_SHAPE_CLOCK;
	checkRadioButton(RB_CONFIG_MOUSE_BUSY_CLOCK);
	resetMouseBusyAnimation();
}

void rbConfigScopeOriginal(void)
{
	config.specialFlags &= ~LINED_SCOPES;
	checkRadioButton(RB_CONFIG_SCOPE_NORMAL);
}

void rbConfigScopeLined(void)
{
	config.specialFlags |= LINED_SCOPES;
	checkRadioButton(RB_CONFIG_SCOPE_LINED);
}

void rbConfigPatt4Chans(void)
{
	config.ptnMaxChannels = MAX_CHANS_SHOWN_4;
	checkRadioButton(RB_CONFIG_MAXCHAN_4);

	editor.ui.maxVisibleChannels = 2 + (((uint8_t)(config.ptnMaxChannels) + 1) * 2);
	redrawPatternEditor();
}

void rbConfigPatt6Chans(void)
{
	config.ptnMaxChannels = MAX_CHANS_SHOWN_6;
	checkRadioButton(RB_CONFIG_MAXCHAN_6);

	editor.ui.maxVisibleChannels = 2 + (((uint8_t)(config.ptnMaxChannels) + 1) * 2);
	redrawPatternEditor();
}

void rbConfigPatt8Chans(void)
{
	config.ptnMaxChannels = MAX_CHANS_SHOWN_8;
	checkRadioButton(RB_CONFIG_MAXCHAN_8);

	editor.ui.maxVisibleChannels = 2 + (((uint8_t)(config.ptnMaxChannels) + 1) * 2);
	redrawPatternEditor();
}

void rbConfigPatt12Chans(void)
{
	config.ptnMaxChannels = MAX_CHANS_SHOWN_12;
	checkRadioButton(RB_CONFIG_MAXCHAN_12);

	editor.ui.maxVisibleChannels = 2 + (((uint8_t)(config.ptnMaxChannels) + 1) * 2);
	redrawPatternEditor();
}

void rbConfigFontCapitals(void)
{
	config.ptnFont = PATT_FONT_CAPITALS;
	checkRadioButton(RB_CONFIG_FONT_CAPITALS);
	redrawPatternEditor();
}

void rbConfigFontLowerCase(void)
{
	config.ptnFont = PATT_FONT_LOWERCASE;
	checkRadioButton(RB_CONFIG_FONT_LOWERCASE);
	redrawPatternEditor();
}

void rbConfigFontFuture(void)
{
	config.ptnFont = PATT_FONT_FUTURE;
	checkRadioButton(RB_CONFIG_FONT_FUTURE);
	redrawPatternEditor();
}

void rbConfigFontBold(void)
{
	config.ptnFont = PATT_FONT_BOLD;
	checkRadioButton(RB_CONFIG_FONT_BOLD);
	redrawPatternEditor();
}

void rbConfigPalPatternText(void)
{
	editor.currPaletteEdit = 0;
	editor.currPaletteEntry = &video.palette[PAL_PATTEXT];
	checkRadioButton(RB_CONFIG_PAL_PATTERNTEXT);

	updatePaletteSelection();
	showPaletteEditor();
}

void rbConfigPalBlockMark(void)
{
	editor.currPaletteEdit = 1;
	editor.currPaletteEntry = &video.palette[PAL_BLCKMRK];
	checkRadioButton(RB_CONFIG_PAL_BLOCKMARK);

	updatePaletteSelection();
	showPaletteEditor();
}

void rbConfigPalTextOnBlock(void)
{
	editor.currPaletteEdit = 2;
	editor.currPaletteEntry = &video.palette[PAL_BLCKTXT];
	checkRadioButton(RB_CONFIG_PAL_TEXTONBLOCK);

	updatePaletteSelection();
	showPaletteEditor();
}

void rbConfigPalMouse(void)
{
	editor.currPaletteEdit = 3;
	editor.currPaletteEntry = &video.palette[PAL_MOUSEPT];
	checkRadioButton(RB_CONFIG_PAL_MOUSE);

	updatePaletteSelection();
	showPaletteEditor();
}

void rbConfigPalDesktop(void)
{
	editor.currPaletteEdit = 4;
	editor.currPaletteEntry = &video.palette[PAL_DESKTOP];
	checkRadioButton(RB_CONFIG_PAL_DESKTOP);

	setScrollBarPos(SB_PAL_CONTRAST, editor.ui.desktopContrast, false);
	showPaletteEditor();
	updatePaletteSelection();
}

void rbConfigPalButttons(void)
{
	editor.currPaletteEdit = 5;
	editor.currPaletteEntry = &video.palette[PAL_BUTTONS];
	checkRadioButton(RB_CONFIG_PAL_BUTTONS);

	setScrollBarPos(SB_PAL_CONTRAST, editor.ui.buttonContrast, false);
	showPaletteEditor();
	updatePaletteSelection();
}

void rbConfigPalArctic(void)
{
	config.cfg_StdPalNr = PAL_ARCTIC;
	setPalettePreset(PAL_ARCTIC);
	checkRadioButton(RB_CONFIG_PAL_ARCTIC);

	showPaletteEditor();
}

void rbConfigPalLitheDark(void)
{
	config.cfg_StdPalNr = PAL_LITHE_DARK;
	setPalettePreset(PAL_LITHE_DARK);
	checkRadioButton(RB_CONFIG_PAL_LITHE_DARK);

	showPaletteEditor();
}

void rbConfigPalAuroraBorealis(void)
{
	config.cfg_StdPalNr = PAL_AURORA_BOREALIS;
	setPalettePreset(PAL_AURORA_BOREALIS);
	checkRadioButton(RB_CONFIG_PAL_AURORA_BOREALIS);

	showPaletteEditor();
}

void rbConfigPalRose(void)
{
	config.cfg_StdPalNr = PAL_ROSE;
	setPalettePreset(PAL_ROSE);
	checkRadioButton(RB_CONFIG_PAL_ROSE);

	showPaletteEditor();
}

void rbConfigPalBlues(void)
{
	config.cfg_StdPalNr = PAL_BLUES;
	setPalettePreset(PAL_BLUES);
	checkRadioButton(RB_CONFIG_PAL_BLUES);

	showPaletteEditor();
}

void rbConfigPalSpacePigs(void)
{
	config.cfg_StdPalNr = PAL_SPACE_PIGS;
	setPalettePreset(PAL_SPACE_PIGS);
	checkRadioButton(RB_CONFIG_PAL_SPACE_PIGS);

	showPaletteEditor();
}

void rbConfigPalGold(void)
{
	config.cfg_StdPalNr = PAL_GOLD;
	setPalettePreset(PAL_GOLD);
	checkRadioButton(RB_CONFIG_PAL_GOLD);

	showPaletteEditor();
}

void rbConfigPalViolent(void)
{
	config.cfg_StdPalNr = PAL_VIOLENT;
	setPalettePreset(PAL_VIOLENT);
	checkRadioButton(RB_CONFIG_PAL_VIOLENT);

	showPaletteEditor();
}

void rbConfigPalHeavyMetal(void)
{
	config.cfg_StdPalNr = PAL_HEAVY_METAL;
	setPalettePreset(PAL_HEAVY_METAL);
	checkRadioButton(RB_CONFIG_PAL_HEAVY_METAL);

	showPaletteEditor();
}

void rbConfigPalWhyColors(void)
{
	config.cfg_StdPalNr = PAL_WHY_COLORS;
	setPalettePreset(PAL_WHY_COLORS);
	checkRadioButton(RB_CONFIG_PAL_WHY_COLORS);

	showPaletteEditor();
}

void rbConfigPalJungle(void)
{
	config.cfg_StdPalNr = PAL_JUNGLE;
	setPalettePreset(PAL_JUNGLE);
	checkRadioButton(RB_CONFIG_PAL_JUNGLE);

	showPaletteEditor();
}

void rbConfigPalUserDefined(void)
{
	config.cfg_StdPalNr = PAL_USER_DEFINED;
	setPalettePreset(PAL_USER_DEFINED);
	checkRadioButton(RB_CONFIG_PAL_USER_DEFINED);

	showPaletteEditor();
}

void rbFileSortExt(void)
{
	config.cfg_SortPriority = FILESORT_EXT;
	checkRadioButton(RB_CONFIG_FILESORT_EXT);
	editor.diskOpReadOnOpen = true;
}

void rbFileSortName(void)
{
	config.cfg_SortPriority = FILESORT_NAME;
	checkRadioButton(RB_CONFIG_FILESORT_NAME);
	editor.diskOpReadOnOpen = true;
}

void rbWinSizeAuto(void)
{
	if (video.fullscreen)
	{
		okBox(0, "System message", "You can't change the window size while in fullscreen mode!");
		return;
	}

	config.windowFlags &= ~(WINSIZE_1X + WINSIZE_2X + WINSIZE_3X + WINSIZE_4X);
	config.windowFlags |= WINSIZE_AUTO;

	setWindowSizeFromConfig(true);
	checkRadioButton(RB_CONFIG_WIN_SIZE_AUTO);
}

void rbWinSize1x(void)
{
	if (video.fullscreen)
	{
		okBox(0, "System message", "You can't change the window size while in fullscreen mode!");
		return;
	}

	config.windowFlags &= ~(WINSIZE_AUTO + WINSIZE_2X + WINSIZE_3X + WINSIZE_4X);
	config.windowFlags |= WINSIZE_1X;

	setWindowSizeFromConfig(true);
	checkRadioButton(RB_CONFIG_WIN_SIZE_1X);
}

void rbWinSize2x(void)
{
	if (video.fullscreen)
	{
		okBox(0, "System message", "You can't change the window size while in fullscreen mode!");
		return;
	}

	config.windowFlags &= ~(WINSIZE_AUTO + WINSIZE_1X + WINSIZE_3X + WINSIZE_4X);
	config.windowFlags |= WINSIZE_2X;

	setWindowSizeFromConfig(true);
	checkRadioButton(RB_CONFIG_WIN_SIZE_2X);
}

void rbWinSize3x(void)
{
	if (video.fullscreen)
	{
		okBox(0, "System message", "You can't change the window size while in fullscreen mode!");
		return;
	}

	config.windowFlags &= ~(WINSIZE_AUTO + WINSIZE_1X + WINSIZE_2X + WINSIZE_4X);
	config.windowFlags |= WINSIZE_3X;

	setWindowSizeFromConfig(true);
	checkRadioButton(RB_CONFIG_WIN_SIZE_3X);
}

void rbWinSize4x(void)
{
	if (video.fullscreen)
	{
		okBox(0, "System message", "You can't change the window size while in fullscreen mode!");
		return;
	}

	config.windowFlags &= ~(WINSIZE_AUTO + WINSIZE_1X + WINSIZE_2X + WINSIZE_3X);
	config.windowFlags |= WINSIZE_4X;

	setWindowSizeFromConfig(true);
	checkRadioButton(RB_CONFIG_WIN_SIZE_4X);
}

void sbPalRPos(uint32_t pos)
{
	uint8_t paletteEntry;
	uint16_t r;
	uint32_t *pixel;

	if (config.cfg_StdPalNr != PAL_USER_DEFINED)
		return;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	r = P8_TO_P6(RGB_R(*pixel));
	if (pos != r)
	{
		r = (int16_t)(pos);

		*pixel &= 0xFF00FFFF;
		*pixel |= (P6_TO_P8(r) << 16);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextR = (uint8_t)(r);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkR   = (uint8_t)(r); break;
			case PAL_BLCKTXT: config.palTextOnBlockR = (uint8_t)(r); break;
			case PAL_MOUSEPT: config.palMouseR       = (uint8_t)(r); break;
			case PAL_DESKTOP: config.palDesktopR     = (uint8_t)(r); break;
			case PAL_BUTTONS: config.palButtonsR     = (uint8_t)(r); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void sbPalGPos(uint32_t pos)
{
	uint8_t paletteEntry;
	uint16_t g;
	uint32_t *pixel;

	if (config.cfg_StdPalNr != PAL_USER_DEFINED)
		return;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	g = P8_TO_P6(RGB_G(*pixel));
	if (pos != g)
	{
		g = (int16_t)(pos);

		*pixel &= 0xFFFF00FF;
		*pixel |= (P6_TO_P8(g) << 8);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextG = (uint8_t)(g);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkG   = (uint8_t)(g); break;
			case PAL_BLCKTXT: config.palTextOnBlockG = (uint8_t)(g); break;
			case PAL_MOUSEPT: config.palMouseG       = (uint8_t)(g); break;
			case PAL_DESKTOP: config.palDesktopG     = (uint8_t)(g); break;
			case PAL_BUTTONS: config.palButtonsG     = (uint8_t)(g); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void sbPalBPos(uint32_t pos)
{
	uint8_t paletteEntry;
	uint16_t b;
	uint32_t *pixel;

	if (config.cfg_StdPalNr != PAL_USER_DEFINED)
		return;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	b = P8_TO_P6(RGB_B(*pixel));
	if (pos != b)
	{
		b = (int16_t)(pos);

		*pixel &= 0xFFFFFF00;
		*pixel |= P6_TO_P8(b);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextB = (uint8_t)(b);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkB   = (uint8_t)(b); break;
			case PAL_BLCKTXT: config.palTextOnBlockB = (uint8_t)(b); break;
			case PAL_MOUSEPT: config.palMouseB       = (uint8_t)(b); break;
			case PAL_DESKTOP: config.palDesktopB     = (uint8_t)(b); break;
			case PAL_BUTTONS: config.palButtonsB     = (uint8_t)(b); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void sbPalContrastPos(uint32_t pos)
{
	uint8_t paletteEntry, update;

	if (config.cfg_StdPalNr != PAL_USER_DEFINED)
		return;

	update = false;

	paletteEntry = getCurrentPaletteEntry();
	if (paletteEntry == PAL_DESKTOP)
	{
		if ((int8_t)(pos) != editor.ui.desktopContrast)
		{
			editor.ui.desktopContrast = (int8_t)(pos);
			update = true;
		}
	}
	else if (paletteEntry == PAL_BUTTONS)
	{
		if ((int8_t)(pos) != editor.ui.buttonContrast)
		{
			editor.ui.buttonContrast = (int8_t)(pos);
			update = true;
		}
	}

	updatePaletteContrast();

	if (update)
	{
		showTopScreen(false);
		showBottomScreen();
	}
}

void configPalRDown(void)
{
	uint8_t paletteEntry;
	int16_t r;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	r = P8_TO_P6(RGB_R(*pixel));
	if (r > 0)
	{
		r--;

		*pixel &= 0xFF00FFFF;
		*pixel |= (P6_TO_P8(r) << 16);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextR = (uint8_t)(r);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkR   = (uint8_t)(r); break;
			case PAL_BLCKTXT: config.palTextOnBlockR = (uint8_t)(r); break;
			case PAL_MOUSEPT: config.palMouseR       = (uint8_t)(r); break;
			case PAL_DESKTOP: config.palDesktopR     = (uint8_t)(r); break;
			case PAL_BUTTONS: config.palButtonsR     = (uint8_t)(r); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalRUp(void)
{
	uint8_t paletteEntry;
	int16_t r;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	r = P8_TO_P6(RGB_R(*pixel));
	if (r < 63)
	{
		r++;

		*pixel &= 0xFF00FFFF;
		*pixel |= (P6_TO_P8(r) << 16);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextR = (uint8_t)(r);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkR   = (uint8_t)(r); break;
			case PAL_BLCKTXT: config.palTextOnBlockR = (uint8_t)(r); break;
			case PAL_MOUSEPT: config.palMouseR       = (uint8_t)(r); break;
			case PAL_DESKTOP: config.palDesktopR     = (uint8_t)(r); break;
			case PAL_BUTTONS: config.palButtonsR     = (uint8_t)(r); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalGDown(void)
{
	uint8_t paletteEntry;
	int16_t g;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	g = P8_TO_P6(RGB_G(*pixel));
	if (g > 0)
	{
		g--;

		*pixel &= 0xFFFF00FF;
		*pixel |= (P6_TO_P8(g) << 8);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextG = (uint8_t)(g);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkG   = (uint8_t)(g); break;
			case PAL_BLCKTXT: config.palTextOnBlockG = (uint8_t)(g); break;
			case PAL_MOUSEPT: config.palMouseG       = (uint8_t)(g); break;
			case PAL_DESKTOP: config.palDesktopG     = (uint8_t)(g); break;
			case PAL_BUTTONS: config.palButtonsG     = (uint8_t)(g); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalGUp(void)
{
	uint8_t paletteEntry;
	int16_t g;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	g = P8_TO_P6(RGB_G(*pixel));
	if (g < 63)
	{
		g++;

		*pixel &= 0xFFFF00FF;
		*pixel |= (P6_TO_P8(g) << 8);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextG = (uint8_t)(g);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkG   = (uint8_t)(g); break;
			case PAL_BLCKTXT: config.palTextOnBlockG = (uint8_t)(g); break;
			case PAL_MOUSEPT: config.palMouseG       = (uint8_t)(g); break;
			case PAL_DESKTOP: config.palDesktopG     = (uint8_t)(g); break;
			case PAL_BUTTONS: config.palButtonsG     = (uint8_t)(g); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalBDown(void)
{
	uint8_t paletteEntry;
	int16_t b;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	b = P8_TO_P6(RGB_B(*pixel));
	if (b > 0)
	{
		b--;

		*pixel &= 0xFFFFFF00;
		*pixel |= P6_TO_P8(b);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextB = (uint8_t)(b);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkB   = (uint8_t)(b); break;
			case PAL_BLCKTXT: config.palTextOnBlockB = (uint8_t)(b); break;
			case PAL_MOUSEPT: config.palMouseB       = (uint8_t)(b); break;
			case PAL_DESKTOP: config.palDesktopB     = (uint8_t)(b); break;
			case PAL_BUTTONS: config.palButtonsB     = (uint8_t)(b); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalBUp(void)
{
	uint8_t paletteEntry;
	int16_t b;
	uint32_t *pixel;

	paletteEntry = getCurrentPaletteEntry();
	pixel = &video.palette[paletteEntry];

	b = P8_TO_P6(RGB_B(*pixel));
	if (b < 63)
	{
		b++;

		*pixel &= 0xFFFFFF00;
		*pixel |= P6_TO_P8(b);

		switch (paletteEntry)
		{
			case PAL_PATTEXT:
			{
				config.palPattTextB = (uint8_t)(b);
				updateLoopPinPalette();
			}
			break;

			case PAL_BLCKMRK: config.palBlockMarkB   = (uint8_t)(b); break;
			case PAL_BLCKTXT: config.palTextOnBlockB = (uint8_t)(b); break;
			case PAL_MOUSEPT: config.palMouseB       = (uint8_t)(b); break;
			case PAL_DESKTOP: config.palDesktopB     = (uint8_t)(b); break;
			case PAL_BUTTONS: config.palButtonsB     = (uint8_t)(b); break;
			default: break;
		}

		updatePaletteContrast();

		showTopScreen(false);
		showBottomScreen();

		drawCurrentPaletteColor();
	}
}

void configPalContDown(void)
{
	bool update;

	update = false;

	if (editor.currPaletteEdit == 4)
	{
		if (editor.ui.desktopContrast > 0)
		{
			editor.ui.desktopContrast--;
			update = true;
		}
	}
	else if (editor.currPaletteEdit == 5)
	{
		if (editor.ui.buttonContrast > 0)
		{
			editor.ui.buttonContrast--;
			update = true;
		}
	}

	updatePaletteContrast();

	if (update)
	{
		showTopScreen(false);
		showBottomScreen();
	}
}

void configPalContUp(void)
{
	bool update;

	update = false;

	if (editor.currPaletteEdit == 4)
	{
		if (editor.ui.desktopContrast < 100)
		{
			editor.ui.desktopContrast++;
			update = true;
		}
	}
	else if (editor.currPaletteEdit == 5)
	{
		if (editor.ui.buttonContrast < 100)
		{
			editor.ui.buttonContrast++;
			update = true;
		}
	}

	updatePaletteContrast();

	if (update)
	{
		showTopScreen(false);
		showBottomScreen();
	}
}

void cbSampCutToBuff(void)
{
	config.smpCutToBuffer ^= 1;
}

void cbPattCutToBuff(void)
{
	config.ptnCutToBuffer ^= 1;
}

void cbKillNotesAtStop(void)
{
	config.killNotesOnStopPlay ^= 1;
}

void cbFileOverwriteWarn(void)
{
	config.cfg_OverwriteWarning ^= 1;
}

void cbMultiChanRec(void)
{
	config.multiRec ^= 1;
}

void cbMultiChanKeyJazz(void)
{
	config.multiKeyJazz ^= 1;
}

void cbMultiChanEdit(void)
{
	config.multiEdit ^= 1;
}

void cbRecKeyOff(void)
{
	config.recRelease ^= 1;
}

void cbQuantisize(void)
{
	config.recQuant ^= 1;
}

void cbChangePattLenInsDel(void)
{
	config.recTrueInsert ^= 1;
}

void cbMIDIAllowPC(void)
{
	config.recMIDIAllowPC ^= 1;
}

void cbMIDIEnable(void)
{
	midi.enable ^= 1;
}

void cbMIDIRecTransp(void)
{
	config.recMIDITransp ^= 1;
}

void cbMIDIRecAllChn(void)
{
	config.recMIDIAllChn ^= 1;
}

void cbMIDIRecVelosity(void)
{
	config.recMIDIVelosity ^= 1;
}

void cbMIDIRecAftert(void)
{
	config.recMIDIAftert ^= 1;
}

void cbVsyncOff(void)
{
	config.windowFlags ^= FORCE_VSYNC_OFF;

	if (!(config.dontShowAgainFlags & DONT_SHOW_NOT_YET_APPLIED_WARNING_FLAG))
		okBox(7, "System message", "This setting is not applied until you close and reopen the program.");
}

void cbFullScreen(void)
{
	config.windowFlags ^= START_IN_FULLSCR;

	if (!(config.dontShowAgainFlags & DONT_SHOW_NOT_YET_APPLIED_WARNING_FLAG))
		okBox(7, "System message", "This setting is not applied until you close and reopen the program.");
}

void cbPixelFilter(void)
{
	config.windowFlags ^= FILTERING;
	recreateTexture();

	if (video.fullscreen)
	{
		leaveFullScreen();
		enterFullscreen();
	}
}

void configQuantizeUp(void)
{
	if (config.recQuantRes <= 8)
	{
		config.recQuantRes *= 2;
		drawQuantValue();
	}
}

void configQuantizeDown(void)
{
	if (config.recQuantRes > 1)
	{
		config.recQuantRes /= 2;
		drawQuantValue();
	}
}

void configMIDIChnUp(void)
{
	config.recMIDIChn++;
	config.recMIDIChn = ((config.recMIDIChn - 1) % 16) + 1;

	drawMIDIChanValue();
}

void configMIDIChnDown(void)
{
	config.recMIDIChn--;
	config.recMIDIChn = (((uint16_t)(config.recMIDIChn - 1)) % 16) + 1;

	drawMIDIChanValue();
}

void configMIDITransUp(void)
{
	if (config.recMIDITranspVal < 72)
	{
		config.recMIDITranspVal++;
		drawMIDITransp();
	}
}

void configMIDITransDown(void)
{
	if (config.recMIDITranspVal > -72)
	{
		config.recMIDITranspVal--;
		drawMIDITransp();
	}
}

void configMIDISensDown(void)
{
	scrollBarScrollUp(SB_MIDI_SENS, 1);
}

void configMIDISensUp(void)
{
	scrollBarScrollDown(SB_MIDI_SENS, 1);
}

void sbMIDISens(uint32_t pos)
{
	if ((int16_t)(pos) != config.recMIDIVolSens)
	{
		config.recMIDIVolSens = (int16_t)(pos);
		drawMIDISens();
	}
}

void sbAmp(uint32_t pos)
{
	config.boostLevel = 1 + (int8_t)(pos);
	setAudioAmp(config.boostLevel, config.masterVol, config.specialFlags & BITDEPTH_24);
	configDrawAmp();
	updateWavRendererSettings();
}

void configAmpDown(void)
{
	if (config.boostLevel > 1)
	{
		scrollBarScrollUp(SB_AMP_SCROLL, 1);
		updateWavRendererSettings();
	}
}

void configAmpUp(void)
{
	if (config.boostLevel < 32)
	{
		scrollBarScrollDown(SB_AMP_SCROLL, 1);
		updateWavRendererSettings();
	}
}

void sbMasterVol(uint32_t pos)
{
	config.masterVol = (int16_t)(pos);
	setAudioAmp(config.boostLevel, config.masterVol, config.specialFlags & BITDEPTH_24);
}

void configMasterVolDown(void)
{
	if (config.masterVol > 0)
		scrollBarScrollUp(SB_MASTERVOL_SCROLL, 1);
}

void configMasterVolUp(void)
{
	if (config.masterVol < 256)
		scrollBarScrollDown(SB_MASTERVOL_SCROLL, 1);
}

// default FT2.CFG (unencrypted, and some settings changed)
const uint8_t defaultConfigBuffer[CONFIG_FILE_SIZE] =
{
	0x46,0x61,0x73,0x74,0x54,0x72,0x61,0x63,0x6B,0x65,0x72,0x20,0x32,0x2E,0x30,0x20,0x63,0x6F,0x6E,0x66,
	0x69,0x67,0x75,0x72,0x61,0x74,0x69,0x6F,0x6E,0x20,0x66,0x69,0x6C,0x65,0x1A,0x01,0x01,0x80,0xBB,0x00,
	0x00,0xFF,0x00,0x00,0x01,0xDC,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0xFF,0x00,0x20,0x02,0x01,0x00,
	0x05,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x04,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x24,0x2F,0x3F,0x09,0x09,0x10,0x3F,0x3F,0x3F,0x13,0x18,0x26,0x3F,0x3F,0x3F,0x27,0x27,
	0x27,0x00,0x00,0x00,0x08,0x0A,0x0F,0x20,0x29,0x3F,0x0F,0x0F,0x0F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
	0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x01,0x0A,0x10,0x0A,0xE0,0x08,0xC0,0x08,0x40,0x08,0x20,0x08,
	0xF1,0x04,0xF2,0x04,0x81,0x04,0x82,0x04,0x20,0x30,0x40,0x50,0x61,0x62,0x71,0x72,0x91,0x92,0xF8,0x03,
	0x2D,0x88,0x18,0x00,0x66,0x88,0x18,0x00,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x01,0x01,0x10,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x64,0x00,0x01,0x01,
	0x01,0x01,0x12,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0A,0x00,0x01,0x60,0x00,0x05,0x56,0x6F,0x67,
	0x75,0x65,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x02,0x16,0x05,0x4D,0x72,0x2E,0x20,0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x17,0x04,0x4C,0x6F,0x6F,0x74,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x07,
	0x0A,0x4C,0x69,0x7A,0x61,0x72,0x64,0x6B,0x69,0x6E,0x67,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x06,0x03,0x41,0x6C,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x05,0x03,0x55,0x62,0x65,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x04,0x00,0x04,0x06,0x4E,0x69,0x6B,0x6C,0x61,0x73,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x03,0x05,0x4A,0x65,0x6E,0x73,0x61,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x02,
	0x05,0x54,0x6F,0x62,0x62,0x65,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x01,0x08,0x4B,0x61,0x72,0x6F,0x6C,0x69,0x6E,0x61,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x02,0x00,
	0x00,0x00,0x00,0x99,0xE2,0x27,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x0A,0x00,0x00,0x00,0x30,
	0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x2C,0x00,0x0E,0x00,0x08,0x00,0x18,0x00,0x16,0x00,0x20,0x00,0x08,
	0x00,0x3C,0x00,0x00,0x00,0x46,0x00,0x00,0x00,0x50,0x00,0x00,0x00,0x5A,0x00,0x00,0x00,0x64,0x00,0x00,
	0x00,0x6E,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x0A,0x00,0x28,0x00,0x1E,0x00,0x18,0x00,0x32,0x00,0x20,
	0x00,0x3C,0x00,0x20,0x00,0x46,0x00,0x20,0x00,0x50,0x00,0x20,0x00,0x5A,0x00,0x20,0x00,0x64,0x00,0x20,
	0x00,0x6E,0x00,0x20,0x00,0x78,0x00,0x20,0x00,0x82,0x00,0x20,0x00,0x00,0x00,0x30,0x00,0x04,0x00,0x40,
	0x00,0x08,0x00,0x2C,0x00,0x0E,0x00,0x08,0x00,0x18,0x00,0x16,0x00,0x20,0x00,0x08,0x00,0x3C,0x00,0x00,
	0x00,0x46,0x00,0x00,0x00,0x50,0x00,0x00,0x00,0x5A,0x00,0x00,0x00,0x64,0x00,0x00,0x00,0x6E,0x00,0x00,
	0x00,0x00,0x00,0x20,0x00,0x0A,0x00,0x28,0x00,0x1E,0x00,0x18,0x00,0x32,0x00,0x20,0x00,0x3C,0x00,0x20,
	0x00,0x46,0x00,0x20,0x00,0x50,0x00,0x20,0x00,0x5A,0x00,0x20,0x00,0x64,0x00,0x20,0x00,0x6E,0x00,0x20,
	0x00,0x78,0x00,0x20,0x00,0x82,0x00,0x20,0x00,0x00,0x00,0x30,0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x2C,
	0x00,0x0E,0x00,0x08,0x00,0x18,0x00,0x16,0x00,0x20,0x00,0x08,0x00,0x3C,0x00,0x00,0x00,0x46,0x00,0x00,
	0x00,0x50,0x00,0x00,0x00,0x5A,0x00,0x00,0x00,0x64,0x00,0x00,0x00,0x6E,0x00,0x00,0x00,0x00,0x00,0x20,
	0x00,0x0A,0x00,0x28,0x00,0x1E,0x00,0x18,0x00,0x32,0x00,0x20,0x00,0x3C,0x00,0x20,0x00,0x46,0x00,0x20,
	0x00,0x50,0x00,0x20,0x00,0x5A,0x00,0x20,0x00,0x64,0x00,0x20,0x00,0x6E,0x00,0x20,0x00,0x78,0x00,0x20,
	0x00,0x82,0x00,0x20,0x00,0x00,0x00,0x30,0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x2C,0x00,0x0E,0x00,0x08,
	0x00,0x18,0x00,0x16,0x00,0x20,0x00,0x08,0x00,0x3C,0x00,0x00,0x00,0x46,0x00,0x00,0x00,0x50,0x00,0x00,
	0x00,0x5A,0x00,0x00,0x00,0x64,0x00,0x00,0x00,0x6E,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x0A,0x00,0x28,
	0x00,0x1E,0x00,0x18,0x00,0x32,0x00,0x20,0x00,0x3C,0x00,0x20,0x00,0x46,0x00,0x20,0x00,0x50,0x00,0x20,
	0x00,0x5A,0x00,0x20,0x00,0x64,0x00,0x20,0x00,0x6E,0x00,0x20,0x00,0x78,0x00,0x20,0x00,0x82,0x00,0x20,
	0x00,0x00,0x00,0x30,0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x2C,0x00,0x0E,0x00,0x08,0x00,0x18,0x00,0x16,
	0x00,0x20,0x00,0x08,0x00,0x3C,0x00,0x00,0x00,0x46,0x00,0x00,0x00,0x50,0x00,0x00,0x00,0x5A,0x00,0x00,
	0x00,0x64,0x00,0x00,0x00,0x6E,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x0A,0x00,0x28,0x00,0x1E,0x00,0x18,
	0x00,0x32,0x00,0x20,0x00,0x3C,0x00,0x20,0x00,0x46,0x00,0x20,0x00,0x50,0x00,0x20,0x00,0x5A,0x00,0x20,
	0x00,0x64,0x00,0x20,0x00,0x6E,0x00,0x20,0x00,0x78,0x00,0x20,0x00,0x82,0x00,0x20,0x00,0x00,0x00,0x30,
	0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x2C,0x00,0x0E,0x00,0x08,0x00,0x18,0x00,0x16,0x00,0x20,0x00,0x08,
	0x00,0x3C,0x00,0x00,0x00,0x46,0x00,0x00,0x00,0x50,0x00,0x00,0x00,0x5A,0x00,0x00,0x00,0x64,0x00,0x00,
	0x00,0x6E,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x0A,0x00,0x28,0x00,0x1E,0x00,0x18,0x00,0x32,0x00,0x20,
	0x00,0x3C,0x00,0x20,0x00,0x46,0x00,0x20,0x00,0x50,0x00,0x20,0x00,0x5A,0x00,0x20,0x00,0x64,0x00,0x20,
	0x00,0x6E,0x00,0x20,0x00,0x78,0x00,0x20,0x00,0x82,0x00,0x20,0x00,0x06,0x00,0x06,0x00,0x06,0x00,0x06,
	0x00,0x06,0x00,0x06,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x03,0x00,0x03,
	0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x05,
	0x00,0x06,0x00,0x06,0x00,0x06,0x00,0x06,0x00,0x06,0x00,0x06,0x00,0x02,0x00,0x02,0x00,0x02,0x00,0x02,
	0x00,0x02,0x00,0x02,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x03,0x00,0x05,0x00,0x05,
	0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x80,0x00,0x80,0x00,0x80,0x00,0x80,0x00,0x80,0x00,0x80,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x07,0x00,0x07,0x00,0x07,0x00,0x07,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC8,0x00,0x03,0x00,0x40,0x1F,0x40,
	0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,
	0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x40,0x1F,0x01,0x00,0x00,0x08,0x00,0x00,0x00
};
