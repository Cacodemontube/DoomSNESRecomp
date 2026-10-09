#include "doom_resolution.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static void word(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
int main(void) {
    uint8_t pixels[256], runs[] = {3,10,11,12,0xfd,22};
    CHECK(DoomResolutionDecodeColumn(runs,sizeof(runs),0,6,pixels));
    CHECK(!memcmp(pixels,(uint8_t[]){10,11,12,22,22,22},6));
    CHECK(!DoomResolutionDecodeColumn(runs,4,0,6,pixels));
    CHECK(!DoomResolutionDecodeColumn((uint8_t[]){0},1,0,1,pixels));
    CHECK(!DoomResolutionDecodeColumn(runs,sizeof(runs),0,257,pixels));
    uint8_t sprite_runs[] = {5,6,0,2,7,0,0xfe,8,0,0};
    CHECK(DoomResolutionDecodeSprite(sprite_runs,sizeof(sprite_runs),0,10,pixels));
    CHECK(!memcmp(pixels,(uint8_t[]){5,6,0,0,7,8,8,0,0,0},10));
    CHECK(!DoomResolutionDecodeSprite(sprite_runs,7,0,10,pixels));
    uint8_t *rom=calloc(1,0x200000), *ram=calloc(1,0x10000);
    DoomResolutionScene *scene=calloc(1,sizeof(*scene));
    CHECK(rom && ram && scene);
    rom[0x1b0686]=128;rom[0x1b0687]=127;
    word(ram+0xd6,0x7180+62);
    word(ram+0x7180+52,0x4000);word(ram+0x7180+54,0x4004);
    word(ram+0x7180+28,0x3080);
    word(ram+0x4000,128);word(ram+0x4002,(unsigned)-96);
    word(ram+0x4004,192);word(ram+0x4006,96);
    CHECK(DoomResolutionCapture(scene,ram,0x10000,0,rom,0x200000));
    CHECK(scene->count==1 && scene->segments[0].x1==-96);
    word(ram+0xd6,0x7181);
    CHECK(!DoomResolutionCapture(scene,ram,0x10000,0,rom,0x200000));
    scene->count=1;
    scene->segments[0]=(DoomResolutionSegment){
        .x1=-96,.z1=128,.x2=96,.z2=192,.flags=1,.near_sector=0,
        .far_sector=UINT16_MAX,.texture={0x100,0},
        .texture_h={128,0},.texture_w={15,0},
        .angle=0,.perpendicular=150,.texture_offset=48 };
    word(scene->sectors+2,(unsigned)-64);word(scene->sectors+4,64);
    scene->sectors[8]=100;scene->sectors[9]=101;
    for(unsigned row=0;row<33;row++)
        for(unsigned c=0;c<256;c++)rom[0x1cde00+row*256+c]=c;
    for(unsigned u=0;u<16;u++) {
        unsigned table=0x1b0102+u*3,address=0x4000+u*0x100;
        word(rom+table,address);rom[table+2]=0x40;
        rom[address++]=64;
        for(unsigned y=0;y<64;y++)rom[address++]=1+(u*7+y)%96;
        rom[address++]=64;
        for(unsigned y=64;y<128;y++)rom[address++]=1+(u*7+y)%96;
    }
    uint32_t palette[144][256];bool visible[144];
    for(unsigned y=0;y<144;y++) {
        visible[y]=true;
        for(unsigned c=0;c<256;c++)palette[y][c]=0xff000000|c;
    }
    DoomResolutionHit hits[168];
    CHECK(DoomResolutionHits(scene,0,1,hits)==1);
    CHECK(fabs(hits[0].depth-160)<1e-9);
    unsigned changed=0;
    for(int look=-42;look<=42;look+=42) {
    scene->horizon_offset=look;
    for(unsigned scale=1;scale<=4;scale++) {
        unsigned width=256*scale,height=224*scale,stride=width+7;
        uint32_t *out=malloc(stride*height*4);CHECK(out);
        for(unsigned i=0;i<stride*height;i++)out[i]=0xa5a5a5a5;
        for(unsigned x=20*scale;x<236*scale;x++) {
            double rx=(x+0.5)/scale-128;
            DoomResolutionRasterColumn(scene,rom,0x200000,0,rx,
                rx/DOOM_FOCAL,1,x,scale,(uint8_t*)out,stride*4,palette,visible,NULL);
        }
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<stride;x++)
            if(y<23*scale || y>=167*scale || x<20*scale || x>=236*scale)
                CHECK(out[y*stride+x]==0xa5a5a5a5);
        for(unsigned y=24*scale;y<166*scale;y++)for(unsigned x=21*scale;x<235*scale;x++)
            if(out[y*stride+x]!=out[(y/scale*scale)*stride+(x/scale*scale)])changed++;
        free(out);
    }
    }
    scene->horizon_offset=0;
    CHECK(changed>500);
    /* Native texture offsets crossing a repeat must not interpolate halfway
     * through the texture. Both endpoints describe the same wrapped phase. */
    DoomResolutionSegment *seg=&scene->segments[0];
    seg->uv[0][42]=(DoomResolutionUv){.u=10,.v=2,.step=1,.y=71,.valid=true};
    seg->uv[0][43]=(DoomResolutionUv){.u=11,.v=130,.step=1,.y=71,.valid=true};
    uint32_t *out=calloc(512*448,4);CHECK(out);
    DoomResolutionWall(scene,rom,0x200000,seg,0,(85.0-108)/DOOM_FOCAL,
        128,64,10,142,144,2,(uint8_t*)out,512*4,palette,visible,NULL);
    CHECK(out[(142+46)*512+10]==(0xff000000u|73u));
    seg->uv[0][42].v=130;seg->uv[0][43].v=2;
    DoomResolutionWall(scene,rom,0x200000,seg,0,(85.0-108)/DOOM_FOCAL,
        128,64,10,142,144,2,(uint8_t*)out,512*4,palette,visible,NULL);
    CHECK(out[(142+46)*512+10]==(0xff000000u|73u));
    /* Original object-image columns retain transparency and source detail;
     * a nearer wall's depth prevents the object from showing through it. */
    scene->sprite_count=1;
    scene->sprites[0]=(DoomResolutionSprite){.x=0,.depth=1000,.bottom=-10,
        .screen_left=105,.width=64,.height=64,.image=0x1000,.map=0xde};
    for(unsigned u=0;u<64;u++) {
        unsigned table=0x1a1002+u*3,address=0x10000+u*128;
        word(rom+table,address&0xffff);rom[table+2]=0x41;
        for(unsigned y=0;y<64;y++)rom[address+y]=1+(u*7+y)%96;
    }
    double depths[288];for(unsigned y=0;y<288;y++)depths[y]=INFINITY;
    for(unsigned i=0;i<512*448;i++)out[i]=0xff101010;
    DoomResolutionSprites(scene,rom,0x200000,0,1,256,2,(uint8_t*)out,
        512*4,palette,visible,depths);
    CHECK(out[(140+46)*512+256]!=0xff101010);
    /* Native RLTraceO projects height at 128/depth, independently of the
     * horizontal SNES aspect correction. The former square projection
     * omitted this upper part of the sprite. */
    CHECK(out[(131+46)*512+256]!=0xff101010);
    for(unsigned y=0;y<288;y++)depths[y]=10;
    for(unsigned i=0;i<512*448;i++)out[i]=0xff101010;
    DoomResolutionSprites(scene,rom,0x200000,0,1,256,2,(uint8_t*)out,
        512*4,palette,visible,depths);
    for(unsigned y=0;y<448;y++)CHECK(out[y*512+256]==0xff101010);
    /* Preserve native proportions and the supplied left edge at every
     * supported resolution, including the scale-1 mouse-look redraw. */
    uint32_t *scaled=calloc(1024*896,4);CHECK(scaled);
    double scaled_depths[576];
    for(unsigned scale=1;scale<=4;scale++) {
        for(unsigned j=0;j<1024*896;j++)scaled[j]=0xff101010;
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        DoomResolutionSprites(scene,rom,0x200000,0,1,128*scale,scale,
            (uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        unsigned painted=0;
        for(unsigned y=0;y<144*scale;y++)
            painted+=scaled[(y+23*scale)*1024+128*scale]!=0xff101010;
        CHECK(painted>=8*scale && painted<=9*scale);
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        DoomResolutionSprites(scene,rom,0x200000,(104-108)/DOOM_FOCAL,1,
            100*scale,scale,(uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        for(unsigned y=0;y<144*scale;y++)
            CHECK(scaled[(y+23*scale)*1024+100*scale]==0xff101010);
    }
    /* Floor depth must not hide the lowest sprite pixels. Exercise native
     * and enhanced sizes, close/far objects, yawed views and mouse look. */
    word(scene->sectors+2,0);scene->sectors[8]=200;scene->view_z=32;
    const double distances[]={128,256,1000};
    for(unsigned scale=1;scale<=4;scale++)
    for(unsigned d=0;d<3;d++)
    for(unsigned side=0;side<2;side++)
    for(int look=-20;look<=20;look+=20) {
        scene->horizon_offset=look;
        DoomResolutionSprite *sprite=&scene->sprites[0];
        sprite->depth=distances[d];sprite->bottom=-32;
        sprite->screen_left=108-(int)(32*DOOM_FOCAL/sprite->depth);
        double ratio=side ? 1.3 : 1.0,depth=sprite->depth/ratio;
        int foot=DoomResolutionClipY(-32,depth,scale,0,144*scale,look);
        for(unsigned y=0;y<144*scale;y++)scaled_depths[y]=INFINITY;
        DoomResolutionPlane(scene,rom,0,false,0,0,128*scale,
            0,144*scale,scale,(uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        uint32_t before=scaled[(foot-1+23*scale)*1024+128*scale];
        CHECK(scaled_depths[foot-1]>=depth);
        DoomResolutionSprites(scene,rom,0x200000,0,ratio,128*scale,scale,
            (uint8_t*)scaled,1024*4,palette,visible,scaled_depths);
        CHECK(scaled[(foot-1+23*scale)*1024+128*scale]!=before);
        CHECK(scaled_depths[foot-1]==depth);
    }
    free(scaled);
    free(out); /* Newly rasterized subpixels, not duplicated native pixels. */
    free(scene);free(ram);free(rom);
    puts("Resolution: bounded RLE, geometry validation, ray depth, subpixel detail and clipping passed");
    return 0;
}
