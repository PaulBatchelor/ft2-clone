#ifndef __FT2_MODULE_LOADER_H
#define __FT2_MODULE_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "unicode.h"

void loadMusic(UNICHAR *filenameU);
//bool loadMusicUnthreaded(UNICHAR *filenameU); // for development testing
bool handleModuleLoadFromArg(int argc, char **argv);
void loadDroppedFile(char *fullPathUTF8, bool songModifiedCheck);
void handleLoadMusicEvents(void);
void clearUnusedChannels(tonTyp *p, int16_t pattLen, uint8_t antChn);
void unpackPatt(uint8_t *dst, uint16_t inn, uint16_t len, uint8_t antChn);

#endif
