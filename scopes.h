#ifndef __FT2_SCOPES_H
#define __FT2_SCOPES_H

#include <stdint.h>
#include <stdbool.h>
#include "ft2_header.h"

int32_t getSampleReadPos(uint8_t ch);
void stopAllScopes(void);
void unmuteAllChansOnMusicLoad(void);
bool testScopesMouseDown(void);
void drawScopes(void);
void drawScopeFramework(void);
bool initScopes(void);

#endif
