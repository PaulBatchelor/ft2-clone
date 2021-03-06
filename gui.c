// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <time.h>
#include "header.h"
#include "gfxdata.h"
#include "config.h"
#include "about.h"
#include "mouse.h"
#include "nibbles.h"
#include "gui.h"
#include "pattern_ed.h"
#include "scopes.h"
#include "help.h"
#include "sample_ed.h"
#include "inst_ed.h"
#include "diskop.h"
#include "wav_renderer.h"
#include "trim.h"
#include "video.h"

void unstuckAllGUIElements(void) // releases all GUI elements if they were held down/used
{
	int16_t i;

	if (mouse.lastUsedObjectID == OBJECT_ID_NONE)
		return; // nothing to unstuck

	mouse.lastUsedObjectID   = OBJECT_ID_NONE;
	mouse.lastUsedObjectType = OBJECT_NONE;

	for (i = 0; i < NUM_RADIOBUTTONS; ++i)
	{
		if (radioButtons[i].state == RADIOBUTTON_PRESSED)
		{
			radioButtons[i].state = RADIOBUTTON_UNCHECKED;
			if (radioButtons[i].visible)
				drawRadioButton(i);
		}
	}

	for (i = 0; i < NUM_CHECKBOXES; ++i)
	{
		if (checkBoxes[i].state == CHECKBOX_PRESSED)
		{
			checkBoxes[i].state = CHECKBOX_UNPRESSED;
			if (checkBoxes[i].visible)
				drawCheckBox(i);
		}
	}

	for (i = 0; i < NUM_PUSHBUTTONS; ++i)
	{
		if (pushButtons[i].state == PUSHBUTTON_PRESSED)
		{
			pushButtons[i].state = PUSHBUTTON_UNPRESSED;
			if (pushButtons[i].visible)
				drawPushButton(i);
		}
	}

	for (i = 0; i < NUM_SCROLLBARS; ++i)
	{
		scrollBars[i].state = SCROLLBAR_UNPRESSED;
		if (scrollBars[i].visible)
			drawScrollBar(i);
	}
}

bool setupGUI(void)
{
	int16_t i;
	textBox_t *t;
	pushButton_t *p;
	checkBox_t *c;
	radioButton_t *r;
	scrollBar_t *s;

	// all memory will be NULL-tested and free'd if we return false somewhere in this function

	editor.tmpFilenameU      = (UNICHAR *)(calloc(PATH_MAX + 1,              sizeof (UNICHAR)));
	editor.tmpInstrFilenameU = (UNICHAR *)(calloc(PATH_MAX + 1,              sizeof (UNICHAR)));

	if ((editor.tmpFilenameU == NULL) || (editor.tmpInstrFilenameU == NULL))
		goto oom;

	// set uninitialized GUI struct entries

	for (i = 1; i < NUM_TEXTBOXES; ++i) // skip first entry, it's reserved for inputBox())
	{
		t = &textBoxes[i];

		t->visible    = false;
		t->bufOffset  = 0;
		t->cursorPos  = 0;
		t->textPtr    = NULL;
		t->renderBufW = (9 + 1) * t->maxChars; // 9 = max character/glyph width possible
		t->renderBufH = 10; // 10 = max character height possible
		t->renderW    = t->w - (t->tx * 2);

		t->renderBuf = (uint8_t *)(malloc(t->renderBufW * t->renderBufH * sizeof (int8_t)));
		if (t->renderBuf == NULL)
			goto oom;
	}

	for (i = 0; i < NUM_PUSHBUTTONS; ++i)
	{
		p = &pushButtons[i];

		p->state = 0;
		p->visible = false;

		if ((i == PB_LOGO) || (i == PB_BADGE))
		{
			p->bitmapFlag = true;
		}
		else
		{
			p->bitmapFlag = false;
			p->bitmapUnpressed = NULL;
			p->bitmapPressed = NULL;
		}
	}

	for (i = 0; i < NUM_CHECKBOXES; ++i)
	{
		c = &checkBoxes[i];

		c->state   = 0;
		c->checked = false;
		c->visible = false;
	}

	for (i = 0; i < NUM_RADIOBUTTONS; ++i)
	{
		r = &radioButtons[i];

		r->state   = 0;
		r->visible = false;
	}

	for (i = 0; i < NUM_SCROLLBARS; ++i)
	{
		s = &scrollBars[i];

		s->visible = false;
		s->state   = 0;
		s->pos     = 0;
		s->page    = 0;
		s->end     = 0;
		s->thumbX  = 0;
		s->thumbY  = 0;
		s->thumbW  = 0;
		s->thumbH  = 0;
	}

	seedAboutScreenRandom((uint32_t)(time(NULL)));
	setupInitialTextBoxPointers();
	setInitialTrimFlags();
	initializeScrollBars();
	setMouseMode(MOUSE_MODE_NORMAL);
	updateTextBoxPointers();
	drawGUIOnRunTime();
	updateSampleEditorSample();
	updatePatternWidth();
	initFTHelp();

	return (true);

oom:
	showErrorMsgBox("Not enough memory!");
	return (false);
}

