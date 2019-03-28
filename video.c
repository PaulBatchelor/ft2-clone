// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#else
#include <unistd.h> // usleep()
#endif
#include "ft2_header.h"
#include "ft2_config.h"
#include "ft2_gfxdata.h"
#include "ft2_gui.h"
#include "ft2_video.h"
#include "ft2_events.h"
#include "ft2_mouse.h"
#include "ft2_scopes.h"
#include "ft2_pattern_ed.h"
#include "ft2_sample_ed.h"
#include "ft2_nibbles.h"
#include "ft2_inst_ed.h"
#include "ft2_diskop.h"
#include "ft2_about.h"
#include "ft2_trim.h"
#include "ft2_sampling.h"
#include "ft2_module_loader.h"
#include "ft2_midi.h"

typedef struct pal16_t
{
	uint8_t r, g, b;
} pal16;

// these two are defined at the bottom of this file
extern const uint8_t textCursorData[12];
extern const pal16 palTable[12][13];

static bool songIsModified;
static uint32_t paletteTemp[PAL_NUM];
static uint64_t timeNext64, timeNext64Frac;
static sprite_t sprites[SPRITE_NUM];

static void drawReplayerData(void);

void flipFrame(void)
{
	renderSprites();

	SDL_UpdateTexture(video.texture, NULL, video.frameBuffer, SCREEN_W * sizeof (int32_t));

	SDL_RenderClear(video.renderer);
	SDL_RenderCopy(video.renderer, video.texture, NULL, NULL);
	SDL_RenderPresent(video.renderer);

	eraseSprites();

	waitVBL();
	editor.framesPassed++;
}

void showErrorMsgBox(const char *fmt, ...)
{
	char strBuf[256];
	va_list args;

	// format the text string
	va_start(args, fmt);
	vsnprintf(strBuf, sizeof (strBuf), fmt, args);
	va_end(args);

	// window can be NULL here, no problem...
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", strBuf, video.window);
}

void updateRenderSizeVars(void)
{
	int32_t di;
#ifdef __APPLE__
	int32_t actualScreenW, actualScreenH;
	float fXUpscale, fYUpscale;
#endif
	float fXScale, fYScale;
	SDL_DisplayMode dm;

	if (video.fullscreen)
	{
		di = SDL_GetWindowDisplayIndex(video.window);
		if (di < 0)
			di = 0; // return display index 0 (default) on error

		SDL_GetDesktopDisplayMode(di, &dm);
		video.displayW = dm.w;
		video.displayH = dm.h;

		if (config.windowFlags & FILTERING)
		{
			video.renderW = video.displayW;
			video.renderH = video.displayH;
			video.renderX = 0;
			video.renderY = 0;
		}
		else
		{
			SDL_RenderGetScale(video.renderer, &fXScale, &fYScale);

			video.renderW = (int32_t)(SCREEN_W * fXScale);
			video.renderH = (int32_t)(SCREEN_H * fYScale);

#ifdef __APPLE__
			// retina high-DPI hackery (SDL2 is bad at reporting actual rendering sizes on macOS w/ high-DPI)
			SDL_GL_GetDrawableSize(video.window, &actualScreenW, &actualScreenH);

			fXUpscale = ((float)(actualScreenW) / video.displayW);
			fYUpscale = ((float)(actualScreenH) / video.displayH);

			// downscale back to correct sizes
			if (fXUpscale != 0.0f) video.renderW = (int32_t)(video.renderW / fXUpscale);
			if (fYUpscale != 0.0f) video.renderH = (int32_t)(video.renderH / fYUpscale);
#endif
			video.renderX = (video.displayW - video.renderW) / 2;
			video.renderY = (video.displayH - video.renderH) / 2;
		}
	}
	else
	{
		SDL_GetWindowSize(video.window, &video.renderW, &video.renderH);

		video.renderX = 0;
		video.renderY = 0;
	}
}

void enterFullscreen(void)
{
	SDL_DisplayMode dm;

	strcpy(editor.ui.fullscreenButtonText, "Go windowed");
	if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_MISCELLANEOUS))
		showConfigScreen(); // redraw so that we can see the new button text

	if (config.windowFlags & FILTERING)
	{
		SDL_GetDesktopDisplayMode(0, &dm);
		SDL_RenderSetLogicalSize(video.renderer, dm.w, dm.h);
	}
	else
	{
		SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);
	}

	SDL_SetWindowSize(video.window, SCREEN_W, SCREEN_H);
	SDL_SetWindowFullscreen(video.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_SetWindowGrab(video.window, SDL_TRUE);

	updateRenderSizeVars();
	updateMouseScaling();
}

void leaveFullScreen(void)
{
	strcpy(editor.ui.fullscreenButtonText, "Go fullscreen");
	if (editor.ui.configScreenShown && (editor.currConfigScreen == CONFIG_SCREEN_MISCELLANEOUS))
		showConfigScreen(); // redraw so that we can see the new button text

	SDL_SetWindowFullscreen(video.window, 0);
	SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);

	setWindowSizeFromConfig(false); // also updates mouse scaling and render size vars
	SDL_SetWindowSize(video.window, SCREEN_W * video.upscaleFactor, SCREEN_H * video.upscaleFactor);
	SDL_SetWindowPosition(video.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowGrab(video.window, SDL_FALSE);

	updateRenderSizeVars();
	updateMouseScaling();
}

