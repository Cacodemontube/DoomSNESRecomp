#pragma once
void DoomCheatsBeforeFrame(void);
void DoomCheatsReset(void);
void DoomCheatsShutdown(void);
void DoomCheatsSuspended(int suspended);
void DoomCheatsEvent(const void *event);
#if SNESRECOMP_TRACE
typedef struct DoomCheatsStats {
    int enabled,ready,god,clip,map,health,armor,keys,arms,face,skill,level,x,y,ammo[4];
    unsigned monster_arrows;
} DoomCheatsStats;
void DoomCheatsTestText(const char *text);
void DoomCheatsGetStats(DoomCheatsStats *stats);
void DoomCheatsTestExit(void);
#endif
