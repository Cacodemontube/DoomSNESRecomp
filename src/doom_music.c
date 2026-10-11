/* Run the retail music loader from the native main loop. It uploads the
 * selected module, stops the old track and starts the requested song at zero.
 * The normal beam/APU scheduler keeps running while the loader waits. */
#include "doom_music.h"
#include "common_cpu_infra.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/superfx.h"
#include <string.h>

enum { MainLoop=0x7e205c, MenuCall=0x7e20ea, MenuReturn=MenuCall+4, PlaySong=0x4efeb4, SongTable=0x1f3782 };
static const uint8_t main_signature[]={0xc2,0x20,0x24,0x32,0x30,0x0a,0x20,0xec,0x29};
static const uint8_t song_signature[]={0x8d,0xbf,0x06,0x22,0xcf,0xfe,0x4e,0xac,0xbf,0x06,0xa9,0x06,0x00};
static const uint8_t boundary_signature[]={0xa5,0x2f,0x05,0x2d,0xd0,0xfa,0x22,0x0f,0xa4,0x5f};
static unsigned requested;
static int allowed,playing,waiting,menu_active;
static Snes *machine;
static CpuState saved;
static uint8_t saved_interrupts;
int DoomMusicBusy(void) { return requested || waiting || playing; }
int DoomNativeMenuActive(void) { return menu_active; }

static void Main(CpuState *cpu,uint32_t pc) {
    if(!g_snes || g_snes!=machine)return;
    if(pc==MenuReturn)menu_active=0;
    if(pc==MenuCall && (g_snes->cart->superfx->sfr & 0x20)) {
        /* Raster phases can finish before the final GSU task. The native
         * menu and music loader both execute from the shared ROM bus. */
        interp_bridge_pre_opcode_redirect(MenuCall-6);
        return;
    }
    if(playing && pc==MenuReturn) {
        if(cpu->S!=saved.S)return;
        /* Restore registers, preserving the loader's elapsed hardware time
         * and bus state. The native RTL already balanced its return frame. */
        cpu->A=saved.A;cpu->X=saved.X;cpu->Y=saved.Y;
        cpu->D=saved.D;cpu->DB=saved.DB;cpu->PB=saved.PB;
        /* The menu's return ABI is 16-bit A, 8-bit X/Y; its caller's LDX
         * immediate is one byte, regardless of the entry register widths. */
        cpu->P=(saved.P & ~CPU_P_M) | CPU_P_X;cpu->emulation=saved.emulation;
        cpu->X&=255;cpu->Y&=255;
        cpu->host_return_valid=saved.host_return_valid;cpu_p_to_mirrors(cpu);
        /* Module upload disables raster IRQs and controller polling. Level
         * startup normally reenables them; a mid-level cheat must do so. */
        cpu_write8(cpu,0,0x4200,saved_interrupts);
        playing=0;
        return;
    }
    if(pc==MainLoop) {
        if(!allowed || !requested || waiting || playing || cpu->emulation)return;
        /* Use the game's pause boundary to drain the GSU before executing
         * cartridge code. Its normal continuation restarts the renderer. */
        if(!(g_snes->ram[0x2c]&0x40))return;
        g_snes->ram[0x2c]&=~0x40;
        waiting=1;
        return;
    }
    if(pc!=MenuCall)return;
    if(!waiting || !requested) {menu_active=1;return;}
    waiting=0;
    unsigned level=requested-1;requested=0;
    Cart *cart=g_snes->cart;
    saved=*cpu;playing=1;
    saved_interrupts=(g_snes->nmiEnabled?0x80:0) |
        (g_snes->vIrqEnabled?0x20:0) | (g_snes->hIrqEnabled?0x10:0) |
        (g_snes->autoJoyRead?1:0);
    g_snes->ram[0x2d]=g_snes->ram[0x2f]=8;
    g_snes->ram[0x2e]=g_snes->ram[0x30]=0;
    unsigned ret=(MenuReturn-1)&0xffff;
    cpu_write8(cpu,0,cpu->S--,(uint8_t)(pc>>16));
    cpu_write8(cpu,0,cpu->S--,(uint8_t)(ret>>8));
    cpu_write8(cpu,0,cpu->S--,(uint8_t)ret);
    cpu->A=cart->rom[SongTable+level*2] | (cart->rom[SongTable+level*2+1]<<8);
    cpu->D=0;cpu->DB=0;cpu->P&=~(CPU_P_M|CPU_P_X|CPU_P_D);
    cpu_p_to_mirrors(cpu);
    interp_bridge_pre_opcode_redirect(PlaySong);
}
void DoomMusicRequest(unsigned level) {
    if(allowed && level<27)requested=level+1;
}
void DoomMusicReset(void) {
    requested=0;playing=waiting=menu_active=0;allowed=0;machine=NULL;
}
void DoomMusicBeforeFrame(int enabled) {
    if(machine && machine!=g_snes)DoomMusicReset();
    allowed=0;
    Cart *cart=g_snes?g_snes->cart:NULL;
    if(!enabled && !waiting && !playing)requested=0;
    if(!cart || !cart->rom || cart->romSize!=0x200000 || !g_snes->ram ||
       memcmp(cart->rom+0xefeb4,song_signature,sizeof(song_signature)) ||
       memcmp(g_snes->ram+0x205c,main_signature,sizeof(main_signature)) ||
       memcmp(g_snes->ram+0x20e4,boundary_signature,sizeof(boundary_signature)))return;
    machine=g_snes;
    if(!interp_bridge_add_pre_opcode_hook(MenuCall,Main) ||
       !interp_bridge_add_pre_opcode_hook(MenuReturn,Main))return;
    if(enabled || waiting || playing)allowed=interp_bridge_add_pre_opcode_hook(MainLoop,Main) &&
        interp_bridge_add_pre_opcode_hook(MenuReturn,Main) && enabled;
}