void toggleFullScreen(void)
{
	video.fullscreen ^= 1;

	if (video.fullscreen)
		enterFullscreen();
	else
		leaveFullScreen();
}

static float palPow(float fX, float fY)
{
	if (fY == 1.0f)
		return (fX);

	fY *= logf(fabsf(fX));
	fY  = CLAMP(fY, -86.0f, 86.0f);

	return (expf(fY));
}

static uint8_t palMax(float fC)
{
	int32_t x;

	x = (int32_t)(fC);
	return ((uint8_t)(CLAMP(x, 0, 63)));
}

void updatePaletteContrast(void)
{
	uint8_t r, g, b, newR, newG, newB;
	float fContrast;

	if (editor.currPaletteEdit == 4)
	{
		// get 8-bit RGB values and convert to 6-bit
		r = P8_TO_P6(RGB_R(video.palette[PAL_DESKTOP]));
		g = P8_TO_P6(RGB_G(video.palette[PAL_DESKTOP]));
		b = P8_TO_P6(RGB_B(video.palette[PAL_DESKTOP]));

		fContrast = editor.ui.desktopContrast / 40.0f;

		// generate shade
		newR = palMax(roundf(r * palPow(0.5f, fContrast)));
		newG = palMax(roundf(g * palPow(0.5f, fContrast)));
		newB = palMax(roundf(b * palPow(0.5f, fContrast)));
		config.palDesktop2R = (uint8_t)(newR);
		config.palDesktop2G = (uint8_t)(newG);
		config.palDesktop2B = (uint8_t)(newB);

		// convert 6-bit RGB values to 24-bit RGB
		video.palette[PAL_DSKTOP2] = (PAL_DSKTOP2 << 24) | TO_RGB(P6_TO_P8(newR), P6_TO_P8(newG), P6_TO_P8(newB));

		// generate shade
		newR = palMax(roundf(r * palPow(1.5f, fContrast)));
		newG = palMax(roundf(g * palPow(1.5f, fContrast)));
		newB = palMax(roundf(b * palPow(1.5f, fContrast)));

		config.palDesktop1R = (uint8_t)(newR);
		config.palDesktop1G = (uint8_t)(newG);
		config.palDesktop1B = (uint8_t)(newB);

		// convert 6-bit RGB values to 24-bit RGB
		video.palette[PAL_DSKTOP1] = (PAL_DSKTOP1 << 24) | TO_RGB(P6_TO_P8(newR), P6_TO_P8(newG), P6_TO_P8(newB));
		video.customPaletteContrasts[0] = editor.ui.desktopContrast;
	}
	else if (editor.currPaletteEdit == 5)
	{
		// get 8-bit RGB values and convert to 6-bit
		r = P8_TO_P6(RGB_R(video.palette[PAL_BUTTONS]));
		g = P8_TO_P6(RGB_G(video.palette[PAL_BUTTONS]));
		b = P8_TO_P6(RGB_B(video.palette[PAL_BUTTONS]));

		fContrast = editor.ui.buttonContrast / 40.0f;

		// generate shade
		newR = palMax(roundf(r * palPow(0.5f, fContrast)));
		newG = palMax(roundf(g * palPow(0.5f, fContrast)));
		newB = palMax(roundf(b * palPow(0.5f, fContrast)));
		config.palButtons2R = (uint8_t)(newR);
		config.palButtons2G = (uint8_t)(newG);
		config.palButtons2B = (uint8_t)(newB);

		// convert 6-bit RGB values to 24-bit RGB
		video.palette[PAL_BUTTON2] = (PAL_BUTTON2 << 24) | TO_RGB(P6_TO_P8(newR), P6_TO_P8(newG), P6_TO_P8(newB));

		// generate shade
		newR = palMax(roundf(r * palPow(1.5f, fContrast)));
		newG = palMax(roundf(g * palPow(1.5f, fContrast)));
		newB = palMax(roundf(b * palPow(1.5f, fContrast)));
		config.palButtons1R = (uint8_t)(newR);
		config.palButtons1G = (uint8_t)(newG);
		config.palButtons1B = (uint8_t)(newB);

		// convert 6-bit RGB values to 24-bit RGB
		video.palette[PAL_BUTTON1] = (PAL_BUTTON1 << 24) | TO_RGB(P6_TO_P8(newR), P6_TO_P8(newG), P6_TO_P8(newB));
		video.customPaletteContrasts[1] = editor.ui.buttonContrast;
	}
}

static void changePaletteTempEntry(uint8_t paletteEntry, uint8_t r6, uint8_t g6, uint8_t b6)
{
	assert(paletteEntry < PAL_NUM);

	if (r6 > 0x3F) r6 = 0x3F;
	if (g6 > 0x3F) g6 = 0x3F;
	if (b6 > 0x3F) b6 = 0x3F;

	paletteTemp[paletteEntry] = (paletteEntry << 24) | TO_RGB(P6_TO_P8(r6), P6_TO_P8(g6), P6_TO_P8(b6));
}

