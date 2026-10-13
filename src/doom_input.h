#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
void DoomInputReset(void);
void DoomInputShutdown(void);
void DoomInputBeforeFrame(void);
uint16_t DoomInputKeyboard(const uint8_t *keys, unsigned player, uint16_t defaults);
uint16_t DoomInputAuxiliary(unsigned player,uint16_t defaults);
int DoomInputEvent(const void *event);
void DoomInputSuspended(int suspended);
int DoomInputLookEnabled(void);
double DoomInputPitch(void);
typedef struct DoomInputStats {
    unsigned movement_updates, turns;
    int enabled, gameplay, captured;
    double pitch;
    unsigned weapon_requests, weapon_selections;
    int jumping, suspended;
} DoomInputStats;
void DoomInputGetStats(DoomInputStats *out);
/* Emulation-thread entry shared by SDL motion and developer test requests. */
int DoomInputMotion(double x, double y);
int DoomInputRequestWeapon(unsigned slot);
int DoomInputRequestJump(void);
int DoomInputRequestCrouch(int held);
int DoomInputRequestMapZoom(int steps);
/* Host input state must rewind along with the guest during run-ahead. */
size_t DoomInputRollbackBound(void);
size_t DoomInputRollbackSave(void *data,size_t capacity);
int DoomInputRollbackLoad(const void *data,size_t size);
void DoomInputSpeculativeFrame(void);

int DoomInputMenuPointer(double *x,double *y);
int DoomInputMenuPosition(double x,double y,int click);
