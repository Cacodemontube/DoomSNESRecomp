#ifndef DOOM_PPU_STATE_H
#define DOOM_PPU_STATE_H

#include "snes/ppu.h"
#include "snes/saveload.h"

/* The generic PPU blob saves visible registers and tile memory, but not
 * the upload/read ports. Doom sets VMAIN during level initialization and
 * relies on it during later face and menu DMA uploads. A fresh-process
 * continuation must retain these ports rather than their reset defaults. */
static inline bool DoomPpuSaveLoadAccess(Ppu *ppu, SaveLoadInfo *sli) {
    uint32_t state[12]={ppu->vramPointer,ppu->vramIncrementOnHigh,
        ppu->vramRemapMode,ppu->vramIncrement,ppu->vramReadBuffer,
        ppu->cgramPointer,ppu->cgramSecondWrite,ppu->cgramBuffer,
        ppu->scrollPrev,ppu->scrollPrev2,ppu->mosaicStartLine,ppu->m7prev};
    sli->func(sli,state,sizeof(state));
    if(state[0]>65535 || state[1]>1 || state[2]>3 ||
       (state[3]!=1 && state[3]!=32 && state[3]!=128) || state[4]>65535 ||
       state[5]>255 || state[6]>1)return false;
    for(unsigned i=7;i<12;i++)if(state[i]>255)return false;
    ppu->vramPointer=state[0];ppu->vramIncrementOnHigh=state[1]!=0;
    ppu->vramRemapMode=state[2];ppu->vramIncrement=state[3];
    ppu->vramReadBuffer=state[4];ppu->cgramPointer=state[5];
    ppu->cgramSecondWrite=state[6]!=0;ppu->cgramBuffer=state[7];
    ppu->scrollPrev=state[8];ppu->scrollPrev2=state[9];
    ppu->mosaicStartLine=state[10];ppu->m7prev=state[11];
    return true;
}
#endif