// TEXT ROUTINES

// returns full pixel width of a char/glyph
uint8_t charWidth(char ch)
{
	uint8_t c;

	c = (uint8_t)(ch);
	if (c >= FONT_CHARS)
		return (8);

	return (font1Widths[c]);
}

// returns full pixel width of a char/glyph (big font)
uint8_t bigCharWidth(char ch)
{
	uint8_t c;

	c = (uint8_t)(ch);
	if (c >= FONT_CHARS)
		return (16);

	return (font2Widths[c]);
}

// return full pixel width of a text string
uint16_t textWidth(char *textPtr)
{
	uint16_t textWidth;

	assert(textPtr != NULL);

	textWidth = 0;
	while (*textPtr != '\0')
		textWidth += charWidth(*textPtr++);

	// there will be a pixel spacer at the end of the last char/glyph, remove it
	if (textWidth > 0)
		textWidth--;

	return (textWidth);
}

uint16_t textNWidth(char *textPtr, int32_t length)
{
	char ch;
	uint16_t textWidth;
	int32_t i;

	assert(textPtr != NULL);

	textWidth = 0;
	for (i = 0; i < length; ++i)
	{
		ch = textPtr[i];
		if (ch == '\0')
			break;

		textWidth += charWidth(ch);
	}

	// there will be a pixel spacer at the end of the last char/glyph, remove it
	if (textWidth > 0)
		textWidth--;

	return (textWidth);
}

// return full pixel width of a text string (big font)
uint16_t textBigWidth(char *textPtr)
{
	uint16_t textWidth;

	assert(textPtr != NULL);

	textWidth = 0;
	while (*textPtr != '\0')
		textWidth += charWidth(*textPtr++);

	// there will be a pixel spacer at the end of the last char/glyph, remove it
	if (textWidth > 0)
		textWidth--;

	return (textWidth);
}

void charOut(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, char chr)
{
	const uint8_t *srcPtr;
	uint8_t c, x, y;
	uint32_t *dstPtr, pixVal;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS))
		return;

	pixVal = video.palette[paletteIndex];
	srcPtr = &font1Data[c * FONT1_CHAR_W];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (y = 0; y < FONT1_CHAR_H; ++y)
	{
		for (x = 0; x < FONT1_CHAR_W; ++x)
		{
			if (srcPtr[x])
				dstPtr[x] = pixVal;
		}

		srcPtr += FONT1_WIDTH;
		dstPtr += SCREEN_W;
	}
}

void charOutBg(uint16_t xPos, uint16_t yPos, uint8_t fgPalette, uint8_t bgPalette, char chr)
{
	const uint8_t *srcPtr;
	uint8_t c, x, y;
	uint32_t *dstPtr, fg, bg;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS))
		return;

	fg = video.palette[fgPalette];
	bg = video.palette[bgPalette];

	srcPtr = &font1Data[c * FONT1_CHAR_W];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (y = 0; y < FONT1_CHAR_H; ++y)
	{
		for (x = 0; x < 7; ++x)
			dstPtr[x] = srcPtr[x] ? fg : bg;

		srcPtr += FONT1_WIDTH;
		dstPtr += SCREEN_W;
	}
}

void charOutOutlined(uint16_t x, uint16_t y, uint8_t paletteIndex, char chr)
{
	charOut(x - 1, y,     PAL_BCKGRND, chr);
	charOut(x + 1, y,     PAL_BCKGRND, chr);
	charOut(x,     y - 1, PAL_BCKGRND, chr);
	charOut(x,     y + 1, PAL_BCKGRND, chr);

	charOut(x, y, paletteIndex, chr);
}

