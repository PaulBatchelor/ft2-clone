// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include "header.h"
#include "audio.h"
#include "gui.h"
#include "mouse.h"
#include "sample_ed.h"
#include "module_loader.h"

/*
** These savers are directly ported, so they should act identical to FT2
** except for some very minor changes.
*/

static SDL_Thread *thread;

static uint16_t packPatt(uint8_t *pattPtr, uint16_t numRows);

// ft2_replayer.c
extern const char modSig[16][5];
extern const uint16_t amigaPeriod[12 * 8];

bool saveXM(UNICHAR *filenameU)
{
	uint8_t *pattPtr;
	int16_t ap, ai, i, k, a;
	uint16_t b, c;
	size_t result;
	instrTyp *ins;
	songHeaderTyp h;
	patternHeaderTyp ph;
	instrHeaderTyp ih;
	sampleTyp *srcSmp;
	sampleHeaderTyp *dstSmp;
	FILE *f;

	f = UNICHAR_FOPEN(filenameU, "wb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "Error opening file for saving, is it in use?");
		return (false);
	}

	memcpy(h.sig, "Extended Module: ", 17);
	memset(h.name, ' ', 20);
	h.name[20] = 0x1A;
	memcpy(h.name, song.name, strlen(song.name));
	memcpy(h.progName, PROG_NAME_STR, 20);
	h.ver = 0x0104;
	h.headerSize = 20 + 256;
	h.len = song.len;
	h.repS = song.repS;
	h.antChn = song.antChn;
	h.defTempo = song.tempo;
	h.defSpeed = song.speed;

	// count number of patterns
	ap = MAX_PATTERNS;
	do
	{
		if (patternEmpty(ap - 1))
			ap--;
		else
			break;
	}
	while (ap > 0);
	h.antPtn = ap;

	// count number of instruments
	ai = MAX_INST;
	while ((ai > 0) && (getUsedSamples(ai) == 0) && (song.instrName[ai][0] == '\0'))
		ai--;
	h.antInstrs = ai;

	h.flags = linearFrqTab;
	memcpy(h.songTab, song.songTab, sizeof (song.songTab));

	if (fwrite(&h, sizeof (h), 1, f) != 1)
	{
		fclose(f);
		okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
		return (false);
	}

	for (i = 0; i < ap; ++i)
	{
		if (patternEmpty(i))
		{
			if (patt[i] != NULL)
			{
				free(patt[i]);
				patt[i] = NULL;
			}

			pattLens[i] = 64;
		}

		ph.patternHeaderSize = sizeof (patternHeaderTyp);
		ph.pattLen = pattLens[i];
		ph.typ = 0;

		if (patt[i] == NULL)
		{
			ph.dataLen = 0;
			if (fwrite(&ph, ph.patternHeaderSize, 1, f) != 1)
			{
				fclose(f);
				okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
				return (false);
			}
		}
		else
		{
			c = packPatt((uint8_t *)(patt[i]), pattLens[i]);
			b = pattLens[i] * TRACK_WIDTH;
			ph.dataLen = c;

			result  = fwrite(&ph,     ph.patternHeaderSize, 1, f);
			result += fwrite(patt[i], ph.dataLen,           1, f);

			pattPtr = (uint8_t *)(patt[i]);

			memcpy(&pattPtr[b - c], patt[i], c);
			unpackPatt(pattPtr, b - c, pattLens[i], song.antChn);
			clearUnusedChannels(patt[i], pattLens[i], song.antChn);

			if (result != 2) // write was not OK
			{
				fclose(f);
				okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
				return (false);
			}
		}
	}

	for (i = 1; i <= ai; ++i)
	{
		ins = &instr[i];

		a = getUsedSamples(i);

		memset(ih.name, 0, 22);
		memcpy(ih.name, song.instrName[i], strlen(song.instrName[i]));

		ih.typ        = 0;
		ih.antSamp    = a;
		ih.sampleSize = sizeof (sampleHeaderTyp);

		if (a > 0)
		{
			ih.instrSize = INSTR_HEADER_SIZE;

			memcpy(ih.ta, ins, INSTR_SIZE);
			for (k = 0; k < a; ++k)
			{
				srcSmp = &ins->samp[k];
				dstSmp = &ih.samp[k];

				memset(dstSmp->name, ' ', sizeof (dstSmp->name));

				memcpy(dstSmp, srcSmp, 12 + 4 + 2 + strlen(srcSmp->name));
				if (srcSmp->pek == NULL)
					dstSmp->len = 0;
			}
		}
		else
		{
			ih.instrSize = 22 + 11;
		}

		if (fwrite(&ih, ih.instrSize + (a * sizeof (sampleHeaderTyp)), 1, f) != 1)
		{
			fclose(f);
			okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
			return (false);
		}

		for (k = 0; k < a; ++k)
		{
			srcSmp = &ins->samp[k];
			if (srcSmp->pek != NULL)
			{
				restoreSample(srcSmp);
				samp2Delta(srcSmp->pek, srcSmp->len, srcSmp->typ);

				result = fwrite(srcSmp->pek, 1, srcSmp->len, f);

				delta2Samp(srcSmp->pek, srcSmp->len, srcSmp->typ);
				fixSample(srcSmp);

				if (result != (size_t)(srcSmp->len)) // write not OK
				{
					fclose(f);
					okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
					return (false);
				}
			}
		}
	}

	removeSongModifiedFlag();

	fclose(f);

	editor.diskOpReadDir = true; // force diskop re-read

	setMouseBusy(false);
	return (true);
}

