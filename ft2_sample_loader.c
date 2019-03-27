// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "ft2_header.h"
#include "ft2_gui.h"
#include "ft2_unicode.h"
#include "ft2_audio.h"
#include "ft2_sample_ed.h"
#include "ft2_mouse.h"
#include "ft2_diskop.h"

enum
{
	STEREO_SAMPLE_READ_LEFT  = 1,
	STEREO_SAMPLE_READ_RIGHT = 2,
	STEREO_SAMPLE_CONVERT    = 3,

	WAV_FORMAT_PCM        = 0x0001,
	WAV_FORMAT_IEEE_FLOAT = 0x0003
};

static volatile bool sampleIsLoading;
static bool loadAsInstrFlag;
static uint8_t sampleSlot;
static int16_t stereoSampleLoadMode;
static SDL_Thread *thread;

static void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength);
static void normalize24bitSigned(int32_t *sampleData, uint32_t sampleLength);
static void normalize16bitFloatSigned(float *fSampleData, uint32_t sampleLength);
static void normalize64bitDoubleSigned(double *dSampleData, uint32_t sampleLength);

void removeSampleIsLoadingFlag(void)
{
	sampleIsLoading = false;
}

static double aiffRateToDouble(uint8_t *in)
{
	int32_t neg, exp;
	uint32_t lo, hi;
	double dOut;

	neg = (int32_t)(in[0] >> 7);
	exp = (int32_t)(((in[0] & 0x7F) << 8) | in[1]);
	lo  = (in[2] << 24) | (in[3] << 16) | (in[4] << 8) | in[5];
	hi  = (in[6] << 24) | (in[7] << 16) | (in[8] << 8) | in[9];

	if ((exp == 0) && (lo == 0) && (hi == 0))
		return (0.0);

	exp -= 16383;

	dOut = ldexp(lo, -31 + exp) + ldexp(hi, -63 + exp);
	return (neg ? -dOut : dOut);
}

static bool aiffIsStereo(FILE *f) // only ran on files that are confirmed to be AIFFs
{
	bool stereoFlag;
	uint16_t numChannels;
	int32_t bytesRead, endOfChunk, filesize;
	uint32_t chunkID, chunkSize, commPtr, commLen;
	uint32_t oldPos;

	oldPos = ftell(f);

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);

	if (filesize < 12)
	{
		fseek(f, oldPos, SEEK_SET);
		return (false);
	}

	fseek(f, 12, SEEK_SET);

	commPtr = 0; commLen = 0;

	bytesRead = 0;
	while (!feof(f) && (bytesRead < (filesize - 12)))
	{
		fread(&chunkID,   4, 1, f); chunkID   = SWAP32(chunkID);   if (feof(f)) break;
		fread(&chunkSize, 4, 1, f); chunkSize = SWAP32(chunkSize); if (feof(f)) break;

		endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
		switch (chunkID)
		{
			case 0x434F4D4D: // "COMM"
			{
				commPtr = ftell(f);
				commLen = chunkSize;
			}
			break;

			default: break;
		}

		bytesRead += (chunkSize + (chunkSize & 1));
		fseek(f, endOfChunk, SEEK_SET);
	}

	if ((commPtr == 0) || (commLen < 2))
	{
		fseek(f, oldPos, SEEK_SET);
		return (false);
	}

	fseek(f, commPtr, SEEK_SET);
	fread(&numChannels, 2, 1, f); numChannels = SWAP16(numChannels);

	stereoFlag = false;
	if (numChannels == 2)
		stereoFlag = true;

	fseek(f, oldPos, SEEK_SET);
	return (stereoFlag);
}

static bool wavIsStereo(FILE *f) // only ran on files that are confirmed to be WAVs
{
	bool stereoFlag;
	uint16_t numChannels;
	int32_t bytesRead, endOfChunk, filesize;
	uint32_t chunkID, chunkSize, fmtPtr, fmtLen;
	uint32_t oldPos;

	oldPos = ftell(f);

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);

	if (filesize < 12)
	{
		fseek(f, oldPos, SEEK_SET);
		return (false);
	}

	fseek(f, 12, SEEK_SET);

	fmtPtr = 0;
	fmtLen = 0;

	bytesRead = 0;
	while (!feof(f) && (bytesRead < (filesize - 12)))
	{
		fread(&chunkID,   4, 1, f); if (feof(f)) break;
		fread(&chunkSize, 4, 1, f); if (feof(f)) break;

		endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
		switch (chunkID)
		{
			case 0x20746D66: // "fmt "
			{
				fmtPtr = ftell(f);
				fmtLen = chunkSize;
			}
			break;

			default: break;
		}

		bytesRead += (chunkSize + (chunkSize & 1));
		fseek(f, endOfChunk, SEEK_SET);
	}

	if ((fmtPtr == 0) || (fmtLen < 4))
	{
		fseek(f, oldPos, SEEK_SET);
		return (false);
	}

	fseek(f, fmtPtr + 2, SEEK_SET);
	fread(&numChannels, 2, 1, f);

	stereoFlag = false;
	if (numChannels == 2)
		stereoFlag = true;

	fseek(f, oldPos, SEEK_SET);
	return (stereoFlag);
}