void charOutShadow(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, uint8_t shadowPaletteIndex, char chr)
{
	const uint8_t *srcPtr;
	uint8_t c, x, y;
	uint32_t *dstPtr, pixVal1, pixVal2;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS))
		return;

	pixVal1 = video.palette[paletteIndex];
	pixVal2 = video.palette[shadowPaletteIndex];
	srcPtr  = &font1Data[c * FONT1_CHAR_W];
	dstPtr  = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (y = 0; y < FONT1_CHAR_H; ++y)
	{
		for (x = 0; x < FONT1_CHAR_W; ++x)
		{
			if (srcPtr[x])
			{
				dstPtr[x + (SCREEN_W + 1)] = pixVal2;
				dstPtr[x] = pixVal1;
			}
		}

		srcPtr += FONT1_WIDTH;
		dstPtr += SCREEN_W;
	}
}

void charOutClipX(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, char chr, uint16_t clipX)
{
	const uint8_t *srcPtr;
	uint8_t c;
	uint16_t x, y, width;
	uint32_t *dstPtr, pixVal;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS) || (xPos > clipX))
		return;

	pixVal = video.palette[paletteIndex];
	srcPtr = &font1Data[c * FONT1_CHAR_W];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	width = FONT1_CHAR_W;
	if ((xPos + width) > clipX)
		width = FONT1_CHAR_W - (((xPos + width) - clipX));

	for (y = 0; y < FONT1_CHAR_H; ++y)
	{
		for (x = 0; x < width; ++x)
		{
			if (srcPtr[x])
				dstPtr[x] = pixVal;
		}

		srcPtr += FONT1_WIDTH;
		dstPtr += SCREEN_W;
	}
}

void bigCharOut(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, char chr)
{
	const uint8_t *srcPtr;
	uint8_t c, x, y;
	uint32_t *dstPtr, pixVal;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS))
		return;

	srcPtr = &font2Data[c * FONT2_CHAR_W];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	pixVal = video.palette[paletteIndex];

	for (y = 0; y < FONT2_CHAR_H; ++y)
	{
		for (x = 0; x < FONT2_CHAR_W; ++x)
		{
			if (srcPtr[x])
				dstPtr[x] = pixVal;
		}

		srcPtr += FONT2_WIDTH;
		dstPtr += SCREEN_W;
	}
}

static void bigCharOutShadow(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, uint8_t shadowPaletteIndex, char chr)
{
	const uint8_t *srcPtr;
	uint8_t c, x, y;
	uint32_t *dstPtr, pixVal1, pixVal2;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	c = (uint8_t)(chr);
	if ((c == ' ') || (c >= FONT_CHARS))
		return;

	pixVal1 = video.palette[paletteIndex];
	pixVal2 = video.palette[shadowPaletteIndex];
	srcPtr  = &font2Data[c * FONT2_CHAR_W];
	dstPtr  = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (y = 0; y < FONT2_CHAR_H; ++y)
	{
		for (x = 0; x < FONT2_CHAR_W; ++x)
		{
			if (srcPtr[x])
			{
				dstPtr[x + (SCREEN_W + 1)] = pixVal2;
				dstPtr[x] = pixVal1;
			}
		}

		srcPtr += FONT2_WIDTH;
		dstPtr += SCREEN_W;
	}
}

void textOut(uint16_t x, uint16_t y, uint8_t paletteIndex, char *textPtr)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		charOut(currX, y, paletteIndex, c);
		currX += charWidth(c);
	}
}

// fixed width
void textOutFixed(uint16_t x, uint16_t y, uint8_t fgPaltete, uint8_t bgPalette, char *textPtr)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		charOutBg(currX, y, fgPaltete, bgPalette, c);
		currX += 7;
	}
}

void textOutShadow(uint16_t x, uint16_t y, uint8_t paletteIndex, uint8_t shadowPaletteIndex, char *textPtr)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		charOutShadow(currX, y, paletteIndex, shadowPaletteIndex, c);
		currX += charWidth(c);
	}
}

void bigTextOut(uint16_t x, uint16_t y, uint8_t paletteIndex, char *textPtr)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		bigCharOut(currX, y, paletteIndex, c);
		currX += bigCharWidth(c);
	}
}

