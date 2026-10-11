/* Exercise the real PPU ports without the desktop/debug/plugin runtime. */
#undef SNESRECOMP_TRACE
#define SNESRECOMP_TRACE 0
#undef SNESRECOMP_ENABLE_MODS
#define SNESRECOMP_ENABLE_MODS 0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doom_ppu_state.h"
#include "snes/snes.h"
Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint16_t WsShadowTile(int l,int x,uint32_t y,uint16_t h,uint16_t a,uint16_t t){(void)l;(void)x;(void)y;(void)h;(void)a;return t;}
bool WsShadowLayerActive(int l){(void)l;return false;}
uint32_t WsShadowWorldX(int l){(void)l;return 0;}
uint32_t WsShadowPresentWorldY(int l,int x){(void)l;(void)x;return 0;}
uint32_t WsShadowScrollY(int l){(void)l;return 0;}
void WsShadowOnVramWrite(uint16_t a,uint16_t v){(void)a;(void)v;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct Stream { SaveLoadInfo sli;uint8_t bytes[48];bool load; } Stream;
static void transfer(SaveLoadInfo *sli,void *data,size_t size) {
    Stream *s=(Stream *)sli;CHECK(size==sizeof(s->bytes));
    if(s->load)memcpy(data,s->bytes,size);else memcpy(s->bytes,data,size);
}
#include "../snesrecomp/runner/src/snes/ppu.c"
static void upload(Ppu *p) {
    /* Doom relies on the inherited VMAIN mode, and only sets the address
     * before uploading fresh animated graphics. */
    ppu_write(p,0x16,0x00);ppu_write(p,0x17,0x20);
    ppu_write(p,0x18,0x12);ppu_write(p,0x19,0x34);
    ppu_write(p,0x18,0x56);ppu_write(p,0x19,0x78);
}
int main(void) {
    Ppu *original=ppu_init(),*resumed=ppu_init();CHECK(original && resumed);
    ppu_reset(original);ppu_reset(resumed);
    ppu_write(original,0x15,0x80);
    Stream s={.sli={.func=transfer}};
    CHECK(DoomPpuSaveLoadAccess(original,&s.sli));
    upload(original);upload(resumed);
    CHECK(original->vram[0x2000]==0x3412);
    CHECK(resumed->vram[0x2000]!=original->vram[0x2000]); /* Old fresh-process failure. */
    ppu_reset(resumed);s.load=true;
    CHECK(DoomPpuSaveLoadAccess(resumed,&s.sli));upload(resumed);
    CHECK(resumed->vram[0x2000]==0x3412 && resumed->vram[0x2001]==0x7856);
    CHECK(resumed->vramPointer==original->vramPointer);
    /* Recover an in-progress palette word too, rather than losing its low byte. */
    ppu_write(original,0x21,5);ppu_write(original,0x22,0x2a);
    s.load=false;CHECK(DoomPpuSaveLoadAccess(original,&s.sli));
    s.load=true;CHECK(DoomPpuSaveLoadAccess(resumed,&s.sli));
    ppu_write(original,0x22,0x35);ppu_write(resumed,0x22,0x35);
    CHECK(resumed->cgram[5]==original->cgram[5] && resumed->cgram[5]==0x352a);
    ppu_free(original);ppu_free(resumed);
    puts("PPU continuation preserves animated tile uploads and palette write latches");return 0;
}
