#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned expected_advance[4],advance_count,advance_seen,cases,execute_commands,last_advanced;
static unsigned lifecycle_calls[3],lifecycle_cases;
static void Lifecycle(SuperFx *fx,uint32_t pc,void *context) {
    (void)fx;(void)context;
    if(pc==0x90fd)lifecycle_calls[0]++;
    else if(pc==0x9129)lifecycle_calls[1]++;
    else { CHECK(pc==0x90f3);lifecycle_calls[2]++; }
}
static void Advance(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0x905e);
    if(execute_commands && last_advanced==superfx_reg(fx,12))return;
    CHECK(advance_seen<advance_count);
    CHECK(superfx_reg(fx,12)==expected_advance[advance_seen++]);
    last_advanced=superfx_reg(fx,12);
    /* Observe selection only; animation commands/lifecycle calls have their
     * own audits. Resume the native iterator after this selected object. */
    if(!execute_commands)superfx_hook_redirect(fx,0x9040);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static void Case(SuperFx *fx,uint8_t *expected,unsigned timer,uint16_t frames,unsigned holes) {
    superfx_reset(fx);memset(expected,0xa5,65536);advance_count=advance_seen=last_advanced=0;
    Put(expected,0x174,4);Put(expected,0x1ee,(uint16_t)(frames-1));Put(expected,0x196,0xf800);
    unsigned cursor=0x70b6;
    for(unsigned i=0;i<4;i++) {
        unsigned object=0x5b00+i*38;
        if(holes&(1u<<(i%2))) { Put(expected,cursor,0xffff);cursor+=2; }
        Put(expected,cursor,(uint16_t)(object-1));cursor+=2;
        expected[object+26]=(uint8_t)(timer+i*63);Put(expected,object+24,0x9000);
    }
    memcpy(fx->ram,expected,65536);
    for(unsigned i=0;i<4;i++) {
        unsigned object=0x5b00+i*38;uint8_t value=expected[object+26];
        if(value&0x80)continue;
        uint16_t intermediate=(uint16_t)(value-1-frames);
        if(intermediate&0x8000) {
            expected_advance[advance_count++]=object;
            if(execute_commands) {
                const unsigned durations[]={3,5,127};
                int remaining=(int)value-(int)frames;unsigned image=0;
                do { remaining+=(int)durations[image];if(remaining<=0)image=(image+1)%3; } while(remaining<=0);
                expected[object+23]=(uint8_t)(0x41+image);
                Put(expected,object+24,(uint16_t)(0x9000+(image+1)*2));
                expected[object+26]=(uint8_t)remaining;
            }
        }
        else expected[object+26]=(uint8_t)(value-frames);
    }
    Put(expected,0x2180,0);Put(expected,0x2b80,0);
    superfx_cpu_write_io(fx,0x301e,0x28);superfx_cpu_write_io(fx,0x301f,0x90);
    superfx_sync(fx,fx->master_clock+50000);CHECK(!superfx_is_running(fx));
    if(advance_seen!=advance_count || memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"animation iterator timer=%u frames=%04x holes=%u expected advances=%u actual=%u\n",timer,frames,holes,advance_count,advance_seen);exit(1);
    }
    CHECK(superfx_reg(fx,10)==cursor);CHECK(superfx_reg(fx,13)==0xffff);CHECK(superfx_reg(fx,9)==frames);cases++;
}
static void LifecycleCase(SuperFx *fx,uint8_t *expected,unsigned count,unsigned holes,unsigned mode,uint16_t flags) {
    superfx_reset(fx);memset(expected,0xa5,65536);memset(lifecycle_calls,0,sizeof(lifecycle_calls));
    Put(expected,0x174,(uint16_t)count);Put(expected,0x172,(uint16_t)count);
    Put(expected,0x1ee,0);Put(expected,0x196,0xf800);Put(expected,0x308c,0x5b00);
    unsigned cursor=0x70b6;
    for(unsigned i=0;i<count;i++) {
        unsigned object=0x5b00+i*38;
        if(holes&(1u<<(i%2))) { Put(expected,cursor,0xffff);cursor+=2; }
        Put(expected,cursor,(uint16_t)(object-1));cursor+=2;
        Put(expected,object,(uint16_t)(i+1<count ? object+38 : 0));Put(expected,object+2,(uint16_t)(i ? object-38 : 0));
        Put(expected,object+4,flags);expected[object+22]=0;Put(expected,object+24,0x9000);expected[object+26]=0;
    }
    memcpy(fx->ram,expected,65536);
    unsigned slot=0x70b6;
    for(unsigned i=0;i<count;i++) {
        unsigned object=0x5b00+i*38;
        if(holes&(1u<<(i%2)))slot+=2;
        Put(expected,slot,0xffff);slot+=2;
        Put(expected,object+4,(uint16_t)((flags|(mode==2 ? 0x10 : 0))&~0x28u));
        expected[object+23]=0x41;Put(expected,object+24,0x9005);expected[object+26]=3;
        if(mode==0) { Put(expected,object,0xffff);Put(expected,object+2,0); }
    }
    Put(expected,0x174,0);if(mode==0) { Put(expected,0x172,0);Put(expected,0x308c,0); }
    Put(expected,0x2180,0);Put(expected,0x2b80,0);
    unsigned routine=mode==0 ? 0x90fd : mode==1 ? 0x9129 : 0x90f3;
    fx->rom[0x1f9000]=0x81;Put(fx->rom,0x1f9001,(uint16_t)routine);
    fx->rom[0x1f9003]=3;fx->rom[0x1f9004]=0x41;
    superfx_cpu_write_io(fx,0x301e,0x28);superfx_cpu_write_io(fx,0x301f,0x90);
    superfx_sync(fx,fx->master_clock+100000);CHECK(!superfx_is_running(fx));
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"animation lifecycle count=%u holes=%u mode=%u flags=%04x\n",count,holes,mode,flags);exit(1);
    }
    CHECK(lifecycle_calls[0]==(mode==0 ? count : 0));CHECK(lifecycle_calls[1]==count);
    CHECK(lifecycle_calls[2]==(mode==2 ? count : 0));
    CHECK(superfx_reg(fx,10)==cursor && superfx_reg(fx,13)==0xffff);lifecycle_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0x3d,0xa9,0xf7,0xd9,0xfa,0xb6,0x70,0x3d,0xad,0xba,0xf8,0x80,0x21,0xf7,0x80,0x2b,
        0xfb,0x5e,0x90,0xa0,0x5f,0x3f,0xdf,0x02,0xed,0x0b,0xd9,0x1c,0x4a,0xda,0xdc,0x09,0xfa,0xda,
        0xa6,0x1a,0x26,0x5c,0x3d,0x46,0x95,0x0b,0xed,0xe0,0x15,0x69,0x0a,0x35,0xd5,0xa0,0x18,0x5c,0x1e,0x40};
    CHECK(!memcmp(rom+0x1028,entry,sizeof(entry)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);CHECK(superfx_set_pc_hook(fx,0x905e,Advance,NULL));
    const uint16_t frames[]={0,1,2,3,7,127,128,255,0x7fff,0x8000,0xffff};
    for(unsigned timer=0;timer<256;timer++)for(unsigned step=0;step<11;step++)for(unsigned holes=0;holes<4;holes++)
        Case(fx,expected,timer,frames[step],holes);
    printf("Retail processing-slot/animation-count iterator: %u source-oracle cases passed\n",cases);
    execute_commands=1;unsigned previous=cases;
    const uint8_t script[]={3,0x41,5,0x42,127,0x43,0x80,0x00,0x90};
    memcpy(rom+0x1f9000,script,sizeof(script));
    const uint16_t command_frames[]={0,1,2,3,7,8,9,16,128,255};
    for(unsigned timer=0;timer<256;timer++)for(unsigned step=0;step<10;step++)for(unsigned holes=0;holes<4;holes++)
        Case(fx,expected,timer,command_frames[step],holes);
    printf("Retail image commands/animation jumps/timer writes: %u source-oracle cases passed\n",cases-previous);
    CHECK(superfx_set_pc_hook(fx,0x905e,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0x90fd,Lifecycle,NULL));CHECK(superfx_set_pc_hook(fx,0x9129,Lifecycle,NULL));
    CHECK(superfx_set_pc_hook(fx,0x90f3,Lifecycle,NULL));
    const uint16_t flags[]={0,0x06ea,0x0642,0x0650,0xffff};
    for(unsigned count=1;count<=4;count++)for(unsigned holes=0;holes<4;holes++)for(unsigned mode=0;mode<3;mode++)for(unsigned fl=0;fl<5;fl++)
        LifecycleCase(fx,expected,count,holes,mode,flags[fl]);
    printf("Retail animation-command/lifecycle integration: %u source-oracle cases passed\n",lifecycle_cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