void bigTextOutShadow(uint16_t x, uint16_t y, uint8_t paletteIndex, uint8_t shadowPaletteIndex, char *textPtr)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		bigCharOutShadow(currX, y, paletteIndex, shadowPaletteIndex, c);
		currX += bigCharWidth(c);
	}
}

void textOutClipX(uint16_t x, uint16_t y, uint8_t paletteIndex, char *textPtr, uint16_t clipX)
{
	uint8_t c;
	uint16_t currX;

	assert(textPtr != NULL);

	currX = x;
	while (true)
	{
		c = (uint8_t)(*textPtr++);
		if (c == '\0')
			break;

		charOutClipX(currX, y, paletteIndex, c, clipX);

		currX += charWidth(c);
		if (currX >= clipX)
			break;
	}
}

void hexOut(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, uint32_t val, uint8_t numDigits)
{
	const uint8_t *srcPtr;
	int8_t i;
	uint8_t x, y, nybble;
	uint32_t *dstPtr, pixVal;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	pixVal = video.palette[paletteIndex];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (i = (numDigits - 1); i >= 0; --i)
	{
		// extract current nybble and set pointer to glyph
		nybble = (val >> (i * 4)) & 15;
		srcPtr = &font6Data[nybble * FONT6_CHAR_W];

		// render glyph
		for (y = 0; y < FONT6_CHAR_H; ++y)
		{
			for (x = 0; x < FONT6_CHAR_W; ++x)
			{
				if (srcPtr[x])
					dstPtr[x] = pixVal;
			}

			srcPtr += FONT6_WIDTH;
			dstPtr += SCREEN_W;
		}

		dstPtr -= ((SCREEN_W * FONT6_CHAR_H) - FONT6_CHAR_W); // xpos += FONT6_CHAR_W
	}
}

void hexOutBg(uint16_t xPos, uint16_t yPos, uint8_t fgPalette, uint8_t bgPalette, uint32_t val, uint8_t numDigits)
{
	const uint8_t *srcPtr;
	int8_t i;
	uint8_t x, y, nybble;
	uint32_t *dstPtr, fg, bg;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H));

	fg = video.palette[fgPalette];
	bg = video.palette[bgPalette];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (i = (numDigits - 1); i >= 0; --i)
	{
		// extract current nybble and set pointer to glyph
		nybble = (val >> (i * 4)) & 15;
		srcPtr = &font6Data[nybble * FONT6_CHAR_W];

		// render glyph
		for (y = 0; y < FONT6_CHAR_H; ++y)
		{
			for (x = 0; x < FONT6_CHAR_W; ++x)
				dstPtr[x] = srcPtr[x] ? fg : bg;

			srcPtr += FONT6_WIDTH;
			dstPtr += SCREEN_W;
		}

		dstPtr -= ((SCREEN_W * FONT6_CHAR_H) - FONT6_CHAR_W); // xpos += FONT6_CHAR_W 
	}
}

void hexOutShadow(uint16_t xPos, uint16_t yPos, uint8_t paletteIndex, uint8_t shadowPaletteIndex, uint32_t val, uint8_t numDigits)
{
	hexOut(xPos + 1, yPos + 1, shadowPaletteIndex, val, numDigits);
	hexOut(xPos + 0, yPos + 0,       paletteIndex, val, numDigits);
}

// FILL ROUTINES

void clearRect(uint16_t xPos, uint16_t yPos, uint16_t w, uint16_t h)
{
	uint16_t y;
	uint32_t *dstPtr, fillNumDwords;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H) && ((xPos + w) <= SCREEN_W) && ((yPos + h) <= SCREEN_H));

	fillNumDwords = w * sizeof (int32_t);

	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	for (y = 0; y < h; ++y)
	{
		memset(dstPtr, 0, fillNumDwords);
		dstPtr += SCREEN_W;
	}
}

void fillRect(uint16_t xPos, uint16_t yPos, uint16_t w, uint16_t h, uint8_t paletteIndex)
{
	uint32_t *dstPtr, pixVal, x, y;

	assert((xPos < SCREEN_W) && (yPos < SCREEN_H) && ((xPos + w) <= SCREEN_W) && ((yPos + h) <= SCREEN_H));

	pixVal = video.palette[paletteIndex];
	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];

	for (y = 0; y < h; ++y)
	{
		for (x = 0; x < w; ++x)
			dstPtr[x] = pixVal;

		dstPtr += SCREEN_W;
	}
}

