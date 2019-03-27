#ifndef __FT2_MIX_MACROS_H
#define __FT2_MIX_MACROS_H

#include "ft2_header.h"

/* ----------------------------------------------------------------------- */
/*                          GENERAL MIXER MACROS                           */
/* ----------------------------------------------------------------------- */

#define GET_VOL \
	CDA_LVol = v->SLVol2; \
	CDA_RVol = v->SRVol2; \

#define SET_VOL_BACK \
	v->SLVol2 = CDA_LVol; \
	v->SRVol2 = CDA_RVol; \

#define GET_MIXER_VARS \
	audioMixL = audio.mixBufferL; \
	audioMixR = audio.mixBufferR; \
	mixInMono = (CDA_LVol == CDA_RVol); \
	realPos   = v->SPos; \
	pos       = v->SPosDec; \
	delta     = v->SFrq; \

#define GET_MIXER_VARS_RAMP \
	audioMixL  = audio.mixBufferL; \
	audioMixR  = audio.mixBufferR; \
	CDA_LVolIP = v->SLVolIP; \
	CDA_RVolIP = v->SRVolIP; \
	mixInMono  = (v->SLVol2 == v->SRVol2) && (CDA_LVolIP == CDA_RVolIP); \
	realPos    = v->SPos; \
	pos        = v->SPosDec; \
	delta      = v->SFrq; \

#define SET_BASE8 \
	CDA_LinearAdr = v->sampleData8; \
	smpPtr = CDA_LinearAdr + realPos; \

#define SET_BASE16 \
	CDA_LinearAdr = v->sampleData16; \
	smpPtr = CDA_LinearAdr + realPos; \

#define INC_POS \
	pos += delta; \
	smpPtr += (pos >> 16); \
	pos &= 0xFFFF; \

#define DEC_POS \
	pos += delta; \
	smpPtr -= (pos >> 16); \
	pos &= 0xFFFF; \

#define SET_BACK_MIXER_POS \
	v->SPosDec = pos; \
	v->SPos = realPos; \

/* ----------------------------------------------------------------------- */
/*                          SAMPLE RENDERING MACROS                        */
/* ----------------------------------------------------------------------- */

#define VOLUME_RAMPING \
	CDA_LVol += CDA_LVolIP; \
	CDA_RVol += CDA_RVolIP; \

// all the 64-bit MULs here convert to fast logic on most 32-bit CPUs

