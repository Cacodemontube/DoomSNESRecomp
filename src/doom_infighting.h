#pragma once
#include <stdbool.h>
typedef struct SaveLoadInfo SaveLoadInfo;
void DoomInfightingBeforeFrame(void);
void DoomInfightingShutdown(void);
void DoomInfightingReset(void);
void DoomInfightingSave(SaveLoadInfo *sli);
bool DoomInfightingLoad(SaveLoadInfo *sli);
#if SNESRECOMP_TRACE
typedef struct DoomInfightingStats {
    unsigned ready, births, hits, interceptions, retaliations, blocked, splash;
} DoomInfightingStats;
void DoomInfightingGetStats(DoomInfightingStats *stats);
#endif