void blit(uint16_t xPos, uint16_t yPos, const uint8_t *srcPtr, uint16_t w, uint16_t h)
{
	uint32_t *dstPtr, x, y, pixel;

	assert((srcPtr != NULL) && (xPos < SCREEN_W) && (yPos < SCREEN_H) && ((xPos + w) <= SCREEN_W) && ((yPos + h) <= SCREEN_H));

	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	for (y = 0; y < h; ++y)
	{
		for (x = 0; x < w; ++x)
		{
			pixel = srcPtr[x];
			if (pixel != PAL_TRANSPR)
				dstPtr[x] = video.palette[pixel];
		}

		srcPtr += w;
		dstPtr += SCREEN_W;
	}
}

void blitFast(uint16_t xPos, uint16_t yPos, const uint8_t *srcPtr, uint16_t w, uint16_t h) // no colorkey
{
	uint32_t *dstPtr, x, y;

	assert((srcPtr != NULL) && (xPos < SCREEN_W) && (yPos < SCREEN_H) && ((xPos + w) <= SCREEN_W) && ((yPos + h) <= SCREEN_H));

	dstPtr = &video.frameBuffer[(yPos * SCREEN_W) + xPos];
	for (y = 0; y < h; ++y)
	{
		for (x = 0; x < w; ++x)
			dstPtr[x] = video.palette[srcPtr[x]];

		srcPtr += w;
		dstPtr += SCREEN_W;
	}
}

// LINE ROUTINES

void hLine(uint16_t x, uint16_t y, uint16_t w, uint8_t paletteIndex)
{
	uint32_t *dstPtr, i, pixVal;

	assert((x < SCREEN_W) && (y < SCREEN_H) && ((x + w) <= SCREEN_W));

	pixVal = video.palette[paletteIndex];

	dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (i = 0; i < w; ++i)
		dstPtr[i] = pixVal;
}

void vLine(uint16_t x, uint16_t y, uint16_t h, uint8_t paletteIndex)
{
	uint32_t *dstPtr,i, pixVal;

	assert((x < SCREEN_W) && (y < SCREEN_H) && ((y + h) <= SCREEN_W));

	pixVal = video.palette[paletteIndex];

	dstPtr = &video.frameBuffer[(y * SCREEN_W) + x];
	for (i = 0; i < h; ++i)
	{
		*dstPtr  = pixVal;
		 dstPtr += SCREEN_W;
	}
}

void line(int16_t x1, int16_t x2, int16_t y1, int16_t y2, uint8_t paletteIndex)
{
	int16_t d, x, y, sx, sy, dx, dy;
	uint16_t ax, ay;
	int32_t pitch;
	uint32_t pixVal, *dst32;

	// get coefficients
	dx = x2 - x1;
	ax = ABS(dx) * 2;
	sx = SGN(dx);
	dy = y2 - y1;
	ay = ABS(dy) * 2;
	sy = SGN(dy);
	x  = x1;
	y  = y1;

	pixVal = video.palette[paletteIndex];
	pitch  = sy * SCREEN_W;
	dst32  = &video.frameBuffer[(y * SCREEN_W) + x];

	// draw line
	if (ax > ay)
	{
		d = ay - (ax / 2);

		while (true)
		{
			assert((x < SCREEN_W) && (y < SCREEN_H));

			*dst32 = pixVal;
			if (x == x2)
				break;

			if (d >= 0)
			{
#ifdef _DEBUG
				y += sy;
#endif
				d -= ax;
				dst32 += pitch;
			}

			x += sx;
			d += ay;
			dst32 += sx;
		}
	}
	else
	{
		d = ax - (ay / 2);

		while (true)
		{
			assert((x < SCREEN_W) && (y < SCREEN_H));

			*dst32 = pixVal;
			if (y == y2)
				break;

			if (d >= 0)
			{
#ifdef _DEBUG
				x += sx;
#endif
				d -= ay;
				dst32 += sx;
			}

			y += sy;
			d += ax;
			dst32 += pitch;
		}
	}
}