static bool saveMOD(UNICHAR *filenameU)
{
	bool test, tooManyInstr, incompatEfx, noteUnderflow;
	int8_t smp8;
	uint8_t ton, inst, pattBuff[64 * 4 * 32];
	int16_t a, i, ap, *ptr16;
	int32_t j, k, l1, l2, l3;
	FILE *f;
	instrTyp *ins;
	sampleTyp *smp;
	tonTyp *t;
	songMOD31HeaderTyp hm;

	tooManyInstr  = false;
	incompatEfx   = false;
	noteUnderflow = false;

	if (linearFrqTab) okBoxThreadSafe(0, "System message", "Linear frequency table used!");

	// sanity checking

	test = false;
	if (song.len > 128)
		test = true;

	for (i = 100; i < 256; ++i)
	{
		if (patt[i] != NULL)
		{
			test = true;
			break;
		}
	}
	if (test) okBoxThreadSafe(0, "System message", "Too many patterns!");

	for (i = 32; i < 128; ++i)
	{
		if (getRealUsedSamples(i) > 0)
		{
			okBoxThreadSafe(0, "System message", "Too many instruments!");
			break;
		}
	}

	test = false;
	for (i = 1; i <= 31; ++i)
	{
		ins = &instr[i];
		smp = &ins->samp[0];

		j = getRealUsedSamples(i);
		if (j > 1)
		{
			test = true;
			break;
		}

		if (j == 1)
		{
			if ((smp->len > 65534)  || (ins->envVTyp != 0)   ||
				(ins->envPTyp != 0) || ((smp->typ & 3) == 2) ||
				(smp->relTon  != 0) || ins->midiOn)
			{
				test = true;
				break;
			}
		}
	}
	if (test) okBoxThreadSafe(0, "System message", "Incompatible instruments!");

	for (i = 0; i < 99; ++i)
	{
		if (patt[i] != NULL)
		{
			if (pattLens[i] != 64)
			{
				okBoxThreadSafe(0, "System message", "Unable to convert module. (Illegal pattern length)");
				return (false);
			}

			for (j = 0; j < 64; ++j)
			{
				for (k = 0; k < song.antChn; ++k)
				{
					t = &patt[i][(j * MAX_VOICES) + k];

					if (t->instr > 31)
						tooManyInstr = true;

					if ((t->effTyp > 15) || (t->vol != 0))
						incompatEfx = true;

					// added security that wasn't present in FT2
					if ((t->ton > 0) && (t->ton < 10))
						noteUnderflow = true;
				}
			}
		}
	}
	if (tooManyInstr)  okBoxThreadSafe(0, "System message", "Instrument(s) above 31 was found in pattern data!");
	if (incompatEfx)   okBoxThreadSafe(0, "System message", "Incompatible effect(s) was found in pattern data!");
	if (noteUnderflow) okBoxThreadSafe(0, "System message", "Note(s) below A-0 was found in pattern data!");

	// calculate number of patterns

	ap = 0;
	for (i = 0; i < 128; ++i)
	{
		if (song.songTab[i] > ap)
			ap = song.songTab[i];
	}

	// setup header buffer

	memset(&hm, 0, sizeof (hm));
	memcpy(hm.name, song.name, sizeof (hm.name));
	hm.len = (uint8_t)(song.len);
	if (hm.len > 128) hm.len = 128;
	hm.repS = (uint8_t)(song.repS);
	if (hm.repS > 127) hm.repS = 0;
	memcpy(hm.songTab, song.songTab, 128);

	if (song.antChn == 4)
		memcpy(hm.sig, (ap > 64) ? "M!K!" : "M.K.", 4);
	else
		memcpy(hm.sig, modSig[song.antChn - 1], 4);

	// read sample information into header buffer
	for (i = 0; i < 31; ++i)
	{
		memcpy(hm.instr[i].name, song.instrName[1 + i], sizeof (hm.instr[0].name));
		if (getRealUsedSamples(1 + i) != 0)
		{
			smp = &instr[1 + i].samp[0];

			l1 = smp->len  / 2;
			l2 = smp->repS / 2;
			l3 = smp->repL / 2;

			if (smp->typ & 16)
			{
				l1 /= 2;
				l2 /= 2;
				l3 /= 2;
			}

			if (l1 > 32767)
				l1 = 32767;

			if (l2 > l1)
				l2 = l1;

			if ((l2 + l3) > l1)
				l3 = l1 - l2;

			// FT2 bug-fix
			if (l3 < 1)
			{
				l2 = 0;
				l3 = 1;
			}

			hm.instr[i].len  = (uint16_t)(SWAP16(l1));
			hm.instr[i].fine = ((smp->fine + 128) >> 4) ^ 8;
			hm.instr[i].vol  = smp->vol;

			if ((smp->typ & 3) == 0)
			{
				hm.instr[i].repS = 0;
				hm.instr[i].repL = SWAP16(1);
			}
			else
			{
				hm.instr[i].repS = (uint16_t)(SWAP16(l2));
				hm.instr[i].repL = (uint16_t)(SWAP16(l3));
			}
		}

		// FT2 bugfix: never allow replen being below 2 (1)
		if (SWAP16(hm.instr[i].repL) < 1)
		{
			hm.instr[i].repS = SWAP16(0);
			hm.instr[i].repL = SWAP16(1);
		}
	}

	f = UNICHAR_FOPEN(filenameU, "wb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "Error opening file for saving, is it in use?");
		return (false);
	}

	// write header
	if (fwrite(&hm, 1, sizeof (hm), f) != sizeof (hm))
	{
		fclose(f);
		okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
		return (false);
	}

	// write pattern data
	for (i = 0; i <= ap; ++i)
	{
		if (patt[i] == NULL)
		{
			// empty pattern
			memset(pattBuff, 0, 64 * 4 * song.antChn);
		}
		else
		{
			a = 0;
			for (j = 0; j < 64; ++j)
			{
				for (k = 0; k < song.antChn; ++k)
				{
					t = &patt[i][(j * MAX_VOICES) + k];

					inst = t->instr;
					ton  = t->ton;

					// FT2 bugfix: prevent
					if (inst > 31)
						inst = 0;

					// FT2 bugfix: clamp notes below 10 (A-0) to prevent 12-bit period overflow
					if ((ton > 0) && (ton < 10))
						ton = 10;

					if (t->ton == 0)
					{
						pattBuff[a + 0] = inst & 0xF0;
						pattBuff[a + 1] = 0;
					}
					else
					{
						pattBuff[a + 0] = (inst & 0xF0) | ((amigaPeriod[ton - 1] >> 8) & 0x0F);
						pattBuff[a + 1] = amigaPeriod[ton - 1] & 0xFF;
					}

					// FT2 bugfix: if effect is overflowing (0xF in .MOD), set effect and param to 0
					if (t->effTyp > 0x0F)
					{
						pattBuff[a + 2] = (inst & 0x0F) << 4;
						pattBuff[a + 3] = 0;
					}
					else
					{
						pattBuff[a + 2] = ((inst & 0x0F) << 4) | (t->effTyp & 0x0F);
						pattBuff[a + 3] = t->eff;
					}

					a += 4;
				}
			}
		}

		if (fwrite(pattBuff, 1, 64 * 4 * song.antChn, f) != (size_t)(64 * 4 * song.antChn))
		{
			fclose(f);
			okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
			return (false);
		}
	}

	// write sample data
	for (i = 0; i < 31; ++i)
	{
		if (getRealUsedSamples(1 + i) != 0)
		{
		   smp = &instr[1 + i].samp[0];
		   if (smp->len <= 0)
			   continue;

			restoreSample(smp);

			l1 = smp->len / 2;
			if (smp->typ & 16)
			{
				// 16-bit sample (convert to 8-bit)

				if (l1 > 65534)
					l1 = 65534;

				ptr16 = (int16_t *)(smp->pek);
				for (j = 0; j < l1; ++j)
				{
					smp8 = ptr16[j] >> 8;
					if (fwrite(&smp8, 1, 1, f) != 1)
					{
						fixSample(smp);
						fclose(f);
						okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
						return (false);
					}
				}
			}
			else
			{
				// 8-bit sample

				if (l1 > 32767)
					l1 = 32767;
				l1 *= 2;

				if (fwrite(smp->pek, 1, l1, f) != (size_t)(l1))
				{
					fixSample(smp);
					fclose(f);
					okBoxThreadSafe(0, "System message", "Error saving module: general I/O error!");
					return (false);
				}
			}

			fixSample(smp);
		}
	}

	fclose(f);
	removeSongModifiedFlag();

	editor.diskOpReadDir = true; // force diskop re-read

	setMouseBusy(false);
	return (true);
}

