#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned cases,rescans,enemy_mode;
static uint16_t projectile_vz,projectile_zfraction,projectile_ratio=256;
static void Rescan(SuperFx *fx,uint32_t pc,void *context) {
    (void)fx;(void)context;CHECK(pc==0xa47a);rescans++;
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static uint32_t Next(uint32_t coordinate,uint16_t velocity) {
    uint16_t applied=(uint16_t)((uint32_t)((int16_t)velocity*32767)>>15);
    return coordinate+(uint32_t)((int16_t)applied*256);
}
static void Case(SuperFx *fx,uint8_t *expected,uint32_t x,uint32_t y,uint16_t vx,uint16_t vy,int wall,int floor,int ceiling) {
    superfx_reset(fx);rescans=0;memset(expected,0,65536);
    Put(expected,0x11c,0x5a8e);Put(expected,0x11a,0xf800);
    Put(expected,0x124,32);
    Put(expected,0x7180,0xffff);Put(expected,0x7c,0x8000);expected[0x7e]=0x4f;
    Put(expected,0x80,0);Put(expected,0x82,0x1000);
    Put(expected,0x3082,0);Put(expected,0x3084,128);Put(expected,0x308c,0x5a8e);
    Put(expected,0x309a,0x5b00); /* Destination sector's existing head. */
    expected[0x5b16]=1;
    Put(expected,0x5a94,(uint16_t)x);Put(expected,0x5a96,(uint16_t)(x>>16));
    Put(expected,0x5a98,(uint16_t)y);Put(expected,0x5a9a,(uint16_t)(y>>16));
    Put(expected,0x5a9e,vx);Put(expected,0x5aa0,vy);
    if(enemy_mode==2) {
        Put(expected,8,projectile_ratio);Put(expected,0x5a9c,32);
        Put(expected,0x5aae,projectile_vz);Put(expected,0x5ab2,projectile_zfraction);
    }
    if(wall) {
        Put(expected,0x7180,0);Put(expected,0x7182,1000);Put(expected,0x7184,(uint16_t)-300);
        Put(expected,0x7186,0x7fff);Put(expected,0x7188,0);Put(expected,0x718a,(uint16_t)(wall!=2));
        Put(expected,0x718c,1000);Put(expected,0x718e,300);Put(expected,0x7196,0xffff);
        Put(expected,0x7190,(uint16_t)floor);Put(expected,0x7192,(uint16_t)ceiling);Put(expected,0x7194,(uint16_t)floor);
        if(wall>=3) {
            Put(expected,0x7184,700);Put(expected,0x718e,1300);
            Put(expected,0x7196,1);Put(expected,0x7198,1300);Put(expected,0x719a,1000);
            Put(expected,0x719c,0);Put(expected,0x719e,0x8001);Put(expected,0x71a0,1);
            Put(expected,0x71a2,700);Put(expected,0x71a4,1000);Put(expected,0x71ac,0xffff);
            if(wall==4) {
                uint8_t first[22];memcpy(first,expected+0x7180,22);
                memcpy(expected+0x7180,expected+0x7196,22);memcpy(expected+0x7196,first,22);
            }
        }
    }
    memcpy(fx->ram,expected,65536);
    uint32_t nx=Next(x,vx),ny=Next(y,vy);
    int blocked=(wall && wall!=2) || (wall==2 && (floor>=25 || ceiling<56 || ceiling-floor<56));
    int32_t vertical_product=(int16_t)projectile_vz*(int16_t)projectile_ratio;
    uint16_t collision_z=(uint16_t)(32+(uint16_t)((uint32_t)vertical_product>>15));
    uint16_t vertical_scaled=(uint16_t)((uint32_t)vertical_product>>7);
    if(enemy_mode==2 && wall==2)blocked=(int16_t)floor>=(int16_t)collision_z ||
        (int16_t)ceiling<(int16_t)(uint16_t)(collision_z+16) || (int16_t)(uint16_t)(ceiling-floor)<16;
    if(blocked && !enemy_mode) {
        /* rlmove4 negates the stored line PUX before caching the collision
         * vector. Quantization is sign-sensitive: use that oriented vector. */
        uint16_t first=(uint16_t)((uint32_t)((int16_t)vy*-32767)>>15);
        uint16_t magnitude=(uint16_t)(0-first);
        uint16_t projected=(uint16_t)((uint32_t)(32767*(int16_t)magnitude)>>15);
        nx=x;ny=Next(y,projected);Put(expected,0x5a9e,0);Put(expected,0x5aa0,projected);
        if(wall>=3) { ny=y;Put(expected,0x5aa0,0); }
    }
    if(blocked && enemy_mode) { nx=x;ny=y; }
    if(enemy_mode==2 && !blocked) {
        uint32_t nz=Next((32u<<16)|projectile_zfraction,vertical_scaled);
        Put(expected,0x5a9c,(uint16_t)(nz>>16));Put(expected,0x5ab2,(uint16_t)nz);
    }
    Put(expected,0x5a94,(uint16_t)nx);Put(expected,0x5a96,(uint16_t)(nx>>16));
    Put(expected,0x5a98,(uint16_t)ny);Put(expected,0x5a9a,(uint16_t)(ny>>16));
    if(!enemy_mode) { Put(expected,0x5ab0,0);Put(expected,0x5ab2,128); }
    if(wall==2 && !blocked && !enemy_mode) {
        Put(expected,0x5ab0,(uint16_t)(floor>0 ? floor : 0));
        Put(expected,0x5ab2,(uint16_t)(ceiling<128 ? ceiling : 128));
    }
    if(!enemy_mode || !blocked) {
        expected[0x5aa4]=1;Put(expected,0x308c,0);Put(expected,0x309a,0x5a8e);
        Put(expected,0x5a8e,0x5b00);Put(expected,0x5b02,0x5a8e);
    }
    unsigned start=enemy_mode==2 ? 0xa8c9 : enemy_mode ? 0xa6d0 : 0xa42f;
    superfx_cpu_write_io(fx,0x301e,(uint8_t)start);superfx_cpu_write_io(fx,0x301f,(uint8_t)(start>>8));
    superfx_sync(fx,fx->master_clock+100000);CHECK(!superfx_is_running(fx));
    for(unsigned a=0;a<65536;a++) {
        /* Movement's original work variables are temporary guest state.
         * Meaningful outputs below are checked separately; do not manufacture
         * scratch expectations by copying state from the implementation. */
        if(a>=0x11a && a<0x154)continue;
        if(fx->ram[a]!=expected[a]) {
            fprintf(stderr,"pipeline mode=%d x=%08x y=%08x v=%04x,%04x RAM[%04x] expected=%02x actual=%02x\n",
                wall,x,y,vx,vy,a,expected[a],fx->ram[a]);
            fprintf(stderr,"actual coordinates %04x:%04x,%04x:%04x rescans=%u percentage=%04x\n",
                fx->ram[0x5a96]|fx->ram[0x5a97]<<8,fx->ram[0x5a94]|fx->ram[0x5a95]<<8,
                fx->ram[0x5a9a]|fx->ram[0x5a9b]<<8,fx->ram[0x5a98]|fx->ram[0x5a99]<<8,rescans,
                fx->ram[0x13e]|fx->ram[0x13f]<<8);exit(1);
        }
    }
    CHECK(superfx_reg(fx,12)==0x5a8e);CHECK(superfx_reg(fx,0)==0);
    if(enemy_mode==2) {
        CHECK((fx->ram[0x144]|((uint16_t)fx->ram[0x145]<<8))==collision_z);
        CHECK((fx->ram[0x130]|((uint16_t)fx->ram[0x131]<<8))==vertical_scaled);
    }
    CHECK(rescans==(enemy_mode ? 0u : blocked ? 2u : 1u));
    int zero_percent=wall>=3 || (enemy_mode && blocked);
    CHECK(fx->ram[0x13e]==(zero_percent ? 0 : 0xff) && fx->ram[0x13f]==(zero_percent ? 0 : 0x7f));
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0x3d,0xac,0x8e,0xbc,0x3e,0x56,0x11,0x40};
    CHECK(!memcmp(rom+0x242f,entry,sizeof(entry)));rom[0x7800]=0;rom[0x7801]=1;
    /* A one-node BSP whose two leaves select destination sector 1. */
    memset(rom+0xf8000,0,28);Put(rom,0xf8002,0x7fff);
    Put(rom,0xf8010,0x8000);Put(rom,0xf801a,0x8000);rom[0xf9003]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa47a,Rescan,NULL));
    const uint16_t values[]={0,1,255,256,257,0x7fff,0x8000,0xffff};
    for(unsigned vx=0;vx<8;vx++)for(unsigned vy=0;vy<8;vy++)for(unsigned c=0;c<8;c++)
        Case(fx,expected,((uint32_t)values[c]<<16)|values[7-c],((uint32_t)values[7-c]<<16)|values[c],values[vx],values[vy],0,0,128);
    printf("Retail player response through BSP/relink/native return: %u no-contact source-oracle cases passed\n",cases);
    unsigned previous=cases;
    for(unsigned step=2;step<=8;step++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,(uint16_t)(-(int)step*256),(uint16_t)vy,1,0,128);
    printf("Retail solid-wall blocking/sliding with actual rescan: %u source-oracle cases passed\n",cases-previous);
    previous=cases;
    const int floors[]={-24,-1,0,1,23,24,25,26};
    const int ceilings[]={55,56,57,79,80,81,128};
    for(unsigned f=0;f<8;f++)for(unsigned c=0;c<7;c++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,0xfe00,(uint16_t)vy,2,floors[f],ceilings[c]);
    printf("Retail two-sided step/ceiling response: %u source-oracle cases passed\n",cases-previous);
    previous=cases;
    for(unsigned vx=2;vx<=8;vx++)for(unsigned vy=2;vy<=8;vy++)for(int order=3;order<=4;order++)
        Case(fx,expected,1017u<<16,1017u<<16,(uint16_t)(-(int)vx*256),(uint16_t)(-(int)vy*256),order,0,128);
    printf("Retail two-wall corner response in both candidate orders: %u source-oracle cases passed\n",cases-previous);
    previous=cases;enemy_mode=1;
    CHECK(!memcmp(rom+0x26d0,entry,sizeof(entry)));
    for(unsigned vx=0;vx<8;vx++)for(unsigned vy=0;vy<8;vy++)for(unsigned c=0;c<8;c++)
        Case(fx,expected,((uint32_t)values[c]<<16)|values[7-c],((uint32_t)values[7-c]<<16)|values[c],values[vx],values[vy],0,0,128);
    for(unsigned f=0;f<8;f++)for(unsigned c=0;c<7;c++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,0xfe00,(uint16_t)vy,2,floors[f],ceilings[c]);
    for(unsigned step=2;step<=8;step++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,(uint16_t)(-(int)step*256),(uint16_t)vy,1,0,128);
    printf("Retail enemy complete free/blocked/passage response: %u source-oracle cases passed\n",cases-previous);
    previous=cases;enemy_mode=2;CHECK(!memcmp(rom+0x28c9,entry,sizeof(entry)));
    for(unsigned vx=0;vx<8;vx++)for(unsigned vy=0;vy<8;vy++)for(unsigned c=0;c<8;c++)
        Case(fx,expected,((uint32_t)values[c]<<16)|values[7-c],((uint32_t)values[7-c]<<16)|values[c],values[vx],values[vy],0,0,128);
    const int projectile_floors[]={0,15,16,31,32,33};
    const int projectile_ceilings[]={31,32,47,48,49,64};
    for(unsigned f=0;f<6;f++)for(unsigned c=0;c<6;c++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,0xfe00,(uint16_t)vy,2,projectile_floors[f],projectile_ceilings[c]);
    for(unsigned step=2;step<=8;step++)for(int vy=-512;vy<=512;vy+=256)
        Case(fx,expected,1017u<<16,0,(uint16_t)(-(int)step*256),(uint16_t)vy,1,0,128);
    printf("Retail projectile complete free/blocked/passage response: %u source-oracle cases passed\n",cases-previous);
    previous=cases;
    for(unsigned vz=0;vz<65536;vz++)for(unsigned fraction=0;fraction<2;fraction++) {
        projectile_vz=(uint16_t)vz;projectile_zfraction=fraction ? 0xffff : 0;
        Case(fx,expected,1000u<<16,1000u<<16,256,0,0,0,128);
    }
    printf("Retail projectile vertical integration: %u source-oracle cases passed\n",cases-previous);
    previous=cases;
    const uint16_t vz_values[]={0,1,127,128,129,256,0xff00,0xff80,0xff81,0xffff,0x7fff,0x8000};
    const uint16_t ratios[]={0,1,128,255,256,257,512,1024,0x7fff,0x8000,0xffff};
    const uint16_t fractions[]={0,0xff00,0xffff};
    for(unsigned v=0;v<12;v++)for(unsigned r=0;r<11;r++)for(unsigned zf=0;zf<3;zf++) {
        projectile_vz=vz_values[v];projectile_ratio=ratios[r];projectile_zfraction=fractions[zf];
        for(unsigned f=0;f<6;f++)for(unsigned c=0;c<6;c++)
            Case(fx,expected,1017u<<16,0,0xfe00,0,2,projectile_floors[f],projectile_ceilings[c]);
    }
    printf("Retail moving-Z projectile passage contacts: %u source-oracle cases passed\n",cases-previous);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