void drawFramework(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t type)
{
	assert((x < SCREEN_W) && (y < SCREEN_H) && (w >= 2) && (h >= h));

	h--;
	w--;

	if (type == FRAMEWORK_TYPE1)
	{
		// top left corner
		hLine(x, y,     w,     PAL_DSKTOP1);
		vLine(x, y + 1, h - 1, PAL_DSKTOP1);

		// bottom right corner
		hLine(x,     y + h, w,     PAL_DSKTOP2);
		vLine(x + w, y,     h + 1, PAL_DSKTOP2);

		// fill background
		fillRect(x + 1, y + 1, w - 1, h - 1, PAL_DESKTOP);
	}
	else
	{
		// top left corner
		hLine(x, y,     w + 1, PAL_DSKTOP2);
		vLine(x, y + 1, h,     PAL_DSKTOP2);

		// bottom right corner
		hLine(x + 1, y + h, w,     PAL_DSKTOP1);
		vLine(x + w, y + 1, h - 1, PAL_DSKTOP1);

		// clear background
		clearRect(x + 1, y + 1, w - 1, h - 1);
	}
}

// GUI FUNCTIONS

void showTopLeftMainScreen(bool restoreScreens)
{
	editor.ui.diskOpShown          = false;
	editor.ui.sampleEditorExtShown = false;
	editor.ui.instEditorExtShown   = false;
	editor.ui.transposeShown       = false;
	editor.ui.advEditShown         = false;
	editor.ui.wavRendererShown     = false;
	editor.ui.trimScreenShown      = false;

	editor.ui.scopesShown = true;
	if (restoreScreens)
	{
		switch (editor.ui.oldTopLeftScreen)
		{
			default: break;
			case 1: editor.ui.diskOpShown          = true; break;
			case 2: editor.ui.sampleEditorExtShown = true; break;
			case 3: editor.ui.instEditorExtShown   = true; break;
			case 4: editor.ui.transposeShown       = true; break;
			case 5: editor.ui.advEditShown         = true; break;
			case 6: editor.ui.wavRendererShown     = true; break;
			case 7: editor.ui.trimScreenShown      = true; break;
		}

		if (editor.ui.oldTopLeftScreen > 0)
			editor.ui.scopesShown = false;
	}

	editor.ui.oldTopLeftScreen = 0;

	if (editor.ui.diskOpShown)
	{
		showDiskOpScreen();
	}
	else
	{
		// pos ed.
		drawFramework(0, 0, 112, 77, FRAMEWORK_TYPE1);
		drawFramework(2, 2,  51, 19, FRAMEWORK_TYPE2);
		drawFramework(2,30,  51, 19, FRAMEWORK_TYPE2);
		showScrollBar(SB_POS_ED);
		showPushButton(PB_POSED_POS_UP);
		showPushButton(PB_POSED_POS_DOWN);
		showPushButton(PB_POSED_INS);
		showPushButton(PB_POSED_PATT_UP);
		showPushButton(PB_POSED_PATT_DOWN);
		showPushButton(PB_POSED_DEL);
		showPushButton(PB_POSED_LEN_UP);
		showPushButton(PB_POSED_LEN_DOWN);
		showPushButton(PB_POSED_REP_UP);
		showPushButton(PB_POSED_REP_DOWN);
		textOutShadow(4, 52, PAL_FORGRND, PAL_DSKTOP2, "Songlen.");
		textOutShadow(4, 64, PAL_FORGRND, PAL_DSKTOP2, "Repstart");
		drawPosEdNums(song.songPos);
		drawSongLength();
		drawSongRepS();

		// logo button
		showPushButton(PB_LOGO);
		showPushButton(PB_BADGE);

		// left menu
		drawFramework(291, 0, 65, 173, FRAMEWORK_TYPE1);
		showPushButton(PB_ABOUT);
		showPushButton(PB_NIBBLES);
		showPushButton(PB_KILL);
		showPushButton(PB_TRIM);
		showPushButton(PB_EXTEND_VIEW);
		showPushButton(PB_TRANSPOSE);
		showPushButton(PB_INST_ED_EXT);
		showPushButton(PB_SMP_ED_EXT);
		showPushButton(PB_ADV_EDIT);
		showPushButton(PB_ADD_CHANNELS);
		showPushButton(PB_SUB_CHANNELS);

		// song/pattern
		drawFramework(112, 32, 94, 45, FRAMEWORK_TYPE1);
		drawFramework(206, 32, 85, 45, FRAMEWORK_TYPE1);
		showPushButton(PB_BPM_UP);
		showPushButton(PB_BPM_DOWN);
		showPushButton(PB_SPEED_UP);
		showPushButton(PB_SPEED_DOWN);
		showPushButton(PB_EDITADD_UP);
		showPushButton(PB_EDITADD_DOWN);
		showPushButton(PB_PATT_UP);
		showPushButton(PB_PATT_DOWN);
		showPushButton(PB_PATTLEN_UP);
		showPushButton(PB_PATTLEN_DOWN);
		showPushButton(PB_PATT_EXPAND);
		showPushButton(PB_PATT_SHRINK);
		textOutShadow(116, 36, PAL_FORGRND, PAL_DSKTOP2, "BPM");
		textOutShadow(116, 50, PAL_FORGRND, PAL_DSKTOP2, "Spd.");
		textOutShadow(116, 64, PAL_FORGRND, PAL_DSKTOP2, "Add.");
		textOutShadow(210, 36, PAL_FORGRND, PAL_DSKTOP2, "Ptn.");
		textOutShadow(210, 50, PAL_FORGRND, PAL_DSKTOP2, "Ln.");
		drawSongBPM(song.speed);
		drawSongSpeed(song.tempo);
		drawEditPattern(editor.editPattern);
		drawPatternLength(editor.editPattern);
		drawIDAdd();

		// status bar
		drawFramework(0, 77, 291, 15, FRAMEWORK_TYPE1);
		textOutShadow(4, 80, PAL_FORGRND, PAL_DSKTOP2, "Global volume");
		drawGlobalVol(song.globVol);

		editor.ui.updatePosSections = true;

		textOutShadow(204, 80, PAL_FORGRND, PAL_DSKTOP2, "Time");
		charOutShadow(250, 80, PAL_FORGRND, PAL_DSKTOP2, ':');
		charOutShadow(270, 80, PAL_FORGRND, PAL_DSKTOP2, ':');
		drawPlaybackTime();

			 if (editor.ui.sampleEditorExtShown) drawSampleEditorExt();
		else if (editor.ui.instEditorExtShown)   drawInstEditorExt();
		else if (editor.ui.transposeShown)       drawTranspose();
		else if (editor.ui.advEditShown)         drawAdvEdit();
		else if (editor.ui.wavRendererShown)     drawWavRenderer();
		else if (editor.ui.trimScreenShown)      drawTrimScreen();

		if (editor.ui.scopesShown)
			drawScopeFramework();
	}
}

