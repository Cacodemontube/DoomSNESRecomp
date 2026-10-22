#ifndef DOOM_CROSSHAIR_H
#define DOOM_CROSSHAIR_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct DoomAimTarget { unsigned object; int x,depth,bottom,height; } DoomAimTarget;
static inline unsigned DoomAimWord(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
/* Read-only preview of DOOM-FX rlweapons3.a: _RLFireTarget,
 * _RLFireGun and _RLFireProjectile. Keep the nearest target (including
 * barrels that can intercept hitscan), the original strict range/tie rules,
 * active/target flags and actual native drawn flag. No AI or RNG advances. */
static inline DoomAimTarget DoomAimSelect(const uint8_t *ram,size_t size) {
    DoomAimTarget target={0};
    if(!ram || size<65536)return target;
    unsigned weapon=DoomAimWord(ram+0x1dc),flags=weapon>=10?0xe0:0xa0;
    int best=(weapon==0 || weapon==6)?64:32767;
    unsigned address=DoomAimWord(ram+0xda);
    for(unsigned visits=0;address && visits<28;visits++) {
        if(address+26>size)break;
        const uint8_t *p=ram+address;
        unsigned object=DoomAimWord(p+4);
        if(object && object+6<=size && (ram[object+4]&flags)==flags && p[20] &&
           (int16_t)DoomAimWord(p+10)<124 && (int16_t)DoomAimWord(p+12)>=46) {
            int x=(int16_t)DoomAimWord(p+6),depth=(int16_t)DoomAimWord(p);
            int distance=abs(x)>abs(depth)?abs(x):abs(depth);
            if(depth>0 && distance<best) {
                best=distance;
                target=(DoomAimTarget){object,x,depth,(int16_t)DoomAimWord(p+8),p[14]};
            }
        }
        address=DoomAimWord(p+2);
    }
    if(target.object && (ram[target.object+5]<6 || ram[target.object+5]>14 ||
       ((weapon==0 || weapon==6) && abs(target.bottom-(int16_t)DoomAimWord(ram+0x30))>=42)))
        target=(DoomAimTarget){0};
    return target;
}
void DoomCrosshairDraw(uint8_t *dst,size_t pitch,unsigned width,unsigned height);
#endif
