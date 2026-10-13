#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xab08 || pc==0xac02);CHECK(!reached);reached=pc;
    superfx_hook_redirect(fx,0xf800);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t flags,uint16_t dead) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    expected[0x2c]=(uint8_t)dead;expected[0x2d]=(uint8_t)(dead>>8);memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,0,flags);
    superfx_cpu_write_io(fx,0x301e,0xf3);superfx_cpu_write_io(fx,0x301f,0xab);
    superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
    /* Original DEC/BMI checks the wrapped sign bit, not a host boolean. */
    int solid=(int16_t)(uint16_t)(flags-1)<0;
    int alive=(int16_t)(uint16_t)(dead-1)<0;
    unsigned want=!solid && alive ? 0xac02 : 0xab08;
    if(reached!=want) {
        fprintf(stderr,"item flags=%04x PlayerDead=%04x expected PC=%04x actual=%04x\n",flags,dead,want,reached);exit(1);
    }
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t code[]={0xe0,0x0b,0x08,0x01,0x3d,0xa0,0x16,0xe0,0x0b,0x05,0x01,0xff,0x08,0xab,0x01};
    CHECK(!memcmp(rom+0x2bf3,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xab08,Done,NULL));CHECK(superfx_set_pc_hook(fx,0xac02,Done,NULL));
    for(unsigned value=0;value<65536;value++) {
        Case(fx,expected,(uint16_t)value,0);Case(fx,expected,(uint16_t)value,1);
        Case(fx,expected,0x8000,(uint16_t)value);
    }
    printf("Retail item solid/dead-player pickup gate: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