void setPalettePreset(int16_t palNum)
{
	uint8_t palPattTextR, palPattTextG, palPattTextB;
	uint8_t palBlockMarkR, palBlockMarkG, palBlockMarkB;
	uint8_t palTextOnBlockR, palTextOnBlockG, palTextOnBlockB;
	uint8_t palDesktopR, palDesktopG, palDesktopB;
	uint8_t palButtonsR, palButtonsG, palButtonsB;
	uint8_t palDesktop2R, palDesktop2G, palDesktop2B;
	uint8_t palDesktop1R, palDesktop1G, palDesktop1B;
	uint8_t palButtons2R, palButtons2G, palButtons2B;
	uint8_t palButtons1R, palButtons1G, palButtons1B;
	uint8_t palMouseR, palMouseG, palMouseB;

	if (palNum >= PAL_USER_DEFINED)
	{
		palPattTextR    = config.palPattTextR;    palPattTextG    = config.palPattTextG;    palPattTextB    = config.palPattTextB;
		palBlockMarkR   = config.palBlockMarkR;   palBlockMarkG   = config.palBlockMarkG;   palBlockMarkB   = config.palBlockMarkB;
		palTextOnBlockR = config.palTextOnBlockR; palTextOnBlockG = config.palTextOnBlockG; palTextOnBlockB = config.palTextOnBlockB;
		palDesktopR     = config.palDesktopR;     palDesktopG     = config.palDesktopG;     palDesktopB     = config.palDesktopB;
		palButtonsR     = config.palButtonsR;     palButtonsG     = config.palButtonsG;     palButtonsB     = config.palButtonsB;
		palDesktop2R    = config.palDesktop2R;    palDesktop2G    = config.palDesktop2G;    palDesktop2B    = config.palDesktop2B;
		palDesktop1R    = config.palDesktop1R;    palDesktop1G    = config.palDesktop1G;    palDesktop1B    = config.palDesktop1B;
		palButtons2R    = config.palButtons2R;    palButtons2G    = config.palButtons2G;    palButtons2B    = config.palButtons2B;
		palButtons1R    = config.palButtons1R;    palButtons1G    = config.palButtons1G;    palButtons1B    = config.palButtons1B;
		palMouseR       = config.palMouseR;       palMouseG       = config.palMouseG;       palMouseB       = config.palMouseB;

		editor.ui.desktopContrast = video.customPaletteContrasts[0];
		editor.ui.buttonContrast  = video.customPaletteContrasts[1];
	}
	else
	{
		palPattTextR    = palTable[palNum][1].r;  palPattTextG    = palTable[palNum][1].g;  palPattTextB    = palTable[palNum][1].b;
		palBlockMarkR   = palTable[palNum][2].r;  palBlockMarkG   = palTable[palNum][2].g;  palBlockMarkB   = palTable[palNum][2].b;
		palTextOnBlockR = palTable[palNum][3].r;  palTextOnBlockG = palTable[palNum][3].g;  palTextOnBlockB = palTable[palNum][3].b;
		palDesktopR     = palTable[palNum][4].r;  palDesktopG     = palTable[palNum][4].g;  palDesktopB     = palTable[palNum][4].b;
		palButtonsR     = palTable[palNum][6].r;  palButtonsG     = palTable[palNum][6].g;  palButtonsB     = palTable[palNum][6].b;
		palDesktop2R    = palTable[palNum][8].r;  palDesktop2G    = palTable[palNum][8].g;  palDesktop2B    = palTable[palNum][8].b;
		palDesktop1R    = palTable[palNum][9].r;  palDesktop1G    = palTable[palNum][9].g;  palDesktop1B    = palTable[palNum][9].b;
		palButtons2R    = palTable[palNum][10].r; palButtons2G    = palTable[palNum][10].g; palButtons2B    = palTable[palNum][10].b;
		palButtons1R    = palTable[palNum][11].r; palButtons1G    = palTable[palNum][11].g; palButtons1B    = palTable[palNum][11].b;
		palMouseR       = palTable[palNum][12].r; palMouseG       = palTable[palNum][12].g; palMouseB       = palTable[palNum][12].b;
	}

	// these can never change, so set them up like this
	paletteTemp[PAL_BCKGRND] = (PAL_BCKGRND << 24) | 0x000000;
	paletteTemp[PAL_FORGRND] = (PAL_FORGRND << 24) | 0xFFFFFF;
	paletteTemp[PAL_BTNTEXT] = (PAL_BTNTEXT << 24) | 0x000000;
	paletteTemp[PAL_TEXTMRK] = (PAL_TEXTMRK << 24) | 0x0078D7;

	changePaletteTempEntry(PAL_PATTEXT, palPattTextR,    palPattTextG,    palPattTextB);
	changePaletteTempEntry(PAL_BLCKMRK, palBlockMarkR,   palBlockMarkG,   palBlockMarkB);
	changePaletteTempEntry(PAL_BLCKTXT, palTextOnBlockR, palTextOnBlockG, palTextOnBlockB);
	changePaletteTempEntry(PAL_DESKTOP, palDesktopR,     palDesktopG,     palDesktopB);
	changePaletteTempEntry(PAL_BUTTONS, palButtonsR,     palButtonsG,     palButtonsB);
	changePaletteTempEntry(PAL_DSKTOP2, palDesktop2R,    palDesktop2G,    palDesktop2B);
	changePaletteTempEntry(PAL_DSKTOP1, palDesktop1R,    palDesktop1G,    palDesktop1B);
	changePaletteTempEntry(PAL_BUTTON2, palButtons2R,    palButtons2G,    palButtons2B);
	changePaletteTempEntry(PAL_BUTTON1, palButtons1R,    palButtons1G,    palButtons1B);
	changePaletteTempEntry(PAL_MOUSEPT, palMouseR,       palMouseG,       palMouseB);

	// set new palette
	memcpy(video.palette, paletteTemp, sizeof (video.palette));
	updateLoopPinPalette();

	if (video.frameBuffer != NULL) // this routine may be called before video is up
	{
		showTopScreen(false);
		showBottomScreen();
	}
}