void hideTopLeftMainScreen(void)
{
	hideDiskOpScreen();
	hideInstEditorExt();
	hideSampleEditorExt();
	hideTranspose();
	hideAdvEdit();
	hideWavRenderer();
	hideTrimScreen();

	editor.ui.scopesShown = false;

	// position editor
	hideScrollBar(SB_POS_ED);

	hidePushButton(PB_POSED_POS_UP);
	hidePushButton(PB_POSED_POS_DOWN);
	hidePushButton(PB_POSED_INS);
	hidePushButton(PB_POSED_PATT_UP);
	hidePushButton(PB_POSED_PATT_DOWN);
	hidePushButton(PB_POSED_DEL);
	hidePushButton(PB_POSED_LEN_UP);
	hidePushButton(PB_POSED_LEN_DOWN);
	hidePushButton(PB_POSED_REP_UP);
	hidePushButton(PB_POSED_REP_DOWN);

	// logo button
	hidePushButton(PB_LOGO);
	hidePushButton(PB_BADGE);

	// left menu
	hidePushButton(PB_ABOUT);
	hidePushButton(PB_NIBBLES);
	hidePushButton(PB_KILL);
	hidePushButton(PB_TRIM);
	hidePushButton(PB_EXTEND_VIEW);
	hidePushButton(PB_TRANSPOSE);
	hidePushButton(PB_INST_ED_EXT);
	hidePushButton(PB_SMP_ED_EXT);
	hidePushButton(PB_ADV_EDIT);
	hidePushButton(PB_ADD_CHANNELS);
	hidePushButton(PB_SUB_CHANNELS);

	// song/pattern
	hidePushButton(PB_BPM_UP);
	hidePushButton(PB_BPM_DOWN);
	hidePushButton(PB_SPEED_UP);
	hidePushButton(PB_SPEED_DOWN);
	hidePushButton(PB_EDITADD_UP);
	hidePushButton(PB_EDITADD_DOWN);
	hidePushButton(PB_PATT_UP);
	hidePushButton(PB_PATT_DOWN);
	hidePushButton(PB_PATTLEN_UP);
	hidePushButton(PB_PATTLEN_DOWN);
	hidePushButton(PB_PATT_EXPAND);
	hidePushButton(PB_PATT_SHRINK);
}