static int32_t SDLCALL loadAIFFSample(void *ptr)
{
	char *tmpFilename, *tmpPtr, compType[4];
	bool is16Bit;
	uint8_t sampleRateBytes[10], *audioDataU8;
	int16_t *audioDataS16, smp16;
	uint16_t numChannels, bitDepth;
	int32_t j, filesize, smp32, *audioDataS32;
	uint32_t i, filenameLen, sampleRate, sampleLength, blockName, blockSize;
	uint32_t commPtr, commLen, ssndPtr, ssndLen, offset, len32;
	int64_t smp64;
	FILE *f;
	UNICHAR *filename;
	sampleTyp tmpSmp, *s;

	// this is important for the "goto" on load error
	f = NULL;
	tmpSmp.pek = NULL;

	if (editor.tmpFilenameU == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto aiffLoadError;
	}

	filename = editor.tmpFilenameU;

	f = UNICHAR_FOPEN(filename, "rb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto aiffLoadError;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize < 12)
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto aiffLoadError;
	}

	// handle chunks

	commPtr = 0; commLen = 0;
	ssndPtr = 0; ssndLen = 0;

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && (ftell(f) < (filesize - 12)))
	{
		fread(&blockName, 4, 1, f); if (feof(f)) break;
		fread(&blockSize, 4, 1, f); if (feof(f)) break;

		blockName = SWAP32(blockName);
		blockSize = SWAP32(blockSize);

		switch (blockName)
		{
			case 0x434F4D4D: // "COMM"
			{
				commPtr = ftell(f);
				commLen = blockSize;
			}
			break;

			case 0x53534E44: // "SSND"
			{
				ssndPtr = ftell(f);
				ssndLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if ((commPtr == 0) || (commLen < 18) || (ssndPtr == 0))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto aiffLoadError;
	}

	// kludge for some really strange AIFFs
	if (ssndLen == 0)
		ssndLen = filesize - ssndPtr;

	if ((ssndPtr + ssndLen) > (uint32_t)(filesize))
		ssndLen = filesize - ssndPtr;

	fseek(f, commPtr, SEEK_SET);
	fread(&numChannels, 2, 1, f); numChannels = SWAP16(numChannels);
	fseek(f, 4, SEEK_CUR);
	fread(&bitDepth, 2, 1, f); bitDepth = SWAP16(bitDepth);
	fread(sampleRateBytes, 1, 10, f);

	if ((numChannels != 1) && (numChannels != 2))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: Unsupported amounts of channels!");
		goto aiffLoadError;
	}

	if ((bitDepth != 8) && (bitDepth != 16) && (bitDepth != 24) && (bitDepth != 32))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: Unsupported bitdepth!");
		goto aiffLoadError;
	}

	// read compression type (if present)
	if (commLen > 18)
	{
		fread(&compType, 1, 4, f);
		if (memcmp(compType, "NONE", 4))
		{
			okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
			goto aiffLoadError;
		}
	}

	sampleRate = (int32_t)(round(aiffRateToDouble(sampleRateBytes)));

	// sample data chunk

	fseek(f, ssndPtr, SEEK_SET);

	fread(&offset, 4, 1, f);
	if (offset > 0)
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto aiffLoadError;
	}

	fseek(f, 4, SEEK_CUR);

	ssndLen -= 8; // don't include offset and blockSize datas

	sampleLength = ssndLen;
	if (sampleLength > MAX_SAMPLE_LEN)
		sampleLength = MAX_SAMPLE_LEN;

	is16Bit = bitDepth > 8;

	tmpSmp.pek = NULL;
	freeSample(&tmpSmp);

	// read sample data

	if (bitDepth == 8)
	{
		// 8-BIT SIGNED PCM

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}

		if (fread(tmpSmp.pek, sampleLength, 1, f) != 1)
		{
			okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
			goto aiffLoadError;
		}

		if (numChannels == 2)
		{
			sampleLength /= 2;

			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					for (i = 1; i < sampleLength; i++)
						tmpSmp.pek[i] = tmpSmp.pek[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						tmpSmp.pek[i] = tmpSmp.pek[(i * 2) + 1];

					tmpSmp.pek[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
					{
						smp16 = tmpSmp.pek[(i * 2) + 0] + tmpSmp.pek[(i * 2) + 1];
						tmpSmp.pek[i] = (int8_t)(smp16 >> 1);
					}

					tmpSmp.pek[i] = 0;
				}
				break;
			}

			// decrease the memory allocation size now
			tmpSmp.pek = (int8_t *)(realloc(tmpSmp.pek, sampleLength + 4));
			if (tmpSmp.pek == NULL)
			{
				okBoxThreadSafe(0, "System message", "Not enough memory!");
				goto aiffLoadError;
			}
		}
	}
	else if (bitDepth == 16)
	{
		// 16-BIT SIGNED PCM

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}

		if (fread(tmpSmp.pek, sampleLength, 1, f) != 1)
		{
			okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
			goto aiffLoadError;
		}

		// fix endianness
		audioDataS16 = (int16_t *)(tmpSmp.pek);
		len32 = sampleLength / 2;

		for (i = 0; i < len32; ++i)
			audioDataS16[i] = SWAP16(audioDataS16[i]);

		if (numChannels == 2)
		{
			sampleLength /= 2;

			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					audioDataS16 = (int16_t *)(tmpSmp.pek);
					len32 = sampleLength / 2;

					for (i = 1; i < len32; i++)
						audioDataS16[i] = audioDataS16[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					audioDataS16 = (int16_t *)(tmpSmp.pek);
					len32 = (sampleLength / 2) - 1;

					for (i = 0; i < len32; i++)
						audioDataS16[i] = audioDataS16[(i * 2) + 1];

					audioDataS16[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					audioDataS16 = (int16_t *)(tmpSmp.pek);
					len32 = (sampleLength / 2) - 1;

					for (i = 0; i < len32; i++)
					{
						smp32 = audioDataS16[(i * 2) + 0] + audioDataS16[(i * 2) + 1];
						audioDataS16[i] = (int16_t)(smp32 >> 1);
					}

					audioDataS16[i] = 0;
				}
				break;
			}

			// decrease the memory allocation size now
			tmpSmp.pek = (int8_t *)(realloc(tmpSmp.pek, sampleLength + 4));
			if (tmpSmp.pek == NULL)
			{
				okBoxThreadSafe(0, "System message", "Not enough memory!");
				goto aiffLoadError;
			}
		}

		sampleLength &= 0xFFFFFFFE;
	}
	else if (bitDepth == 24)
	{
		// 24-BIT SIGNED PCM

		sampleLength = (sampleLength * 4) / 3;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}

		// read sample data
		audioDataU8 = (uint8_t *)(tmpSmp.pek);
		len32 = sampleLength / 4;

		for (i = 0; i < len32; i++)
		{
			fread(audioDataU8, 3, 1, f);
			audioDataU8[3] = 0;

			audioDataU8 += sizeof (int32_t);
		}

		// fix endianness
		audioDataS32 = (int32_t *)(tmpSmp.pek);
		len32 = sampleLength / 4;

		for (i = 0; i < len32; ++i)
			audioDataS32[i] = SWAP32(audioDataS32[i]);

		if (numChannels == 2)
		{
			sampleLength /= 2;

			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = sampleLength / 4;

					for (i = 1; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = (sampleLength / 4) - 1;

					for (i = 0; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 1];

					audioDataS32[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = (sampleLength / 4) - 1;

					for (i = 0; i < len32; i++)
					{
						smp64   = audioDataS32[(i * 2) + 0];
						smp64  += audioDataS32[(i * 2) + 1];
						smp64 >>= 1;

						audioDataS32[i] = (int32_t)(smp64);
					}

					audioDataS32[i] = 0;
				}
				break;
			}
		}

		normalize24bitSigned(audioDataS32, sampleLength / 4);

		// downscale to 16-bit

		audioDataS16 = (int16_t *)(tmpSmp.pek);
		audioDataS32 = (int32_t *)(tmpSmp.pek);
		len32 = sampleLength / 4;

		for (i = 0; i < len32; ++i)
			audioDataS16[i] = (int16_t)(audioDataS32[i] >> 8);

		sampleLength /= 2;
		sampleLength &= 0xFFFFFFFE;

		// decrease the memory allocation size now
		tmpSmp.pek = (int8_t *)(realloc(tmpSmp.pek, sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}
	}
	else if (bitDepth == 32)
	{
		// 32-BIT SIGNED PCM

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}

		if (fread(tmpSmp.pek, sampleLength, 1, f) != 1)
		{
			okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
			goto aiffLoadError;
		}

		// fix endianness
		audioDataS32 = (int32_t *)(tmpSmp.pek);
		len32 = sampleLength / 4;

		for (i = 0; i < len32; ++i)
			audioDataS32[i] = SWAP32(audioDataS32[i]);

		if (numChannels == 2)
		{
			sampleLength /= 2;

			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = sampleLength / 4;

					for (i = 1; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = (sampleLength / 4) - 1;

					for (i = 0; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 1];

					audioDataS32[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					audioDataS32 = (int32_t *)(tmpSmp.pek);
					len32 = (sampleLength / 4) - 1;

					for (i = 0; i < len32; i++)
					{
						smp64   = audioDataS32[(i * 2) + 0];
						smp64  += audioDataS32[(i * 2) + 1];
						smp64 >>= 1;

						audioDataS32[i] = (int32_t)(smp64);
					}

					audioDataS32[i] = 0;
				}
				break;
			}
		}

		normalize32bitSigned(audioDataS32, sampleLength / 4);

		// downscale to 16-bit

		audioDataS16 = (int16_t *)(tmpSmp.pek);
		audioDataS32 = (int32_t *)(tmpSmp.pek);
		len32 = sampleLength / 4;

		for (i = 0; i < len32; ++i)
			audioDataS16[i] = (int16_t)(audioDataS32[i] >> 16);

		sampleLength /= 2;
		sampleLength &= 0xFFFFFFFE;

		// decrease the memory allocation size now
		tmpSmp.pek = (int8_t *)(realloc(tmpSmp.pek, sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto aiffLoadError;
		}
	}

	// set sample attributes
	tmpSmp.len = sampleLength;

	if (is16Bit)
		tmpSmp.typ |= 16;

	tmpSmp.vol = 64;
	tmpSmp.pan = 128;

	tuneSample(&tmpSmp, sampleRate);

	// set sample name
	tmpFilename = unicharToCp437(filename, true);
	if (tmpFilename != NULL)
	{
		j = (int32_t)(strlen(tmpFilename));
		while (j--)
		{
			if (tmpFilename[j] == DIR_DELIMITER)
				break;
		}

		tmpPtr = tmpFilename;
		if (j > 0)
			tmpPtr += (j + 1);

		sanitizeFilename(tmpPtr);

		filenameLen = (uint32_t)(strlen(tmpPtr));
		for (i = 0; i < 22; ++i)
		{
			if (i < filenameLen)
				tmpSmp.name[i] = tmpPtr[i];
			else
				tmpSmp.name[i] = '\0';
		}

		free(tmpFilename);
	}

	fclose(f);

	// if loaded in instrument mode
	if (loadAsInstrFlag)
		clearInstr(editor.curInstr);

	s = &instr[editor.curInstr].samp[sampleSlot];

	lockMixerCallback();
	freeSample(s);
	memcpy(s, &tmpSmp, sizeof (sampleTyp));
	fixSample(s);
	unlockMixerCallback();

	setSongModifiedFlag();
	stereoSampleLoadMode = -1;

	// also sets mouse busy to false when done
	editor.updateCurSmp = true;

	(void)(ptr); // prevent compiler warning
	return (true);

