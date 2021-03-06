#ifndef __FT2_SAMPLE_LOADER_H
#define __FT2_SAMPLE_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "unicode.h"

bool loadSample(UNICHAR *filenameU, uint8_t sampleSlot, bool loadAsInstrFlag);
bool fileIsInstrument(char *fullPath);
bool fileIsSample(char *fullPath);
void removeSampleIsLoadingFlag(void);

#endif