void showTopRightMainScreen(void)
{
	// right menu
	drawFramework(356, 0, 65, 173, FRAMEWORK_TYPE1);
	showPushButton(PB_PLAY_SONG);
	showPushButton(PB_PLAY_PATT);
	showPushButton(PB_STOP);
	showPushButton(PB_RECORD_SONG);
	showPushButton(PB_RECORD_PATT);
	showPushButton(PB_DISK_OP);
	showPushButton(PB_INST_ED);
	showPushButton(PB_SMP_ED);
	showPushButton(PB_CONFIG);
	showPushButton(PB_HELP);

	// instrument switcher
	editor.ui.instrSwitcherShown = true;
	showInstrumentSwitcher();

	// song name
	showTextBox(TB_SONG_NAME);
	drawSongName();
}

void hideTopRightMainScreen(void)
{
	// right menu
	hidePushButton(PB_PLAY_SONG);
	hidePushButton(PB_PLAY_PATT);
	hidePushButton(PB_STOP);
	hidePushButton(PB_RECORD_SONG);
	hidePushButton(PB_RECORD_PATT);
	hidePushButton(PB_DISK_OP);
	hidePushButton(PB_INST_ED);
	hidePushButton(PB_SMP_ED);
	hidePushButton(PB_CONFIG);
	hidePushButton(PB_HELP);

	// instrument switcher
	hideInstrumentSwitcher();
	editor.ui.instrSwitcherShown = false;

	hideTextBox(TB_SONG_NAME);
}

// BOTTOM STUFF

void setOldTopLeftScreenFlag(void)
{
		 if (editor.ui.diskOpShown)          editor.ui.oldTopLeftScreen = 1;
	else if (editor.ui.sampleEditorExtShown) editor.ui.oldTopLeftScreen = 2;
	else if (editor.ui.instEditorExtShown)   editor.ui.oldTopLeftScreen = 3;
	else if (editor.ui.transposeShown)       editor.ui.oldTopLeftScreen = 4;
	else if (editor.ui.advEditShown)         editor.ui.oldTopLeftScreen = 5;
	else if (editor.ui.wavRendererShown)     editor.ui.oldTopLeftScreen = 6;
	else if (editor.ui.trimScreenShown)      editor.ui.oldTopLeftScreen = 7;
}

void hideTopLeftScreen(void)
{
	setOldTopLeftScreenFlag();

	hideTopLeftMainScreen();
	hideNibblesScreen();
	hideConfigScreen();
	hideAboutScreen();
	hideHelpScreen();
}

void hideTopScreen(void)
{
	setOldTopLeftScreenFlag();

	hideTopLeftMainScreen();
	hideTopRightMainScreen();
	hideNibblesScreen();
	hideConfigScreen();
	hideAboutScreen();
	hideHelpScreen();

	editor.ui.instrSwitcherShown = false;
	editor.ui.scopesShown = false;
}

void showTopScreen(bool restoreScreens)
{
	editor.ui.scopesShown = false;

	if (editor.ui.aboutScreenShown)
	{
		showAboutScreen();
	}
	else if (editor.ui.configScreenShown)
	{
		showConfigScreen();
	}
	else if (editor.ui.helpScreenShown)
	{
		showHelpScreen();
	}
	else if (editor.ui.nibblesShown)
	{
		showNibblesScreen();
	}
	else
	{
		showTopLeftMainScreen(restoreScreens); // updates editor.ui.scopesShown
		showTopRightMainScreen();
	}
}

void showBottomScreen(void)
{
	if (editor.ui.extended || editor.ui.patternEditorShown)
		showPatternEditor();
	else if (editor.ui.instEditorShown)
		showInstEditor();
	else if (editor.ui.sampleEditorShown)
		showSampleEditor();
}

void drawGUIOnRunTime(void)
{
	setScrollBarPos(SB_POS_ED, 0, false);

	showTopScreen(false); // false = don't restore screens
	showPatternEditor();

	editor.ui.updatePosSections = true;
}