bool setupSprites(void)
{
	uint8_t i;
	sprite_t *s;

	memset(sprites, 0, sizeof (sprites));

	s = &sprites[SPRITE_MOUSE_POINTER];
	s->data = mouseCursors;
	s->w = MOUSE_CURSOR_W;
	s->h = MOUSE_CURSOR_H;

	s = &sprites[SPRITE_LEFT_LOOP_PIN];
	s->data = leftLoopPinUnclicked;
	s->w = 16;
	s->h = SAMPLE_AREA_HEIGHT;

	s = &sprites[SPRITE_RIGHT_LOOP_PIN];
	s->data = rightLoopPinUnclicked;
	s->w = 16;
	s->h = SAMPLE_AREA_HEIGHT;

	s = &sprites[SPRITE_TEXT_CURSOR];
	s->data = textCursorData;
	s->w = 1;
	s->h = 12;

	hideSprite(SPRITE_MOUSE_POINTER);
	hideSprite(SPRITE_LEFT_LOOP_PIN);
	hideSprite(SPRITE_RIGHT_LOOP_PIN);
	hideSprite(SPRITE_TEXT_CURSOR);

	// setup refresh buffer (used to clear sprites after each frame)
	for (i = 0; i < SPRITE_NUM; ++i)
	{
		sprites[i].refreshBuffer = (uint32_t *)(malloc((sprites[i].w * sprites[i].h) * sizeof (int32_t)));
		if (sprites[i].refreshBuffer == NULL)
			return (false);
	}

	return (true);
}

void changeSpriteData(uint8_t sprite, const uint8_t *data)
{
	sprites[sprite].data = data;
	memset(sprites[sprite].refreshBuffer, 0, sprites[sprite].w * sprites[sprite].h * sizeof (int32_t));
}

void freeSprites(void)
{
	uint8_t i;

	for (i = 0; i < SPRITE_NUM; ++i)
	{
		if (sprites[i].refreshBuffer != NULL)
		{
			free(sprites[i].refreshBuffer);
			sprites[i].refreshBuffer = NULL;
		}
	}
}

void setLeftLoopPinState(bool clicked)
{
	changeSpriteData(SPRITE_LEFT_LOOP_PIN, clicked ? leftLoopPinClicked : leftLoopPinUnclicked);
}

void setRightLoopPinState(bool clicked)
{
	changeSpriteData(SPRITE_RIGHT_LOOP_PIN, clicked ? rightLoopPinClicked : rightLoopPinUnclicked);
}

int32_t getSpritePosX(uint8_t sprite)
{
	return (sprites[sprite].x);
}

void setSpritePos(uint8_t sprite, int16_t x, int16_t y)
{
	sprites[sprite].newX = x;
	sprites[sprite].newY = y;
}

void hideSprite(uint8_t sprite)
{
	sprites[sprite].newX = SCREEN_W;
}

void eraseSprites(void)
{
	int8_t i;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	const uint32_t *src32;
	uint32_t *dst32;
	sprite_t *s;

	for (i = (SPRITE_NUM - 1); i >= 0; --i) // erasing must be done in reverse order
	{
		s = &sprites[i];

		if (s->x >= SCREEN_W) // sprite is hidden, don't erase
			continue;

		assert((s->y >= 0) && (s->refreshBuffer != NULL));

		sw = s->w;
		sh = s->h;
		x  = s->x;
		y  = s->y;

		// if x is negative, adjust variables (can only happen on loop pins in smp. ed.)
		if (x < 0)
		{
			sw += x; // subtraction
			x   = 0;
		}

		src32 = s->refreshBuffer;
		dst32 = &video.frameBuffer[(y * SCREEN_W) + x];

		if ((y + sh) >= SCREEN_H) sh = SCREEN_H - y;
		if ((x + sw) >= SCREEN_W) sw = SCREEN_W - x;

		srcPitch = s->w     - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; ++y)
		{
			for (x = 0; x < sw; ++x)
				*dst32++ = *src32++;

			src32 += srcPitch;
			dst32 += dstPitch;
		}
	}
}