aiffLoadError:
	if (f != NULL) fclose(f);
	if (tmpSmp.pek != NULL) free(tmpSmp.pek);

	stereoSampleLoadMode = -1;
	sampleIsLoading = false;

	return (false);
}

static int32_t SDLCALL loadIFFSample(void *ptr)
{
	char *tmpFilename, *tmpPtr, hdr[4 + 1];
	bool is16Bit;
	uint8_t i;
	uint16_t sampleRate;
	int32_t j, filesize;
	uint32_t filenameLen, sampleVol, sampleLength, sampleLoopStart, sampleLoopLength, blockName, blockSize;
	uint32_t vhdrPtr, vhdrLen, bodyPtr, bodyLen, namePtr, nameLen;
	FILE *f;
	UNICHAR *filename;
	sampleTyp tmpSmp, *s;

	// this is important for the "goto" on load error
	f = NULL;
	tmpSmp.pek = NULL;

	if (editor.tmpFilenameU == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto iffLoadError;
	}

	filename = editor.tmpFilenameU;

	f = UNICHAR_FOPEN(filename, "rb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto iffLoadError;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize < 12)
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto iffLoadError;
	}

	fseek(f, 8, SEEK_SET);
	fread(hdr, 1, 4, f);
	hdr[4] = '\0';
	is16Bit = !strncmp(hdr, "16SV", 4);

	sampleLength     = 0;
	sampleVol        = 64;
	sampleLoopStart  = 0;
	sampleLoopLength = 0;
	sampleRate       = 16726;

	vhdrPtr = 0; vhdrLen = 0;
	bodyPtr = 0; bodyLen = 0;
	namePtr = 0; nameLen = 0;

	fseek(f, 12, SEEK_SET);
	while (!feof(f) && (ftell(f) < (filesize - 12)))
	{
		fread(&blockName, 4, 1, f); if (feof(f)) break;
		fread(&blockSize, 4, 1, f); if (feof(f)) break;

		blockName = SWAP32(blockName);
		blockSize = SWAP32(blockSize);

		switch (blockName)
		{
			case 0x56484452: // VHDR
			{
				vhdrPtr = ftell(f);
				vhdrLen = blockSize;
			}
			break;

			case 0x4E414D45: // NAME
			{
				namePtr = ftell(f);
				nameLen = blockSize;
			}
			break;

			case 0x424F4459: // BODY
			{
				bodyPtr = ftell(f);
				bodyLen = blockSize;
			}
			break;

			default: break;
		}

		fseek(f, blockSize + (blockSize & 1), SEEK_CUR);
	}

	if ((vhdrPtr == 0) || (vhdrLen < 20) || (bodyPtr == 0))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto iffLoadError;
	}

	// kludge for some really strange IFFs
	if (bodyLen == 0)
		bodyLen = filesize - bodyPtr;

	if ((bodyPtr + bodyLen) > (uint32_t)(filesize))
		bodyLen = filesize - bodyPtr;

	fseek(f, vhdrPtr, SEEK_SET);
	fread(&sampleLoopStart,  4, 1, f); sampleLoopStart  = SWAP32(sampleLoopStart);
	fread(&sampleLoopLength, 4, 1, f); sampleLoopLength = SWAP32(sampleLoopLength);
	fseek(f, 4, SEEK_CUR);
	fread(&sampleRate, 2, 1, f); sampleRate = SWAP16(sampleRate);
	fseek(f, 1, SEEK_CUR);

	if (fgetc(f) != 0) // sample type
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported!");
		goto iffLoadError;
	}

	fread(&sampleVol, 4, 1, f); sampleVol = SWAP32(sampleVol);
	if (sampleVol > 65536)
		sampleVol = 65536;

	sampleVol = (uint32_t)(round(sampleVol / 1024.0));

	sampleLength = bodyLen;
	if (sampleLength > MAX_SAMPLE_LEN)
		sampleLength = MAX_SAMPLE_LEN;

	if ((sampleLoopStart >= MAX_SAMPLE_LEN) || (sampleLoopLength > MAX_SAMPLE_LEN))
	{
		sampleLoopStart  = 0;
		sampleLoopLength = 0;
	}

	if ((sampleLoopStart + sampleLoopLength) > sampleLength)
	{
		sampleLoopStart  = 0;
		sampleLoopLength = 0;
	}

	if (sampleLoopStart > (sampleLength - 2))
	{
		sampleLoopStart  = 0;
		sampleLoopLength = 0;
	}

	tmpSmp.pek = NULL;
	freeSample(&tmpSmp);

	tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
	if (tmpSmp.pek == NULL)
	{
		okBoxThreadSafe(0, "System message", "Not enough memory!");
		goto iffLoadError;
	}

	fseek(f, bodyPtr, SEEK_SET);
	if (fread(tmpSmp.pek, sampleLength, 1, f) != 1)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto iffLoadError;
	}

	// set sample attributes
	tmpSmp.len = sampleLength;

	if (sampleLoopLength > 2)
	{
		tmpSmp.repS = sampleLoopStart;
		tmpSmp.repL = sampleLoopLength;
		tmpSmp.typ |= 1;
	}

	if (is16Bit)
	{
		tmpSmp.repS &= 0xFFFFFFFE;
		tmpSmp.repL &= 0xFFFFFFFE;
		tmpSmp.typ |= 16;
	}

	tmpSmp.vol = (uint8_t)(sampleVol);
	tmpSmp.pan = 128;

	tuneSample(&tmpSmp, sampleRate);

	// set name
	if ((namePtr != 0) && (nameLen > 0))
	{
		fseek(f, namePtr, SEEK_SET);
		if (nameLen > 21)
		{
			tmpSmp.name[21] = '\0';
			fread(tmpSmp.name, 1, 21, f);
		}
		else
		{
			memset(tmpSmp.name, 0, 22);
			fread(tmpSmp.name, 1, nameLen, f);
		}
	}
	else
	{
		// set sample name from filename if we didn't load name from .wav
		tmpFilename = unicharToCp437(filename, true);
		if (tmpFilename != NULL)
		{
			j = (int32_t)(strlen(tmpFilename));
			while (j--)
			{
				if (tmpFilename[j] == DIR_DELIMITER)
					break;
			}

			tmpPtr = tmpFilename;
			if (j > 0)
				tmpPtr += (j + 1);

			sanitizeFilename(tmpPtr);

			filenameLen = (uint32_t)(strlen(tmpPtr));
			for (i = 0; i < 22; ++i)
			{
				if (i < filenameLen)
					tmpSmp.name[i] = tmpPtr[i];
				else
					tmpSmp.name[i] = '\0';
			}

			free(tmpFilename);
		}
	}

	fclose(f);

	// if loaded in instrument mode
	if (loadAsInstrFlag)
		clearInstr(editor.curInstr);

	s = &instr[editor.curInstr].samp[sampleSlot];

	lockMixerCallback();
	freeSample(s);
	memcpy(s, &tmpSmp, sizeof (sampleTyp));
	fixSample(s);
	unlockMixerCallback();

	setSongModifiedFlag();
	stereoSampleLoadMode = -1;

	// also sets mouse busy to false when done
	editor.updateCurSmp = true;

	return (true);

	(void)(ptr); // prevent compiler warning

