#pragma once
#include <stdint.h>
void DoomInputReset(void);
void DoomInputShutdown(void);
void DoomInputBeforeFrame(void);
uint16_t DoomInputKeyboard(const uint8_t *keys, unsigned player, uint16_t defaults);
int DoomInputEvent(const void *event);
void DoomInputSuspended(int suspended);
int DoomInputLookEnabled(void);
double DoomInputPitch(void);
typedef struct DoomInputStats {
    unsigned movement_updates, turns;
    int enabled, gameplay, captured;
    double pitch;
} DoomInputStats;
void DoomInputGetStats(DoomInputStats *out);
/* Emulation-thread entry shared by SDL motion and developer test requests. */
int DoomInputMotion(double x, double y);