static int32_t SDLCALL saveMusicThread(void *ptr)
{
	(void)(ptr);

	assert(editor.tmpFilenameU != NULL);;
	if (editor.tmpFilenameU == NULL)
		return (false);

	pauseAudio();

	if (editor.moduleSaveMode == 1)
		saveXM(editor.tmpFilenameU);
	else
		saveMOD(editor.tmpFilenameU);

	resumeAudio();

	return (true);
}

void saveMusic(UNICHAR *filenameU)
{
	UNICHAR_STRCPY(editor.tmpFilenameU, filenameU);

	mouseAnimOn();
	thread = SDL_CreateThread(saveMusicThread, NULL, NULL);
	if (thread == NULL)
	{
		okBoxThreadSafe(0, "System message", "Couldn't create thread!");
		return;
	}

	SDL_DetachThread(thread);
}

static uint16_t packPatt(uint8_t *pattPtr, uint16_t numRows)
{
	uint8_t bytes[5], packBits, *writePtr, *firstBytePtr;
	uint16_t row, chn, totalPackLen;

	totalPackLen = 0;

	if (pattPtr == NULL)
		return (0);

	writePtr = pattPtr;
	for (row = 0; row < numRows; ++row)
	{
		for (chn = 0; chn < song.antChn; ++chn)
		{
			bytes[0] = *pattPtr++;
			bytes[1] = *pattPtr++;
			bytes[2] = *pattPtr++;
			bytes[3] = *pattPtr++;
			bytes[4] = *pattPtr++;

			firstBytePtr = writePtr++;

			packBits = 0;
			if (bytes[0] > 0) { packBits |= 1; *writePtr++ = bytes[0]; } // note
			if (bytes[1] > 0) { packBits |= 2; *writePtr++ = bytes[1]; } // instrument
			if (bytes[2] > 0) { packBits |= 4; *writePtr++ = bytes[2]; } // volume column
			if (bytes[3] > 0) { packBits |= 8; *writePtr++ = bytes[3]; } // effect

			if (packBits == 15) // first four bits set?
			{
				// no packing needed, write pattern data as is

				// point to first byte (and overwrite data)
				writePtr = firstBytePtr;

				*writePtr++ = bytes[0];
				*writePtr++ = bytes[1];
				*writePtr++ = bytes[2];
				*writePtr++ = bytes[3];
				*writePtr++ = bytes[4];

				totalPackLen += 5;
				continue;
			}

			if (bytes[4] > 0) { packBits |= 16; *writePtr++ = bytes[4]; } // effect parameter

			*firstBytePtr = packBits | 128; // write pack bits byte
			totalPackLen += (uint16_t)(writePtr - firstBytePtr); // bytes writen
		}

		// skip unused channels
		pattPtr += (sizeof (tonTyp) * (MAX_VOICES - song.antChn));
	}

	return (totalPackLen);
}