iffLoadError:
	if (f != NULL) fclose(f);
	if (tmpSmp.pek != NULL) free(tmpSmp.pek);

	stereoSampleLoadMode = -1;
	sampleIsLoading = false;

	return (false);
}

static int32_t SDLCALL loadRawSample(void *ptr)
{
	char *tmpFilename, *tmpPtr;
	int32_t j;
	uint32_t filenameLen, i, filesize;
	FILE *f;
	UNICHAR *filename;
	sampleTyp tmpSmp, *s;

	// this is important for the "goto" on load error
	f = NULL;
	tmpSmp.pek = NULL;

	if (editor.tmpFilenameU == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto rawLoadError;
	}

	filename = editor.tmpFilenameU;

	f = UNICHAR_FOPEN(filename, "rb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto rawLoadError;
	}

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	rewind(f);

	if (filesize == 0)
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto rawLoadError;
	}

	tmpSmp.pek = NULL;
	freeSample(&tmpSmp);

	tmpSmp.pek = (int8_t *)(malloc(filesize + 4));
	if (tmpSmp.pek == NULL)
	{
		okBoxThreadSafe(0, "System message", "Not enough memory!");
		goto rawLoadError;
	}

	if (fread(tmpSmp.pek, filesize, 1, f) != 1)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto rawLoadError;
	}

	fclose(f);

	tmpFilename = unicharToCp437(filename, true);
	if (tmpFilename != NULL)
	{
		j = (int32_t)(strlen(tmpFilename));
		while (j--)
		{
			if (tmpFilename[j] == DIR_DELIMITER)
				break;
		}

		tmpPtr = tmpFilename;
		if (j > 0)
			tmpPtr += (j + 1);

		sanitizeFilename(tmpPtr);

		filenameLen = (uint32_t)(strlen(tmpPtr));
		for (i = 0; i < 22; ++i)
		{
			if (i < filenameLen)
				tmpSmp.name[i] = tmpPtr[i];
			else
				tmpSmp.name[i] = '\0';
		}

		free(tmpFilename);
	}

	tmpSmp.len = filesize;
	tmpSmp.vol = 64;
	tmpSmp.pan = 128;

	// if loaded in instrument mode
	if (loadAsInstrFlag)
		clearInstr(editor.curInstr);

	s = &instr[editor.curInstr].samp[sampleSlot];

	lockMixerCallback();
	freeSample(s);
	memcpy(s, &tmpSmp, sizeof (sampleTyp));
	fixSample(s);
	unlockMixerCallback();

	setSongModifiedFlag();
	stereoSampleLoadMode = -1;

	// also sets mouse busy to false when done
	editor.updateCurSmp = true;

	return (true);

	(void)(ptr); // prevent compiler warning

rawLoadError:
	if (f != NULL) fclose(f);
	if (tmpSmp.pek != NULL) free(tmpSmp.pek);

	stereoSampleLoadMode = -1;
	sampleIsLoading = false;

	return (false);
}