void renderSprites(void)
{
	const uint8_t *src8;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	uint32_t i, *clr32, *dst32, windowFlags;
	sprite_t *s;

	for (i = 0; i < SPRITE_NUM; ++i)
	{
		if ((i == SPRITE_LEFT_LOOP_PIN) || (i == SPRITE_RIGHT_LOOP_PIN))
			continue; // these need special drawing (done elsewhere)

		// don't render the text edit cursor if window is inactive
		if (i == SPRITE_TEXT_CURSOR)
		{
			assert(video.window != NULL);

			windowFlags = SDL_GetWindowFlags(video.window);
			if (!(windowFlags & SDL_WINDOW_INPUT_FOCUS))
				continue;
		}

		s = &sprites[i];

		// set new sprite position
		s->x = s->newX;
		s->y = s->newY;

		if (s->x >= SCREEN_W) // sprite is hidden, don't draw nor fill clear buffer
			continue;

		assert((s->x >= 0) && (s->y >= 0) && (s->data != NULL) && (s->refreshBuffer != NULL));

		sw    = s->w;
		sh    = s->h;
		src8  = s->data;
		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + s->x];
		clr32 = s->refreshBuffer;

		// handle xy clipping
		if ((s->y + sh) >= SCREEN_H) sh = SCREEN_H - s->y;
		if ((s->x + sw) >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w     - sw;
		dstPitch = SCREEN_W - sw;

		if (mouse.mouseOverTextBox && (i == SPRITE_MOUSE_POINTER))
		{
			// text edit mouse pointer (has color changing depending on content under it)

			for (y = 0; y < sh; ++y)
			{
				for (x = 0; x < sw; ++x)
				{
					*clr32++ = *dst32; // fill clear buffer

					if (*src8 != PAL_TRANSPR)
					{
						if (!(*dst32 & 0x00FFFFFF) || (*dst32 == video.palette[PAL_TEXTMRK]))
							*dst32 = 0xB3DBF6;
						else
							*dst32 = 0x004ECE;
					}

					dst32++;
					src8++;
				}

				clr32 += srcPitch;
				src8  += srcPitch;
				dst32 += dstPitch;
			}
		}
		else
		{
			// normal sprites

			for (y = 0; y < sh; ++y)
			{
				for (x = 0; x < sw; ++x)
				{
					*clr32++ = *dst32; // fill clear buffer

					if (*src8 != PAL_TRANSPR)
					{
						assert(*src8 < PAL_NUM);
						*dst32 = video.palette[*src8];
					}

					dst32++;
					src8++;
				}

				clr32 += srcPitch;
				src8  += srcPitch;
				dst32 += dstPitch;
			}
		}
	}
}

void renderLoopPins(void)
{
	uint8_t pal;
	const uint8_t *src8;
	int32_t sx;
	register int32_t x, y, sw, sh, srcPitch, dstPitch;
	uint32_t *clr32, *dst32;
	sprite_t *s;

	// left loop pin

	s = &sprites[SPRITE_LEFT_LOOP_PIN];
	assert((s->data != NULL) && (s->refreshBuffer != NULL));

	// set new sprite position
	s->x = s->newX;
	s->y = s->newY;

	if (s->x < SCREEN_W) // loop pin shown?
	{
		sw = s->w;
		sh = s->h;
		sx = s->x;

		src8  = s->data;
		clr32 = s->refreshBuffer;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw   += sx; // subtraction
			src8 -= sx; // addition
			sx    = 0;
		}

		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + sx];

		// handle x clipping
		if ((s->x + sw) >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w     - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; ++y)
		{
			for (x = 0; x < sw; ++x)
			{
				*clr32++ = *dst32; // fill clear buffer

				if (*src8 != PAL_TRANSPR)
				{
					assert(*src8 < PAL_NUM);
					*dst32 = video.palette[*src8];
				}

				dst32++;
				src8++;
			}

			src8  += srcPitch;
			clr32 += srcPitch;
			dst32 += dstPitch;
		}
	}

	// right loop pin

	s = &sprites[SPRITE_RIGHT_LOOP_PIN];
	assert((s->data != NULL) && (s->refreshBuffer != NULL));

	// set new sprite position
	s->x = s->newX;
	s->y = s->newY;

	if (s->x < SCREEN_W) // loop pin shown?
	{
		s->x = s->newX;
		s->y = s->newY;

		sw = s->w;
		sh = s->h;
		sx = s->x;

		src8  = s->data;
		clr32 = s->refreshBuffer;

		// if x is negative, adjust variables
		if (sx < 0)
		{
			sw   += sx; // subtraction
			src8 -= sx; // addition
			sx    = 0;
		}

		dst32 = &video.frameBuffer[(s->y * SCREEN_W) + sx];

		// handle x clipping
		if ((s->x + sw) >= SCREEN_W) sw = SCREEN_W - s->x;

		srcPitch = s->w     - sw;
		dstPitch = SCREEN_W - sw;

		for (y = 0; y < sh; ++y)
		{
			for (x = 0; x < sw; ++x)
			{
				*clr32++ = *dst32;

				if (*src8 != PAL_TRANSPR)
				{
					if ((y < 9) && (*src8 == PAL_LOOPPIN))
					{
						// don't draw marker line on top of left loop pin's thumb graphics

						pal = *dst32 >> 24;
						if ((pal != PAL_DESKTOP) && (pal != PAL_DSKTOP1) && (pal != PAL_DSKTOP2))
						{
							assert(*src8 < PAL_NUM);
							*dst32 = video.palette[*src8];
						}
					}
					else
					{
						assert(*src8 < PAL_NUM);
						*dst32 = video.palette[*src8];
					}
				}

				dst32++;
				src8++;
			}

			src8  += srcPitch;
			clr32 += srcPitch;
			dst32 += dstPitch;
		}
	}
}

