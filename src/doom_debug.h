#pragma once
#include "debug_server.h"
#if SNESRECOMP_TRACE
void DoomDebugInit(void);
void DoomDebugRecordFrame(unsigned frame);
void DoomDebugRecordDraw(double milliseconds);
#else
static inline void DoomDebugInit(void) {}
static inline void DoomDebugRecordFrame(unsigned frame) { (void)frame; }
static inline void DoomDebugRecordDraw(double milliseconds) { (void)milliseconds; }
#endif
