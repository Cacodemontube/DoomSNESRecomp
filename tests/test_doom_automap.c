#include "doom_automap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
    DoomMapPose a={0,0,65500,0.25},b={100,20,36,0.5};
    DoomMapPose mid=DoomMapInterpolate(a,b,0.5);
    CHECK(mid.x==50 && mid.y==10 && mid.zoom==0.375 && mid.angle==65536);
    double x,y;DoomMapProject((DoomMapPose){0,0,0,0.25},100,0,256,4,&x,&y);
    CHECK(x==512 && y==280);
    DoomMapProject((DoomMapPose){0,0,16384,0.25},100,0,256,4,&x,&y);
    CHECK(fabs(x-612)<1e-9 && fabs(y-380)<1e-9);
    double x1=-1e9,y1=100,x2=1e9,y2=100;
    CHECK(DoomMapClip(&x1,&y1,&x2,&y2,20,23,235,166));
    CHECK(fabs(x1-20)<1e-5 && fabs(x2-235)<1e-5);
    x1=-100;y1=0;x2=-10;y2=10;
    CHECK(!DoomMapClip(&x1,&y1,&x2,&y2,20,23,235,166));
    CHECK(DoomMapBlend(0xff808080,0xff000000,0.25)==0xff606060);
    static uint32_t palettes[144][256];static bool visible[144];
    for(unsigned ny=0;ny<144;ny++) {
        visible[ny]=true;
        palettes[ny][0x81]=0xff000000;palettes[ny][0xe1]=0xffff0000;
        palettes[ny][0x73]=0xff00ff00;palettes[ny][0x06]=0xff808080;
    }
    DoomMapFrame *frame=calloc(1,sizeof(*frame));CHECK(frame);
    frame->count=3;
    frame->lines[0]=(DoomMapLine){-256,0,256,0,0xe1};
    frame->lines[1]=(DoomMapLine){0,-256,0,256,0x73};
    frame->lines[2]=(DoomMapLine){-100000,120,100000,120,0x06};
    frame->text[140*216+5]=0x73;
    for(unsigned scale=1;scale<=4;scale++)for(unsigned wide=0;wide<2;wide++) {
        unsigned width=wide ? 342 : 256,pitch=width*scale*4+32,height=224*scale;
        uint8_t *allocation=malloc(pitch*height+64);CHECK(allocation);
        memset(allocation,0x5a,pitch*height+64);uint8_t *dst=allocation+32;
        for(unsigned row=0;row<height;row++)for(unsigned col=0;col<width*scale;col++)
            ((uint32_t*)(dst+row*pitch))[col]=0xff808080;
        DoomMapRaster(frame,(DoomMapPose){0,0,0,0.25},dst,pitch,width,scale,false,palettes,visible);
        CHECK(((uint32_t*)(dst+30*scale*pitch))[24*scale]==0xff000000);
        unsigned red=0,partial=0;
        for(unsigned row=23*scale;row<167*scale;row++)for(unsigned col=20*scale;col<(width-20)*scale;col++) {
            uint32_t c=((uint32_t*)(dst+row*pitch))[col];
            red+=((c>>16)&255)>0;
            partial+=((c>>16)&255)>0 && ((c>>16)&255)<255;
        }
        CHECK(red>20 && partial>0);
        unsigned left=(width-216)/2;
        CHECK(((uint32_t*)(dst+163*scale*pitch))[(left+5)*scale]==0xff00ff00);
        for(unsigned row=0;row<height;row++) {
            uint32_t *pixels=(uint32_t*)(dst+row*pitch);
            for(unsigned col=0;col<width*scale;col++)
                if(row<23*scale || row>=167*scale || col<20*scale || col>=(width-20)*scale)
                    CHECK(pixels[col]==0xff808080);
            for(unsigned pad=width*scale*4;pad<pitch;pad++)CHECK(dst[row*pitch+pad]==0x5a);
        }
        for(unsigned guard=0;guard<32;guard++)CHECK(allocation[guard]==0x5a && allocation[pitch*height+32+guard]==0x5a);
        for(unsigned row=0;row<height;row++)for(unsigned col=0;col<width*scale;col++)
            ((uint32_t*)(dst+row*pitch))[col]=0xff808080;
        DoomMapRaster(frame,(DoomMapPose){0,0,0,0.25},dst,pitch,width,scale,true,palettes,visible);
        CHECK(((uint32_t*)(dst+30*scale*pitch))[24*scale]==0xff606060);
        visible[7]=false;
        for(unsigned dy=0;dy<scale;dy++)for(unsigned col=20*scale;col<(width-20)*scale;col++)
            ((uint32_t*)(dst+(30*scale+dy)*pitch))[col]=0xffabcdef;
        DoomMapRaster(frame,(DoomMapPose){0,0,0,0.25},dst,pitch,width,scale,false,palettes,visible);
        for(unsigned dy=0;dy<scale;dy++)for(unsigned col=20*scale;col<(width-20)*scale;col++)
            CHECK(((uint32_t*)(dst+(30*scale+dy)*pitch))[col]==0xffabcdef);
        visible[7]=true;free(allocation);
    }
    free(frame);puts("Automap: continuous projection, clipping, high-resolution strokes, overlay and HUD guards passed");
    return 0;
}
