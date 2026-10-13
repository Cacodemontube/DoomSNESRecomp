#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void Word(uint8_t *p,unsigned a,unsigned v) { p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8); }
int main(int argc,char **argv) {
    CHECK(argc==2);
    uint8_t *rom=malloc(0x200000),*ram=malloc(65536),*expected=malloc(65536);CHECK(rom&&ram&&expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t simple[]={0xf0,0xd0,0x21,0x68,0xef,0x09,0x0a,0xde,0xbc,0x38,0xd8,0xd8,0x3d,0xef,0x38,0xd8,0xd8,0x9b,0xde};
    const uint8_t distance[]={0xf0,0xd0,0x21,0x68,0x2e,0x12,0x09,0x17,0xbc,0x38,0xd8,0xd8,0xa0,0x24,0x5c,0x40,0xa1,0x40,0x61,0x07,0x03,0x01,0xde,0xde,0xef,0xde,0x3d,0xef,0x38,0xd8,0xd8,0xb2,0x1e,0x3e,0x54,0x9b,0x01};
    CHECK(!memcmp(rom+0x1092,simple,sizeof(simple)));CHECK(!memcmp(rom+0x10a5,distance,sizeof(distance)));
    rom[0x7800]=0;rom[0x7801]=1;
    Word(rom,0x1f9000,0x1234);Word(rom,0x1f9002,0xabcd);
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);unsigned cases=0;
    for(unsigned mode=0;mode<2;mode++)for(unsigned d=0;d<65536;d++)for(unsigned slot=0;slot<=20;slot++) {
        superfx_reset(fx);memset(expected,0xa5,65536);Word(expected,0x5b24,d);memcpy(ram,expected,65536);
        unsigned full=slot==20,cursor=0x2180+slot*4;
        /* Distinct bijections cover every low/high pointer byte in both
           operand positions, without executing the queued routine. */
        unsigned near_code=(d*40503u+17u)&65535u,far_code=near_code^0xffffu;
        Word(rom,0x1f9000,near_code);Word(rom,0x1f9002,far_code);
        if(!full) { Word(expected,cursor,0x5b00);Word(expected,cursor+2,mode && (int16_t)d>=64 ? far_code : near_code); }
        fx->rombr=0x5f;superfx_set_reg(fx,14,0x9000);superfx_set_reg(fx,12,0x5b00);
        superfx_set_reg(fx,8,(uint16_t)cursor);superfx_set_reg(fx,11,0xf800);
        unsigned pc=mode?0x90a5:0x9092;
        superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
        superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
        if(memcmp(ram,expected,65536)||fx->r[8].data!=cursor+(full?0:4)||fx->r[14].data!=0x9000+(mode?4:2)) {
            fprintf(stderr,"mode=%u distance=%04x slot=%u cursor=%04x stream=%04x\n",mode,d,slot,fx->r[8].data,fx->r[14].data);return 1;
        }
        cases++;
    }
    printf("Native attack queues: %u source-oracle cases passed\n",cases);
    const uint8_t radius[]={0xf0,0xd4,0x2b,0x67,0xa0,0x1e,0x09,0x2b,0x5c,0x40,0xa1,0x0d,0x2e,0x12,0x1e,0x51,0xef,0xde,0x3d,0xef,0x37,0xd7,0xd7,0xbc,0x3e,0x58,0x40,0x37,0xd7,0xd7,0xbc,0x3e,0x5c,0x40,0x37,0x22,0x1e,0xd7,0x05,0x0b,0xd7};
    CHECK(!memcmp(rom+0x10ca,radius,sizeof(radius)));cases=0;
    /* Sweep every damage word and every valid queue position through native
       removal. Coordinates vary independently by bijections of the sweep. */
    for(unsigned damage=0;damage<65536;damage++)for(unsigned slot=0;slot<=14;slot++) {
        superfx_reset(fx);memset(expected,0xa5,65536);
        unsigned object=0x5b00,cursor=0x2b80+slot*6;
        unsigned x=(damage*40503u+17u)&65535u,y=(damage*25173u+0x8000u)&65535u;
        Word(expected,object,0);Word(expected,object+2,0);Word(expected,object+4,0x06ea);
        Word(expected,object+8,x);Word(expected,object+12,y);expected[object+22]=0;
        Word(expected,object+30,0x9200);Word(expected,0x308c,object);
        Word(expected,0x172,1);Word(expected,0x174,1);Word(expected,0x7000,object-1);
        memcpy(ram,expected,65536);Word(rom,0x1f920d,damage);
        if(slot<14) { Word(expected,cursor,damage);Word(expected,cursor+2,x);Word(expected,cursor+4,y); }
        Word(expected,object,0xffff);Word(expected,object+4,0x06c2);Word(expected,0x308c,0);
        Word(expected,0x172,0);Word(expected,0x174,0);Word(expected,0x7000,0xffff);
        fx->rombr=0x5f;superfx_set_reg(fx,14,0x9000);superfx_set_reg(fx,12,object);
        superfx_set_reg(fx,7,cursor);superfx_set_reg(fx,10,0x7002);superfx_set_reg(fx,11,0xf800);
        superfx_cpu_write_io(fx,0x301e,0xca);superfx_cpu_write_io(fx,0x301f,0x90);
        superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
        if(memcmp(ram,expected,65536)||superfx_reg(fx,7)!=cursor+(slot==14?0:6)||superfx_reg(fx,14)!=0x9000) {
            fprintf(stderr,"radius damage=%04x slot=%u cursor=%04x stream=%04x\n",damage,slot,superfx_reg(fx,7),superfx_reg(fx,14));return 1;
        }
        CHECK(superfx_reg(fx,0)==0xffff);cases++;
    }
    printf("Native radius queue and removal: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}