static int32_t SDLCALL loadWAVSample(void *ptr)
{
	char chr, *tmpFilename, *tmpPtr;
	uint8_t *audioDataU8;
	int16_t *audioDataS16, *ptr16;
	uint16_t audioFormat, numChannels, bitsPerSample, tempPan, tempVol;
	int32_t j, *audioDataS32, smp32;
	uint32_t filenameLen, i, *audioDataU32, sampleRate, chunkID, chunkSize, sampleLength, filesize;
	uint32_t numLoops, loopType, loopStart, loopEnd, bytesRead, endOfChunk, dataPtr, dataLen, fmtPtr;
	uint32_t fmtLen, inamPtr, inamLen, smplPtr, smplLen, xtraPtr, xtraLen, xtraFlags, len32;
	int64_t smp64;
	float fSmp, *fAudioDataFloat;
	double *dAudioDataDouble, dSmp;
	FILE *f;
	sampleTyp tmpSmp, *s;
	UNICHAR *filename;

	// this is important for the "goto" on load error
	f = NULL;
	tmpSmp.pek = NULL;

	// zero out memory pointers
	audioDataU8      = NULL;
	audioDataS16     = NULL;
	audioDataS32     = NULL;
	audioDataU32     = NULL;
	dAudioDataDouble = NULL;

	// zero out chunk pointers and lengths
	fmtPtr  = 0; fmtLen  = 0;
	dataPtr = 0; dataLen = 0;
	inamPtr = 0; inamLen = 0;
	xtraPtr = 0; xtraLen = 0;
	smplPtr = 0; smplLen = 0;

	if (editor.tmpFilenameU == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto wavLoadError;
	}

	filename = editor.tmpFilenameU;

	f = UNICHAR_FOPEN(filename, "rb");
	if (f == NULL)
	{
		okBoxThreadSafe(0, "System message", "General I/O error during loading! Is the file in use?");
		goto wavLoadError;
	}

	// get and check filesize
	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	if (filesize < 12)
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto wavLoadError;
	}

	// look for wanted chunks and set up pointers + lengths
	fseek(f, 12, SEEK_SET);

	bytesRead = 0;
	while (!feof(f) && (bytesRead < (filesize - 12)))
	{
		fread(&chunkID,   4, 1, f); if (feof(f)) break;
		fread(&chunkSize, 4, 1, f); if (feof(f)) break;

		endOfChunk = (ftell(f) + chunkSize) + (chunkSize & 1);
		switch (chunkID)
		{
			case 0x20746D66: // "fmt "
			{
				fmtPtr = ftell(f);
				fmtLen = chunkSize;
			}
			break;

			case 0x61746164: // "data"
			{
				dataPtr = ftell(f);
				dataLen = chunkSize;
			}
			break;

			case 0x5453494C: // "LIST"
			{
				if (chunkSize >= 4)
				{
					fread(&chunkID, 4, 1, f);
					if (chunkID == 0x4F464E49) // "INFO"
					{
						bytesRead = 0;
						while (!feof(f) && (bytesRead < chunkSize))
						{
							fread(&chunkID,   4, 1, f);
							fread(&chunkSize, 4, 1, f);

							switch (chunkID)
							{
								case 0x4D414E49: // "INAM"
								{
									inamPtr = ftell(f);
									inamLen = chunkSize;
								}
								break;

								default: break;
							}

							bytesRead += (chunkSize + (chunkSize & 1));
						}
					}
				}
			}
			break;

			case 0x61727478: // "xtra"
			{
				xtraPtr = ftell(f);
				xtraLen = chunkSize;
			}
			break;

			case 0x6C706D73: // "smpl"
			{
				smplPtr = ftell(f);
				smplLen = chunkSize;
			}
			break;

			default: break;
		}

		bytesRead += (chunkSize + (chunkSize & 1));
		fseek(f, endOfChunk, SEEK_SET);
	}

	// we need at least "fmt " and "data" - check if we found them sanely
	if (((fmtPtr == 0) || (fmtLen < 16)) || ((dataPtr == 0) || (dataLen == 0)))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto wavLoadError;
	}

	// ---- READ "fmt " CHUNK ----
	fseek(f, fmtPtr, SEEK_SET);
	fread(&audioFormat, 2, 1, f);
	fread(&numChannels, 2, 1, f);
	fread(&sampleRate,  4, 1, f);
	fseek(f, 6, SEEK_CUR); // unneeded
	fread(&bitsPerSample, 2, 1, f);
	sampleLength = dataLen;
	// ---------------------------

	// test if the WAV is compatible with our loader

	if ((sampleRate == 0) || (sampleLength == 0) || (sampleLength >= filesize))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported or is invalid!");
		goto wavLoadError;
	}

	if ((audioFormat != WAV_FORMAT_PCM) && (audioFormat != WAV_FORMAT_IEEE_FLOAT))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: The sample is not supported!");
		goto wavLoadError;
	}

	if ((numChannels == 0) || (numChannels > 2))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: Unsupported number of channels!");
		goto wavLoadError;
	}

	if ((audioFormat == WAV_FORMAT_IEEE_FLOAT) && (bitsPerSample != 32) && (bitsPerSample != 64))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: Unsupported bitdepth!");
		goto wavLoadError;
	}

	if ((bitsPerSample != 8) && (bitsPerSample != 16) && (bitsPerSample != 24) && (bitsPerSample != 32) && (bitsPerSample != 64))
	{
		okBoxThreadSafe(0, "System message", "Error loading sample: Unsupported bitdepth!");
		goto wavLoadError;
	}

	// ---- READ SAMPLE DATA ----
	fseek(f, dataPtr, SEEK_SET);

	s = &instr[editor.curInstr].samp[editor.curSmp];
	tmpSmp.pek = NULL;
	freeSample(&tmpSmp);

	if (bitsPerSample == 8) // 8-BIT INTEGER SAMPLE
	{
		audioDataU8 = (uint8_t *)(malloc(sampleLength * sizeof (int8_t)));
		if (audioDataU8 == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		fread(audioDataU8, sizeof (int8_t), sampleLength, f);

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;
			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						audioDataU8[i] = audioDataU8[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						audioDataU8[i] = audioDataU8[(i * 2) + 1];

					audioDataU8[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						audioDataU8[i] = (audioDataU8[(i * 2) + 0] + audioDataU8[(i * 2) + 1]) >> 1;

					audioDataU8[i] = 0;
				}
				break;
			}
		}

		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ &= ~16; // 8-bit
		for (i = 0; i < sampleLength; ++i)
			tmpSmp.pek[i] = audioDataU8[i] - 128;
	}
	else if (bitsPerSample == 16) // 16-BIT INTEGER SAMPLE
	{
		sampleLength /= sizeof (int16_t);

		audioDataS16 = (int16_t *)(malloc(sampleLength * sizeof (int16_t)));
		if (audioDataS16 == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		fread(audioDataS16, sizeof (int16_t), sampleLength, f);

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;

			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						audioDataS16[i] = audioDataS16[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						audioDataS16[i] = audioDataS16[(i * 2) + 1];

					audioDataS16[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
					{
						smp32 = audioDataS16[(i * 2) + 0] + audioDataS16[(i * 2) + 1];
						audioDataS16[i] = (int16_t)(smp32 >> 1);
					}

					audioDataS16[i] = 0;
				}
				break;
			}
		}

		sampleLength *= 2;
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ |= 16; // 16-bit
		memcpy(tmpSmp.pek, audioDataS16, sampleLength);
	}
	else if (bitsPerSample == 24) // 24-BIT INTEGER SAMPLE
	{
		sampleLength /= (sizeof (int32_t) - 1);

		audioDataS32 = (int32_t *)(malloc(sampleLength * sizeof (int32_t)));
		if (audioDataS32 == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		audioDataU8 = (uint8_t *)(audioDataS32);
		for (i = 0; i < sampleLength; i++)
		{
			audioDataU8[0] = 0;
			fread(&audioDataU8[1], 3, 1, f);
			audioDataU8 += sizeof (int32_t);
		}
		audioDataU8 = NULL; // IMPORTANT!

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;
			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 1];

					audioDataS32[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
					{
						smp64   = audioDataS32[(i * 2) + 0];
						smp64  += audioDataS32[(i * 2) + 1];
						smp64 >>= 1;

						audioDataS32[i] = (int32_t)(smp64);
					}

					audioDataS32[i] = 0;
				}
				break;
			}
		}

		normalize24bitSigned(audioDataS32, sampleLength);

		sampleLength *= 2;
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ |= 16; // 16-bit

		ptr16 = (int16_t *)(tmpSmp.pek);
		len32 = sampleLength / 2;

		for (i = 0; i < len32; ++i)
			ptr16[i] = (int16_t)(audioDataS32[i] >> 8);

		free(audioDataS32);
		audioDataS32 = NULL;
	}
	else if ((audioFormat == WAV_FORMAT_PCM) && (bitsPerSample == 32)) // 32-BIT INTEGER SAMPLE
	{
		sampleLength /= sizeof (int32_t);

		audioDataS32 = (int32_t *)(malloc(sampleLength * sizeof (int32_t)));
		if (audioDataS32 == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		fread(audioDataS32, sizeof (int32_t), sampleLength, f);

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;
			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						audioDataS32[i] = audioDataS32[(i * 2) + 1];

					audioDataS32[i] = 0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
					{
						smp64   = audioDataS32[(i * 2) + 0];
						smp64  += audioDataS32[(i * 2) + 1];
						smp64 >>= 1;

						audioDataS32[i] = (int32_t)(smp64);
					}

					audioDataS32[i] = 0;
				}
				break;
			}
		}

		normalize32bitSigned(audioDataS32, sampleLength);

		audioDataS16 = NULL;

		sampleLength *= 2;
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ |= 16; // 16-bit

		ptr16 = (int16_t *)(tmpSmp.pek);
		len32 = sampleLength / 2;

		for (i = 0; i < len32; ++i)
			ptr16[i] = (int16_t)(audioDataS32[i] >> 16);
	}
	else if ((audioFormat == WAV_FORMAT_IEEE_FLOAT) && (bitsPerSample == 32)) // 32-BIT FLOATING POINT SAMPLE
	{
		sampleLength /= sizeof (float);

		audioDataU32 = (uint32_t *)(malloc(sampleLength * sizeof (float)));
		if (audioDataU32 == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		fread(audioDataU32, sizeof (float), sampleLength, f);

		fAudioDataFloat = (float *)(audioDataU32);

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;
			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						fAudioDataFloat[i] = fAudioDataFloat[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						fAudioDataFloat[i] = fAudioDataFloat[(i * 2) + 1];

					fAudioDataFloat[i] = 0.0f;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						fAudioDataFloat[i] = (fAudioDataFloat[(i * 2) + 0] + fAudioDataFloat[(i * 2) + 1]) / 2.0f;

					fAudioDataFloat[i] = 0.0f;
				}
				break;
			}
		}

		normalize16bitFloatSigned(fAudioDataFloat, sampleLength);

		sampleLength *= 2;
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ |= 16; // 16-bit

		ptr16 = (int16_t *)(tmpSmp.pek);
		len32 = sampleLength / 2;

		if (cpu.hasSSE)
		{
			for (i = 0; i < len32; ++i)
			{
				fSmp = fAudioDataFloat[i];
				sse_float2int32_round(smp32, fSmp);
				CLAMP16(smp32);
				ptr16[i] = (int16_t)(smp32);
			}
		}
		else
		{
			for (i = 0; i < len32; ++i)
			{
				smp32 = (int32_t)(roundf(fAudioDataFloat[i]));
				CLAMP16(smp32);
				ptr16[i] = (int16_t)(smp32);
			}
		}
	}
	else if ((audioFormat == WAV_FORMAT_IEEE_FLOAT) && (bitsPerSample == 64)) // 64-BIT FLOATING POINT SAMPLE
	{
		sampleLength /= sizeof (double);

		dAudioDataDouble = (double *)(malloc(sampleLength * sizeof (double)));
		if (dAudioDataDouble == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		// read sample data
		fread(dAudioDataDouble, sizeof (double), sampleLength, f);

		// stereo conversion
		if (numChannels == 2)
		{
			sampleLength /= 2;
			switch (stereoSampleLoadMode)
			{
				case STEREO_SAMPLE_READ_LEFT:
				{
					// remove right channel data
					for (i = 1; i < sampleLength; i++)
						dAudioDataDouble[i] = dAudioDataDouble[(i * 2) + 0];
				}
				break;

				case STEREO_SAMPLE_READ_RIGHT:
				{
					// remove left channel data
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						dAudioDataDouble[i] = dAudioDataDouble[(i * 2) + 1];

					dAudioDataDouble[i] = 0.0;
				}
				break;

				default:
				case STEREO_SAMPLE_CONVERT:
				{
					// mix stereo to mono
					len32 = sampleLength - 1;
					for (i = 0; i < len32; i++)
						dAudioDataDouble[i] = (dAudioDataDouble[(i * 2) + 0] + dAudioDataDouble[(i * 2) + 1]) / 2.0;

					dAudioDataDouble[i] = 0.0;
				}
				break;
			}
		}

		normalize64bitDoubleSigned(dAudioDataDouble, sampleLength);

		sampleLength *= 2;
		if (sampleLength > MAX_SAMPLE_LEN)
			sampleLength = MAX_SAMPLE_LEN;

		tmpSmp.pek = (int8_t *)(malloc(sampleLength + 4));
		if (tmpSmp.pek == NULL)
		{
			okBoxThreadSafe(0, "System message", "Not enough memory!");
			goto wavLoadError;
		}

		tmpSmp.typ |= 16; // 16-bit

		ptr16 = (int16_t *)(tmpSmp.pek);
		len32 = sampleLength / 2;

		if (cpu.hasSSE2)
		{
			for (i = 0; i < len32; ++i)
			{
				dSmp = dAudioDataDouble[i];
				sse2_double2int32_round(smp32, dSmp);
				CLAMP16(smp32);
				ptr16[i] = (int16_t)(smp32);
			}
		}
		else
		{
			for (i = 0; i < len32; ++i)
			{
				smp32 = (int32_t)(round(dAudioDataDouble[i]));
				CLAMP16(smp32);
				ptr16[i] = (int16_t)(smp32);
			}
		}
	}

	tuneSample(&tmpSmp, sampleRate);

	tmpSmp.vol = 64;
	tmpSmp.pan = 128;
	tmpSmp.len = sampleLength;

	// ---- READ "smpl" chunk ----
	if ((smplPtr != 0) && (smplLen > 52))
	{
		fseek(f, smplPtr + 28, SEEK_SET); // seek to first wanted byte

		fread(&numLoops, 4, 1, f);
		if (numLoops == 1)
		{
			fseek(f, 4 + 4, SEEK_CUR); // skip "samplerData" and "identifier"

			fread(&loopType,  4, 1, f);
			fread(&loopStart, 4, 1, f);
			fread(&loopEnd,   4, 1, f);

			loopEnd++;

			if (tmpSmp.typ & 16)
			{
				loopStart *= 2;
				loopEnd   *= 2;
			}

			if (loopEnd <= sampleLength)
			{
				tmpSmp.repS = loopStart;
				tmpSmp.repL = loopEnd - loopStart;
				tmpSmp.typ |= ((loopType == 0) ? 1 : 2);
			}
		}
	}
	// ---------------------------

	// ---- READ "xtra" chunk ----
	if ((xtraPtr != 0) && (xtraLen >= 8))
	{
		fseek(f, xtraPtr, SEEK_SET);

		fread(&xtraFlags, 4, 1, f); // flags

		// panning (0..256)
		if (xtraFlags & 0x20) // set panning flag
		{
			fread(&tempPan, 2, 1, f);
			if (tempPan > 255)
				tempPan = 255;

			tmpSmp.pan = (uint8_t)(tempPan);
		}
		else
		{
			// don't read panning, skip it
			fseek(f, 2, SEEK_CUR);
		}

		// volume (0..256)
		fread(&tempVol, 2, 1, f);
		if (tempVol > 256)
			tempVol = 256;

		tmpSmp.vol = (uint8_t)(tempVol / 4); // 0..256 -> 0..64
	}
	// ---------------------------

	// ---- READ "INAM" chunk ----
	if ((inamPtr != 0) && (inamLen > 0))
	{
		fseek(f, inamPtr, SEEK_SET);

		memset(tmpSmp.name, 0, sizeof (tmpSmp.name));
		for (i = 0; i < 22; ++i)
		{
			if (i < inamLen)
			{
				chr = (char)(fgetc(f));
				if (chr == '\0')
					break;

				tmpSmp.name[i] = chr;
			}
		}
	}
	else
	{
		// set sample name from filename if we didn't load name from .wav
		tmpFilename = unicharToCp437(filename, true);
		if (tmpFilename != NULL)
		{
			j = (int32_t)(strlen(tmpFilename));
			while (j--)
			{
				if (tmpFilename[j] == DIR_DELIMITER)
					break;
			}

			tmpPtr = tmpFilename;
			if (j > 0)
				tmpPtr += (j + 1);

			sanitizeFilename(tmpPtr);

			filenameLen = (uint32_t)(strlen(tmpPtr));
			for (i = 0; i < 22; ++i)
			{
				if (i < filenameLen)
					tmpSmp.name[i] = tmpPtr[i];
				else
					tmpSmp.name[i] = '\0';
			}

			free(tmpFilename);
		}
	}

	fclose(f);
	if (audioDataU8  != NULL) free(audioDataU8);
	if (audioDataS16 != NULL) free(audioDataS16);
	if (audioDataS32 != NULL) free(audioDataS32);
	if (audioDataU32 != NULL) free(audioDataU32);
	if (dAudioDataDouble != NULL) free(dAudioDataDouble);

	// if loaded in instrument mode
	if (loadAsInstrFlag)
		clearInstr(editor.curInstr);

	s = &instr[editor.curInstr].samp[sampleSlot];

	lockMixerCallback();
	freeSample(s);
	memcpy(s, &tmpSmp, sizeof (sampleTyp));
	fixSample(s);
	unlockMixerCallback();

	setSongModifiedFlag();
	stereoSampleLoadMode = -1;

	// also sets mouse busy to false when done
	editor.updateCurSmp = true;

	(void)(ptr); // prevent compiler warning
	return (true);

wavLoadError:
	if (f != NULL) fclose(f);

	if (tmpSmp.pek   != NULL) free(tmpSmp.pek);
	if (audioDataU8  != NULL) free(audioDataU8);
	if (audioDataS16 != NULL) free(audioDataS16);
	if (audioDataS32 != NULL) free(audioDataS32);
	if (audioDataU32 != NULL) free(audioDataU32);
	if (dAudioDataDouble != NULL) free(dAudioDataDouble);

	stereoSampleLoadMode = -1;
	sampleIsLoading = false;

	return (false);
}