void setupWaitVBL(void)
{
	// set next frame time
	timeNext64     = SDL_GetPerformanceCounter() + video.vblankTimeLen;
	timeNext64Frac = video.vblankTimeLenFrac;
}

void waitVBL(void)
{
	// this routine almost never delays if we have 60Hz vsync, but it's still needed in some occasions

	int32_t time32;
	uint32_t diff32;
	uint64_t time64;
	double dTime;

	time64 = SDL_GetPerformanceCounter();
	if (time64 < timeNext64)
	{
		assert((timeNext64 - time64) <= 0xFFFFFFFFULL);
		diff32 = (uint32_t)(timeNext64 - time64);

		// convert and round to microseconds
		dTime = diff32 * editor.dPerfFreqMulMicro;

		if (cpu.hasSSE2)
			sse2_double2int32_round(time32, dTime);
		else
			time32 = (int32_t)(round(dTime));

		// delay until we have reached next frame
		if (time32 > 0)
			usleep(time32);
	}

	// update next frame time

	timeNext64 += video.vblankTimeLen;

	timeNext64Frac += video.vblankTimeLenFrac;
	if (timeNext64Frac >= (1ULL << 32))
	{
		timeNext64++;
		timeNext64Frac &= 0xFFFFFFFF;
	}
}

void closeVideo(void)
{
	if (video.texture != NULL)
	{
		SDL_DestroyTexture(video.texture);
		video.texture = NULL;
	}

	if (video.renderer != NULL)
	{
		SDL_DestroyRenderer(video.renderer);
		video.renderer = NULL;
	}

	if (video.window != NULL)
	{
		SDL_DestroyWindow(video.window);
		video.window = NULL;
	}

	if (video.frameBuffer != NULL)
	{
		free(video.frameBuffer);
		video.frameBuffer = NULL;
	}
}

void setWindowSizeFromConfig(bool updateRenderer)
{
#define MAX_UPSCALE_FACTOR 16 // 10112x6400 - ought to be good enough for many years to come

	uint8_t i, oldUpscaleFactor;
	SDL_DisplayMode dm;

	oldUpscaleFactor = video.upscaleFactor;
	if (config.windowFlags & WINSIZE_AUTO)
	{
		// find out which upscaling factor is the biggest to fit on screen
		if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
		{
			for (i = MAX_UPSCALE_FACTOR; i >= 1; --i)
			{
				// slightly bigger than 632x400 because of window title, window borders and taskbar/menu
				if ((dm.w >= (640 * i)) && (dm.h >= (450 * i)))
				{
					video.upscaleFactor = i;
					break;
				}
			}

			if (i == 0)
				video.upscaleFactor = 1; // 1x is not going to fit, but use 1x anyways...
		}
		else
		{
			// couldn't get screen resolution, set to 1x
			video.upscaleFactor = 1;
		}
	}
	else if (config.windowFlags & WINSIZE_1X) video.upscaleFactor = 1;
	else if (config.windowFlags & WINSIZE_2X) video.upscaleFactor = 2;
	else if (config.windowFlags & WINSIZE_3X) video.upscaleFactor = 3;
	else if (config.windowFlags & WINSIZE_4X) video.upscaleFactor = 4;

	if (updateRenderer)
	{
		SDL_SetWindowSize(video.window, SCREEN_W * video.upscaleFactor, SCREEN_H * video.upscaleFactor);

		if (oldUpscaleFactor != video.upscaleFactor)
			SDL_SetWindowPosition(video.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

		updateRenderSizeVars();
		updateMouseScaling();
	}
}

void updateWindowTitle(bool forceUpdate)
{
	char wndTitle[128 + PATH_MAX];
	char *songTitle;

	if (!forceUpdate && (songIsModified == song.isModified))
		return; // window title is already set to the same

	songTitle = getCurrSongFilename();
	if (songTitle != NULL)
	{
		if (song.isModified)
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"%s\" (unsaved)", BETA_VERSION, songTitle);
		else
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"%s\"", BETA_VERSION, songTitle);
	}
	else
	{
		if (song.isModified)
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"untitled\" (unsaved)", BETA_VERSION);
		else
			sprintf(wndTitle, "Fasttracker II clone (beta #%d) - \"untitled\"", BETA_VERSION);
	}

	SDL_SetWindowTitle(video.window, wndTitle);
	songIsModified = song.isModified;
}

bool recreateTexture(void)
{
	if (video.texture != NULL)
	{
		SDL_DestroyTexture(video.texture);
		video.texture = NULL;
	}

	if (config.windowFlags & FILTERING)
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "best");
	else
		SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "nearest");

	video.texture = SDL_CreateTexture(video.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	if (video.texture == NULL)
	{
		showErrorMsgBox("Couldn't create a %dx%d GPU texture:\n%s\n\nIs your GPU (+ driver) too old?", SCREEN_W, SCREEN_H, SDL_GetError());
		return (false);
	}

	SDL_SetTextureBlendMode(video.texture, SDL_BLENDMODE_NONE);
	return (true);
}

