#include "doom_input_math.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
int main(void) {
    CHECK(DoomKeyboardPad(DOOM_KEY_FORWARD)==0x10);
    CHECK(DoomKeyboardPad(DOOM_KEY_BACK)==0x20);
    CHECK(DoomKeyboardPad(DOOM_KEY_LEFT)==0x400);
    CHECK(DoomKeyboardPad(DOOM_KEY_RIGHT)==0x800);
    CHECK(DoomKeyboardPad(DOOM_KEY_RUN)==1);
    CHECK(DoomKeyboardPad(DOOM_KEY_USE)==0x100);
    CHECK(DoomKeyboardPad(DOOM_KEY_PAUSE)==8);
    CHECK(DoomKeyboardPad(DOOM_KEY_FIRE)==2);
    CHECK(DoomKeyboardPad(DOOM_KEY_FORWARD|DOOM_KEY_LEFT|DOOM_KEY_RUN|DOOM_KEY_FIRE)==0x413);
    CHECK(DoomKeyboardPad(15)==0);
    CHECK(DoomMouseAngle(0,1)==0xffc0);
    CHECK(DoomMouseAngle(0,-1)==64);
    CHECK(DoomMouseAngle(0,10000)==0xc000);
    uint16_t angle=0;
    for(unsigned i=0;i<10;i++) angle=DoomMouseAngle(angle,3);
    CHECK(angle==DoomMouseAngle(0,30));
    CHECK(DoomMousePitch(0,-40,0)==10);
    CHECK(DoomMousePitch(0,-40,1)==-10);
    CHECK(DoomMousePitch(0,-10000,0)==42);
    CHECK(DoomMousePitch(-40,10000,0)==-42);
    puts("Modern controls: pad actions, native mouse scale, accumulation, pitch and limits passed");
    return 0;
}