bool loadSample(UNICHAR *filenameU, uint8_t smpNr, bool instrFlag)
{
	char tmpBuffer[16 + 1];
	FILE *f;

	if (sampleIsLoading)
		return (false);

	stereoSampleLoadMode = 0;

	sampleSlot      = smpNr;
	loadAsInstrFlag = instrFlag;

	if (editor.curInstr == 0)
	{
		okBox(0, "System message", "The zero-instrument cannot hold intrument data.");
		return (false);
	}

	f = UNICHAR_FOPEN(filenameU, "rb");
	if (f == NULL)
	{
		okBox(0, "System message", "General I/O error during loading! Is the file in use?");
		return (false);
	}

	memset(tmpBuffer, 0, sizeof (tmpBuffer));
	if (fread(tmpBuffer, sizeof (tmpBuffer) - 1, 1, f) == 1)
	{
		tmpBuffer[sizeof (tmpBuffer) - 1] = '\0';

		// detect what sample format this is...

		// WAV
		if (!strncmp("RIFF", tmpBuffer, 4) && !strncmp("WAVE", tmpBuffer + 8, 4))
		{
			// let the user pick what to do with stereo samples...
			if (wavIsStereo(f))
				stereoSampleLoadMode = okBox(5, "System request", "This is a stereo sample...");

			sampleIsLoading = true;

			fclose(f);
			UNICHAR_STRCPY(editor.tmpFilenameU, filenameU);

			mouseAnimOn();
			thread = SDL_CreateThread(loadWAVSample, NULL, NULL);
			if (thread == NULL)
			{
				okBox(0, "System message", "Couldn't create thread!");
				sampleIsLoading = false;
				return (false);
			}

			SDL_DetachThread(thread);
			return (true);
		}

		// AIFF or IFF
		if (!strncmp("FORM", tmpBuffer, 4))
		{
			if (!strncmp("AIFC", tmpBuffer + 8, 4))
			{
				// AIFC (not supported)

				fclose(f);
				okBox(0, "System message", "Error loading sample: This AIFF type (AIFC) is not supported!");
				return (true);
			}
			else if (!strncmp("AIFF", tmpBuffer + 8, 4))
			{
				// AIFF

				// let the user pick what to do with stereo samples...
				if (aiffIsStereo(f))
					stereoSampleLoadMode = okBox(5, "System request", "This is a stereo sample...");

				sampleIsLoading = true;

				fclose(f);
				UNICHAR_STRCPY(editor.tmpFilenameU, filenameU);

				mouseAnimOn();
				thread = SDL_CreateThread(loadAIFFSample, NULL, NULL);
				if (thread == NULL)
				{
					okBox(0, "System message", "Couldn't create thread!");
					sampleIsLoading = false;
					return (false);
				}

				SDL_DetachThread(thread);
				return (true);
			}
			else if (!strncmp("8SVX", tmpBuffer + 8, 4) || !strncmp("16SV", tmpBuffer + 8, 4))
			{
				// IFF

				sampleIsLoading = true;

				fclose(f);
				UNICHAR_STRCPY(editor.tmpFilenameU, filenameU);

				mouseAnimOn();
				thread = SDL_CreateThread(loadIFFSample, NULL, NULL);
				if (thread == NULL)
				{
					okBox(0, "System message", "Couldn't create thread!");
					sampleIsLoading = false;
					return (false);
				}

				SDL_DetachThread(thread);
				return (true);
			}
		}
	}

	// load as RAW sample

	sampleIsLoading = true;

	fclose(f);
	UNICHAR_STRCPY(editor.tmpFilenameU, filenameU);

	mouseAnimOn();
	thread = SDL_CreateThread(loadRawSample, NULL, NULL);
	if (thread == NULL)
	{
		okBox(0, "System message", "Couldn't create thread!");
		sampleIsLoading = false;
		return (false);
	}

	SDL_DetachThread(thread);
	return (true);
}

