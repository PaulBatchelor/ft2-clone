#ifndef __FT2_PALETTE_H
#define __FT2_PALETTE_H

#define PAL_TRANSPR 127

// FT2 palette (exact order as real FT2)
enum
{
	PAL_BCKGRND = 0,
	PAL_PATTEXT = 1,
	PAL_BLCKMRK = 2,
	PAL_BLCKTXT = 3,
	PAL_DESKTOP = 4,
	PAL_FORGRND = 5,
	PAL_BUTTONS = 6,
	PAL_BTNTEXT = 7,
	PAL_DSKTOP2 = 8,
	PAL_DSKTOP1 = 9,
	PAL_BUTTON2 = 10,
	PAL_BUTTON1 = 11,
	PAL_MOUSEPT = 12,

	/*
	** There are three more palettes used for mouse XOR,
	** but we don't need them in the clone.
	*/

	// custom clone palettes
	PAL_LOOPPIN = 13,
	PAL_TEXTMRK = 14,

	PAL_NUM
};

#endif