#define RENDER_8BIT_SMP \
	sample = (*smpPtr) << (28 - 8); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_8BIT_SMP_MONO \
	sample = (*smpPtr) << (28 - 8); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_16BIT_SMP \
	sample = (*smpPtr) << (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_16BIT_SMP_MONO \
	sample = (*smpPtr) << (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#ifndef LERPMIX

// 3-tap quadratic interpolation (default - slower, but better quality)

// in: int32_t s1,s2,s3 = -128..127 | f = 0..65535 (frac) | out: s1 (can exceed 16-bits because of under-/overshoot)
#define INTERPOLATE8(s1, s2, s3, f) \
{ \
	int32_t frac, s4; \
	\
	frac = (f) >> 1; \
	s2 <<= 8; \
	s4 = (s1 + s3) << 7; \
	s4 -= s2; \
	s4 = (s4 * frac) >> 16; \
	s3 += s1; \
	s3 <<= 8; \
	s1 <<= 9; \
	s3 = (s3 + s1) >> 2; \
	s1 >>= 1; \
	s4 += s2; \
	s4 -= s3; \
	s4 = (s4 * frac) >> 14; \
	s1 += s4; \
} \

// in: int32_t s1,s2,s3 = -32768..32767 | f = 0..65535 (frac) | out: s1 (can exceed 16-bits because of under-/overshoot)
#define INTERPOLATE16(s1, s2, s3, f)  \
{ \
	int32_t frac, s4; \
	\
	frac = (f) >> 1; \
	s4 = (s1 + s3) >> 1; \
	s4 -= s2; \
	s4 = (s4 * frac) >> 16; \
	s3 += s1; \
	s1 += s1; \
	s3 = (s3 + s1) >> 2; \
	s1 >>= 1; \
	s4 += s2; \
	s4 -= s3; \
	s4 = (s4 * frac) >> 14; \
	s1 += s4; \
} \

#define RENDER_8BIT_SMP_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE8(sample, sample2, sample3, pos) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_8BIT_SMP_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE8(sample, sample2, sample3, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_8BIT_SMP_MONO_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE8(sample, sample2, sample3, pos) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_8BIT_SMP_MONO_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 1); \
	INTERPOLATE8(sample, sample2, sample3, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_16BIT_SMP_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE16(sample, sample2, sample3, pos) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_16BIT_SMP_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE16(sample, sample2, sample3, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_16BIT_SMP_MONO_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE16(sample, sample2, sample3, pos) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_16BIT_SMP_MONO_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	sample3 = *(smpPtr + 2); \
	INTERPOLATE16(sample, sample2, sample3, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#else

// 2-tap linear interpolation (like FT2 - faster, but bad quality)

// in: int32_t s1,s2 = -128..127 | f = 0..65535 (frac) | out: s1 = -32768..32767
#define INTERPOLATE8(s1, s2, f) \
	s2 -= s1; \
	s2 *= (int32_t)(f); \
	s1 <<= 8; \
	s2 >>= (16 - 8); \
	s1 += s2; \

// in: int32_t s1,s2 = -32768..32767 | f = 0..65535 (frac) | out: s1 = -32768..32767
#define INTERPOLATE16(s1, s2, f) \
	s2 -= s1; \
	s2 >>= 1; \
	s2 *= (int32_t)(f); \
	s2 >>= (16 - 1); \
	s1 += s2; \

#define RENDER_8BIT_SMP_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE8(sample, sample2, pos) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_8BIT_SMP_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE8(sample, sample2, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_8BIT_SMP_MONO_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE8(sample, sample2, pos) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_8BIT_SMP_MONO_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE8(sample, sample2, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_16BIT_SMP_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE16(sample, sample2, pos) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_16BIT_SMP_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE16(sample, sample2, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	*audioMixL++ += (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixR++ += (int32_t)(((int64_t)(sample) * CDA_RVol) >> 32); \

#define RENDER_16BIT_SMP_MONO_INTRP \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE16(sample, sample2, pos) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#define RENDER_16BIT_SMP_MONO_INTRP_BACKWARDS \
	sample  = *(smpPtr + 0); \
	sample2 = *(smpPtr + 1); \
	INTERPOLATE16(sample, sample2, pos ^ 0xFFFF) \
	sample <<= (28 - 16); \
	sample = (int32_t)(((int64_t)(sample) * CDA_LVol) >> 32); \
	*audioMixL++ += sample; \
	*audioMixR++ += sample; \

#endif

/* ----------------------------------------------------------------------- */
/*                      SAMPLES-TO-MIX LIMITING MACROS                     */
/* ----------------------------------------------------------------------- */

#define LIMIT_MIX_NUM \
	CDA_SmpEndFlag = true; \
	\
	i = (v->SLen - 1) - realPos; \
	if (v->SFrq > (i >> 16)) \
	{ \
		if (i >= 65536) /* won't fit in a 32-bit div */ \
		{ \
			samplesToMix = ((uint32_t)(pos ^ 0xFFFFFFFF) / v->SFrq) + 1; \
			CDA_SmpEndFlag = false; \
		} \
		else \
		{ \
			samplesToMix = ((uint32_t)((i << 16) | (pos ^ 0xFFFF)) / v->SFrq) + 1; \
		} \
	} \
	else \
	{ \
		samplesToMix = 65535; \
	} \
	\
	if (samplesToMix > (uint32_t)(CDA_BytesLeft)) \
	{ \
		samplesToMix = CDA_BytesLeft; \
		CDA_SmpEndFlag = false; \
	} \


#define LIMIT_MIX_NUM_BIDI_LOOP \
	CDA_SmpEndFlag = true; \
	\
	if (v->backwards) \
		i = realPos - v->SRepS; \
	else \
		i = (v->SLen - 1) - realPos; \
	\
	if (v->SFrq > (i >> 16)) \
	{ \
		if (i >= 65536) /* won't fit in a 32-bit div */ \
		{ \
			samplesToMix = ((uint32_t)(pos ^ 0xFFFFFFFF) / v->SFrq) + 1; \
			CDA_SmpEndFlag = false; \
		} \
		else \
		{ \
			samplesToMix = ((uint32_t)((i << 16) | (pos ^ 0xFFFF)) / v->SFrq) + 1; \
		} \
	} \
	else \
	{ \
		samplesToMix = 65535; \
	} \
	\
	if (samplesToMix > (uint32_t)(CDA_BytesLeft)) \
	{ \
		samplesToMix = CDA_BytesLeft; \
		CDA_SmpEndFlag = false; \
	} \

#define LIMIT_MIX_NUM_RAMP \
	if (v->SVolIPLen == 0) \
	{ \
		CDA_LVolIP = 0; \
		CDA_RVolIP = 0; \
		\
		if (v->isFadeOutVoice) \
		{ \
			v->mixRoutine = NULL; /* fade out voice is done, shut it down */ \
			return; \
		} \
	} \
	else \
	{ \
		if (samplesToMix > (uint32_t)(v->SVolIPLen)) \
		{ \
			samplesToMix = v->SVolIPLen; \
			CDA_SmpEndFlag = false; \
		} \
		\
		v->SVolIPLen -= samplesToMix; \
	} \

/* ----------------------------------------------------------------------- */
/*                     SAMPLE END/LOOP WRAPPING MACROS                     */
/* ----------------------------------------------------------------------- */

#define HANDLE_SAMPLE_END \
	realPos = (int32_t)(smpPtr - CDA_LinearAdr); \
	if (CDA_SmpEndFlag) \
	{ \
		v->mixRoutine = NULL; \
		return; \
	} \

#define WRAP_LOOP \
	realPos = (int32_t)(smpPtr - CDA_LinearAdr); \
	if (CDA_SmpEndFlag) \
	{ \
		do \
		{ \
			realPos -= v->SRepL; \
		} \
		while (realPos >= v->SLen); \
		\
		smpPtr = CDA_LinearAdr + realPos; \
	} \

#define WRAP_BIDI_LOOP \
	realPos = (int32_t)(smpPtr - CDA_LinearAdr); \
	if (CDA_SmpEndFlag) \
	{ \
		/* if backwards: turn backward underflow into forward overflow */ \
		if (v->backwards) \
			realPos = (v->SLen - 1) + (v->SRepS - realPos); \
		\
		do \
		{ \
			realPos -= v->SRepL; \
			v->backwards ^= 1; \
		} \
		while (realPos >= v->SLen); \
		\
		/* if backwards: forwards position -> backwards position */ \
		if (v->backwards) \
			realPos = (v->SLen - 1) - (realPos - v->SRepS); \
		\
		smpPtr = CDA_LinearAdr + realPos; \
	} \

/* ----------------------------------------------------------------------- */
/*                       VOLUME=0 OPTIMIZATION MACROS                      */
/* ----------------------------------------------------------------------- */

#define VOL0_OPTIMIZATION_NO_LOOP \
	pos     = v->SPosDec + ((v->SFrq & 0xFFFF) * numSamples); \
	realPos = v->SPos    + ((v->SFrq >>    16) * numSamples) + (pos >> 16); \
	pos    &= 0xFFFF; \
	\
	if (realPos >= v->SLen) \
	{ \
		v->mixRoutine = NULL; /* shut down voice */ \
		return; \
	} \
	\
	SET_BACK_MIXER_POS

#define VOL0_OPTIMIZATION_LOOP \
	pos     = v->SPosDec + ((v->SFrq & 0xFFFF) * numSamples); \
	realPos = v->SPos    + ((v->SFrq >>    16) * numSamples) + (pos >> 16); \
	pos    &= 0xFFFF; \
	\
	while (realPos >= v->SLen) \
		   realPos -= v->SRepL; \
	\
	SET_BACK_MIXER_POS

#define VOL0_OPTIMIZATION_BIDI_LOOP \
	/* if backwards: backwards position -> forwards position. */ \
	/* in FT2, we're always inside the loop when sampling backwards */ \
	if (v->backwards) \
		v->SPos = (v->SLen - 1) - (v->SPos - v->SRepS); \
	\
	pos     = v->SPosDec + ((v->SFrq & 0xFFFF) * numSamples); \
	realPos = v->SPos    + ((v->SFrq >>    16) * numSamples) + (pos >> 16); \
	pos    &= 0xFFFF; \
	\
	while (realPos >= v->SLen) \
	{ \
		realPos -= v->SRepL; \
		v->backwards ^= 1; \
	} \
	\
	/* if backwards: forwards position -> backwards position */ \
	if (v->backwards) \
		realPos = (v->SLen - 1) - (realPos - v->SRepS); \
	\
	SET_BACK_MIXER_POS

#endif