static void normalize32bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
	int32_t smp32;
	uint32_t i, sample, sampleVolPeak;
	double dGain, dSmp;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; ++i)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	// prevent division by zero!
	if (sampleVolPeak < 1)
		sampleVolPeak = 1;

	dGain = (double)(INT_MAX) / sampleVolPeak;

	if (cpu.hasSSE2)
	{
		for (i = 0; i < sampleLength; ++i)
		{
			dSmp = sampleData[i] * dGain;
			sse2_double2int32_trunc(smp32, dSmp);
			sampleData[i] = smp32;
		}
	}
	else
	{
		for (i = 0; i < sampleLength; ++i)
			sampleData[i] = (int32_t)(sampleData[i] * dGain);
	}
}

static void normalize24bitSigned(int32_t *sampleData, uint32_t sampleLength)
{
	int32_t smp32;
	uint32_t i, sample, sampleVolPeak;
	double dGain, dSmp;

	sampleVolPeak = 0;
	for (i = 0; i < sampleLength; ++i)
	{
		sample = ABS(sampleData[i]);
		if (sampleVolPeak < sample)
			sampleVolPeak = sample;
	}

	// prevent division by zero!
	if (sampleVolPeak < 1)
		sampleVolPeak = 1;

	// 8388607 = (2^24 / 2) - 1
	dGain = 8388607.0 / sampleVolPeak;

	if (cpu.hasSSE2)
	{
		for (i = 0; i < sampleLength; ++i)
		{
			dSmp = sampleData[i] * dGain;
			sse2_double2int32_trunc(smp32, dSmp);
			sampleData[i] = smp32;
		}
	}
	else
	{
		for (i = 0; i < sampleLength; ++i)
			sampleData[i] = (int32_t)(sampleData[i] * dGain);
	}
}