bool setupWindow(void)
{
	uint32_t windowFlags;
	SDL_DisplayMode dm;

	video.vsync60HzPresent = false;
	windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;

	setWindowSizeFromConfig(false);

#if SDL_PATCHLEVEL >= 5 // SDL 2.0.5 or later
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

	SDL_GetDesktopDisplayMode(0, &dm);
	if ((dm.refresh_rate == 59) || (dm.refresh_rate == 60)) // both are the same
		video.vsync60HzPresent = true;

	if (config.windowFlags & FORCE_VSYNC_OFF)
		video.vsync60HzPresent = false;

	video.window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
					   SCREEN_W * video.upscaleFactor, SCREEN_H * video.upscaleFactor,
					   windowFlags);

	if (video.window == NULL)
	{
		showErrorMsgBox("Couldn't create SDL window:\n%s", SDL_GetError());
		return (false);
	}

	updateWindowTitle(true);
	return (true);
}

bool setupRenderer(void)
{
	uint32_t rendererFlags;

	rendererFlags = 0;
	if (video.vsync60HzPresent)
		rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

	video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
	if (video.renderer == NULL)
	{
		if (video.vsync60HzPresent)
		{
			// try again without vsync flag
			video.vsync60HzPresent = false;

			rendererFlags &= ~SDL_RENDERER_PRESENTVSYNC;
			video.renderer = SDL_CreateRenderer(video.window, -1, rendererFlags);
		}

		if (video.renderer == NULL)
		{
			showErrorMsgBox("Couldn't create SDL renderer:\n%s\n\nIs your GPU (+ driver) too old?",
				SDL_GetError());
			return (false);
		}
	}

	SDL_RenderSetLogicalSize(video.renderer, SCREEN_W, SCREEN_H);

#if SDL_PATCHLEVEL >= 5
	SDL_RenderSetIntegerScale(video.renderer, SDL_TRUE);
#endif

	SDL_SetRenderDrawBlendMode(video.renderer, SDL_BLENDMODE_NONE);

	if (!recreateTexture())
	{
		showErrorMsgBox("Couldn't create a %dx%d GPU texture:\n%s\n\nIs your GPU (+ driver) too old?",
			SCREEN_W, SCREEN_H, SDL_GetError());
		return (false);
	}

	// framebuffer used by SDL (for texture)
	video.frameBuffer = (uint32_t *)(malloc(SCREEN_W * SCREEN_H * sizeof (int32_t)));
	if (video.frameBuffer == NULL)
	{
		showErrorMsgBox("Not enough memory!");
		return (false);
	}

	if (!setupSprites())
		return (false);

	updateRenderSizeVars();
	updateMouseScaling();

	SDL_ShowCursor(SDL_FALSE);
	return (true);
}

void handleRedrawing(void)
{
	textBox_t *txt;

	if (!editor.ui.configScreenShown && !editor.ui.helpScreenShown)
	{
		if (editor.ui.aboutScreenShown)
		{
			aboutFrame();
		}
		else if (editor.ui.nibblesShown)
		{
			if (editor.NI_Play)
				moveNibblePlayers();
		}
		else
		{
			if (editor.ui.updatePosSections)
			{
				editor.ui.updatePosSections = false;

				if (!editor.ui.diskOpShown)
				{
					drawSongRepS();
					drawSongLength();
					drawPosEdNums(editor.songPos);
					drawEditPattern(editor.editPattern);
					drawPatternLength(editor.editPattern);
					drawSongBPM(editor.speed);
					drawSongSpeed(editor.tempo);
					drawGlobalVol(editor.globalVol);

					if (!songPlaying || editor.wavIsRendering)
						setScrollBarPos(SB_POS_ED, editor.songPos, false);

					// draw current mode text (not while in extended pattern editor mode)
					if (!editor.ui.extended)
					{
						fillRect(115, 80, 74, 10, PAL_DESKTOP);

							 if (playMode == PLAYMODE_PATT)    textOut(115, 80, PAL_FORGRND, "> Play ptn. <");
						else if (playMode == PLAYMODE_EDIT)    textOut(121, 80, PAL_FORGRND, "> Editing <");
						else if (playMode == PLAYMODE_RECSONG) textOut(114, 80, PAL_FORGRND, "> Rec. sng. <");
						else if (playMode == PLAYMODE_RECPATT) textOut(115, 80, PAL_FORGRND, "> Rec. ptn. <");
					}
				}
			}

			if (!editor.ui.extended)
			{
				if (!editor.ui.diskOpShown)
					drawPlaybackTime();

					 if (editor.ui.sampleEditorExtShown) handleSampleEditorExtRedrawing();
				else if (editor.ui.scopesShown)          drawScopes();
			}
		}
	}

	drawReplayerData();

		 if (editor.ui.instEditorShown)   handleInstEditorRedrawing();
	else if (editor.ui.sampleEditorShown) handleSamplerRedrawing();

	// blink text edit cursor
	if (editor.editTextFlag && (mouse.lastEditBox != -1))
	{
		assert((mouse.lastEditBox >= 0) && (mouse.lastEditBox < NUM_TEXTBOXES));

		txt = &textBoxes[mouse.lastEditBox];
		if ((editor.textCursorBlinkCounter < (256 / 2)) && !textIsMarked() && !(mouse.leftButtonPressed | mouse.rightButtonPressed))
			setSpritePos(SPRITE_TEXT_CURSOR, getTextCursorX(txt), getTextCursorY(txt) - 1); // show text cursor
		else
			hideSprite(SPRITE_TEXT_CURSOR); // hide text cursor

		editor.textCursorBlinkCounter += TEXT_CURSOR_BLINK_RATE;
	}

	if (editor.busy)
		animateBusyMouse();

	renderLoopPins();
}

