#ifndef __FT2_EVENTS_H
#define __FT2_EVENTS_H

#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#endif
#include <stdint.h>

enum
{
	EVENT_NONE                = 0,
	EVENT_LOADMUSIC_ARGV      = 1,
	EVENT_LOADMUSIC_DRAGNDROP = 2,
	EVENT_LOADMUSIC_DISKOP    = 3,
};

void handleThreadEvents(void);
void readInput(void);
void handleEvents(void);
void setupCrashHandler(void);
#ifdef _WIN32
bool handleSingleInstancing(int32_t argc, char **argv);
void closeSingleInstancing(void);
void usleep(uint32_t usec);
void setupWin32Usleep(void);
void freeWin32Usleep(void);
#endif

#endif