static void normalize16bitFloatSigned(float *fSampleData, uint32_t sampleLength)
{
	uint32_t i;
	float fSample, fSampleVolPeak, fGain;

	fSampleVolPeak = 0.0f;
	for (i = 0; i < sampleLength; ++i)
	{
		fSample = fabsf(fSampleData[i]);
		if (fSampleVolPeak < fSample)
			fSampleVolPeak = fSample;
	}

	if (fSampleVolPeak > 0.0f)
	{
		fGain = (float)(INT16_MAX) / fSampleVolPeak;
		for (i = 0; i < sampleLength; ++i)
			fSampleData[i] *= fGain;
	}
}

static void normalize64bitDoubleSigned(double *dSampleData, uint32_t sampleLength)
{
	uint32_t i;
	double dSample, dSampleVolPeak, dGain;

	dSampleVolPeak = 0.0;
	for (i = 0; i < sampleLength; ++i)
	{
		dSample = fabs(dSampleData[i]);
		if (dSampleVolPeak < dSample)
			dSampleVolPeak = dSample;
	}

	if (dSampleVolPeak > 0.0)
	{
		dGain = (double)(INT16_MAX) / dSampleVolPeak;
		for (i = 0; i < sampleLength; ++i)
			dSampleData[i] *= dGain;
	}
}

bool fileIsInstrument(char *fullPath)
{
	char *filename;
	int32_t i, len, extOffset;

	// this assumes that fullPath is not empty

	len = (int32_t)(strlen(fullPath));

	// get filename from path
	i = len;
	while (i--)
	{
		if (fullPath[i] == DIR_DELIMITER)
			break;
	}

	filename = fullPath;
	if (i > 0)
		filename += (i + 1);
	// --------------------------

	len = (int32_t)(strlen(filename));
	if (len < 4)
		return (true); // can't be an instrument

	if (!_strnicmp("xi.", filename, 3) || ((len >= 4) && !_strnicmp("pat.", filename, 4)))
		return (true);

	extOffset = getExtOffset(filename, len);
	if (extOffset != -1)
	{
		if ((extOffset <= (len - 4)) && !_strnicmp(".pat", &filename[extOffset], 4)) return (true);
		if ((extOffset <= (len - 3)) && !_strnicmp(".xi",  &filename[extOffset], 3)) return (true);
	}

	return (false);
}

bool fileIsSample(char *fullPath)
{
	char *filename;
	int32_t i, len, extOffset;

	// this assumes that fullPath is not empty

	len = (int32_t)(strlen(fullPath));

	// get filename from path
	i = len;
	while (i--)
	{
		if (fullPath[i] == DIR_DELIMITER)
			break;
	}

	filename = fullPath;
	if (i > 0)
		filename += (i + 1);
	// --------------------------

	len = (int32_t)(strlen(filename));
	if (len < 4)
		return (true); // can't be a module

	if (!_strnicmp("xm.",  filename, 3) || !_strnicmp("ft.",  filename, 3) ||
		!_strnicmp("mod.", filename, 4) || !_strnicmp("nst.", filename, 4) ||
		!_strnicmp("s3m.", filename, 4) || !_strnicmp("stm.", filename, 4) ||
		!_strnicmp("fst.", filename, 4))
	{
		return (false); // definitely a module
	}

	extOffset = getExtOffset(filename, len);
	if (extOffset != -1)
	{
		if (extOffset <= (len - 4))
		{
			filename = &filename[extOffset];
			if (!_strnicmp(".mod", filename, 4) || !_strnicmp(".nst", filename, 4) ||
				!_strnicmp(".s3m", filename, 4) || !_strnicmp(".stm", filename, 4) ||
				!_strnicmp(".fst", filename, 4))
			{
				return (false); // definitely a module
			}
		}
		else if (extOffset <= (len - 3))
		{
			filename = &filename[extOffset];
			if (!_strnicmp(".xm", filename, 3) || !_strnicmp(".ft", filename, 3))
				return (false); // definitely a module
		}
	}

	return (true); // let's assume it's a sample
}