static void drawReplayerData(void)
{
	bool drawPosText;

	if (songPlaying)
	{
		if (editor.ui.drawReplayerPianoFlag)
		{
			editor.ui.drawReplayerPianoFlag = false;

			if (editor.ui.instEditorShown)
			{
				if (editor.wavIsRendering)
					drawPiano();
				else if (chSyncEntry != NULL)
					drawPianoReplayer(chSyncEntry);
			}
		}

		drawPosText = true;
		if (editor.ui.configScreenShown || editor.ui.nibblesShown     ||
			editor.ui.helpScreenShown   || editor.ui.aboutScreenShown ||
			editor.ui.diskOpShown)
		{
			drawPosText = false;
		}

		if (drawPosText)
		{
			if (editor.ui.drawBPMFlag)
			{
				editor.ui.drawBPMFlag = false;
				drawSongBPM(editor.speed);
			}
			
			if (editor.ui.drawSpeedFlag)
			{
				editor.ui.drawSpeedFlag = false;
				drawSongSpeed(editor.tempo);
			}

			if (editor.ui.drawGlobVolFlag)
			{
				editor.ui.drawGlobVolFlag = false;
				drawGlobalVol(editor.globalVol);
			}

			if (editor.ui.drawPosEdFlag)
			{
				editor.ui.drawPosEdFlag = false;
				drawPosEdNums(editor.songPos);
				setScrollBarPos(SB_POS_ED, editor.songPos, false);
			}

			if (editor.ui.drawPattNumLenFlag)
			{
				editor.ui.drawPattNumLenFlag = false;
				drawEditPattern(editor.editPattern);
				drawPatternLength(editor.editPattern);
			}
		}
	}
	else if (editor.ui.instEditorShown)
	{
		drawPiano();
	}

	// handle pattern data updates
	if (editor.ui.updatePatternEditor)
	{
		editor.ui.updatePatternEditor = false;
		if (editor.ui.patternEditorShown)
			writePattern(editor.pattPos, editor.editPattern);
	}
}

const uint8_t textCursorData[12] =
{
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND,
	PAL_FORGRND, PAL_FORGRND, PAL_FORGRND
};

const pal16 palTable[12][13] =
{
	{
		{0, 0, 0},{30, 38, 63},{0, 0, 17},{63, 63, 63},
		{27, 36, 40},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{10, 13, 14},{49, 63, 63},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{21, 40, 63},{0, 0, 17},{63, 63, 63},
		{6, 39, 35},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{2, 14, 13},{11, 63, 63},{16, 16, 16},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{39, 52, 63},{8, 8, 13},{57, 57, 63},
		{10, 21, 33},{63, 63, 63},{37, 37, 45},{0, 0, 0},
		{4, 8, 13},{18, 37, 58},{13, 13, 16},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{47, 47, 47},{9, 9, 9},{63, 63, 63},
		{37, 29, 7},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{11, 9, 2},{63, 58, 14},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{46, 45, 46},{13, 9, 9},{63, 63, 63},
		{22, 19, 22},{63, 63, 63},{36, 32, 34},{0, 0, 0},
		{8, 7, 8},{39, 34, 39},{13, 12, 12},{63, 58, 62},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{19, 49, 54},{0, 11, 7},{52, 63, 61},
		{9, 31, 21},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{4, 13, 9},{15, 50, 34},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{27, 37, 53},{0, 0, 20},{63, 63, 63},
		{7, 12, 21},{63, 63, 63},{38, 39, 39},{0, 0, 0},
		{2, 4, 7},{14, 23, 41},{13, 13, 13},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{63, 54, 62},{18, 3, 3},{63, 63, 63},
		{36, 19, 25},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{11, 6, 8},{63, 38, 50},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{63, 0, 63},{0, 21, 0},{63, 44, 0},
		{0, 63, 0},{63, 63, 63},{63, 0, 0},{0, 0, 0},
		{0, 28, 0},{0, 63, 0},{23, 0, 0},{63, 0, 0},
		{0, 63, 63},
	},
	{
		{0, 0, 0},{50, 46, 63},{15, 0, 16},{59, 58, 63},
		{34, 21, 41},{63, 63, 63},{40, 40, 40},{0, 0, 0},
		{13, 8, 15},{61, 37, 63},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{63, 63, 32},{10, 10, 10},{63, 63, 63},
		{18, 29, 32},{63, 63, 63},{39, 39, 39},{0, 0, 0},
		{6, 10, 11},{34, 54, 60},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	},
	{
		{0, 0, 0},{36, 47, 63},{9, 9, 16},{63, 63, 63},
		{19, 24, 38},{63, 63, 63},{39, 39, 39},{0, 0, 0},
		{8, 10, 15},{32, 41, 63},{15, 15, 15},{63, 63, 63},
		{63, 63, 63},
	}
};
