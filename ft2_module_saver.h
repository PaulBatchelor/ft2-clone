#ifndef __FT2_MODULE_SAVER_H
#define __FT2_MODULE_SAVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ft2_unicode.h"

void saveMusic(UNICHAR *filenameU);
bool saveXM(UNICHAR *filenameU);

#endif
