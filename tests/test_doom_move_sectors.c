#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa289);reached++;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *ram,unsigned address,uint16_t value) {
    ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8);
}
static void Run(SuperFx *fx,unsigned pc) {
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
}
static void Initial(SuperFx *fx,uint8_t *expected,unsigned sector,uint16_t previous) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x11c,0x5ab4);expected[0x5ab4+22]=(uint8_t)sector;
    Put(expected,0x14,previous);Put(expected,0x7180,0xffff);
    memcpy(fx->ram,expected,65536);
    uint16_t marker=(uint16_t)(previous+1);
    Put(expected,0x14,marker);Put(expected,0x308a+sector*14,marker);
    Put(expected,0xd80,(uint16_t)(0x3082+sector*14));Put(expected,0xd82,0xff80);
    Put(expected,0x14e,0xd82);expected[0x123]=0;
    Run(fx,0xa132);CHECK(reached==1); /* Far-jump setup reuses R13 as bank. */
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
static void Add(SuperFx *fx,uint8_t *expected,unsigned sector,uint16_t marker,int present) {
    superfx_reset(fx);memset(expected,0xa5,65536);
    Put(expected,0x308a+sector*14,present ? marker : (uint16_t)(marker+1));
    Put(expected,0x14e,0xd86);memcpy(fx->ram,expected,65536);
    if(!present) {
        Put(expected,0x308a+sector*14,marker);
        Put(expected,0xd86,(uint16_t)(0x3082+sector*14));Put(expected,0x14e,0xd88);
    }
    superfx_set_reg(fx,0,(uint16_t)sector);superfx_set_reg(fx,13,marker);superfx_set_reg(fx,11,0xf800);
    Run(fx,0xa258);
    CHECK(superfx_reg(fx,2)==0x3082+sector*14);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
static unsigned line_cases;
static uint16_t Word(const uint8_t *data,unsigned address) {
    return data[address]|((uint16_t)data[address+1]<<8);
}
static void Line(SuperFx *fx,uint8_t *expected,uint16_t angle,uint16_t flags,unsigned near,unsigned far,
                 uint16_t nf,uint16_t ff,uint16_t nc,uint16_t fc) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x11c,0x5ab4);expected[0x5ab4+22]=0;Put(expected,0x14,0xffff);
    Put(expected,0x7180,0);Put(expected,0x7196,0xffff);
    Put(expected,0x86,0x8000);Put(expected,0x84,0x8800);Put(expected,0x94,0x9000);
    expected[0x7e]=0x4f;expected[0x122]=2;
    Put(expected,0x3082+near*14,nf);Put(expected,0x3084+near*14,nc);
    Put(expected,0x3082+far*14,ff);Put(expected,0x3084+far*14,fc);
    /* If sectors alias, the final fixture contents define their heights. */
    nf=Word(expected,0x3082+near*14);nc=Word(expected,0x3084+near*14);
    memcpy(fx->ram,expected,65536);
    Put(fx->rom,0xf8000,0);Put(fx->rom,0xf8002,4);
    Put(fx->rom,0xf8004,flags);Put(fx->rom,0xf8006,angle);Put(fx->rom,0xf8008,0);
    Put(fx->rom,0xf8800,0x8000);Put(fx->rom,0xf8802,0xffff);
    Put(fx->rom,0xf8804,0x7fff);Put(fx->rom,0xf8806,128);
    fx->rom[0xf9000]=(uint8_t)near;fx->rom[0xf9001]=(uint8_t)far;
    Put(expected,0x14,0);Put(expected,0x308a,0);Put(expected,0xd80,0x3082);
    unsigned cursor=0xd82,seen[3]={1,0,0};
    unsigned sectors[2]={near,far},number=(flags&1)?1:2;
    for(unsigned i=0;i<number;i++)if(!seen[sectors[i]]) {
        seen[sectors[i]]=1;Put(expected,0x308a+sectors[i]*14,0);
        Put(expected,cursor,(uint16_t)(0x3082+sectors[i]*14));cursor+=2;
    }
    Put(expected,cursor,0xff80);Put(expected,0x14e,(uint16_t)cursor);
    expected[0x123]=(uint8_t)(!(flags&1) && ((flags>>8)&2));
    Put(expected,0x7182,0x8000);Put(expected,0x7184,0xffff);
    unsigned index=(angle>>6)*2;
    Put(expected,0x7186,Word(fx->rom,0xae4d1+index));
    Put(expected,0x7188,Word(fx->rom,0xae6d1+index));
    Put(expected,0x718a,flags);Put(expected,0x718c,0x7fff);Put(expected,0x718e,128);
    if(!(flags&1)) {
        Put(expected,0x7190,(int16_t)nf>(int16_t)ff ? nf : ff);
        Put(expected,0x7192,(int16_t)nc<(int16_t)fc ? nc : fc);
        Put(expected,0x7194,(int16_t)nf<(int16_t)ff ? nf : ff);
    }
    Run(fx,0xa132);CHECK(reached==1);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"line angle=%04x flags=%04x near=%u far=%u floors=%04x,%04x ceilings=%04x,%04x\n",angle,flags,near,far,nf,ff,nc,fc);
        for(unsigned a=0;a<65536;a++)if(fx->ram[a]!=expected[a])
            fprintf(stderr,"RAM[%04x] expected=%02x actual=%02x\n",a,expected[a],fx->ram[a]);
        exit(1);
    }
    line_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t init[]={0x3d,0xa1,0x8e,0x3d,0xad,0x0a,0xdd,0xbd,0x90,0xa0,0x16,0x51,0x3d,0x40,0x3f,0x8e};
    const uint8_t add[]={0x3f,0x8e,0xf2,0x8a,0x30,0x22,0x50,0x42,0x22,0x3e,0x68,0x6d,0x09,0x0b,0xbd,0x90};
    CHECK(!memcmp(rom+0x2132,init,sizeof(init)));CHECK(!memcmp(rom+0x2258,add,sizeof(add)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa289,Done,NULL));
    const uint16_t markers[]={0,1,0x7fff,0x8000,0xffff};
    for(unsigned sector=0;sector<256;sector++)for(unsigned m=0;m<5;m++) {
        Initial(fx,expected,sector,markers[m]);
        Add(fx,expected,sector,markers[m],0);Add(fx,expected,sector,markers[m],1);
    }
    printf("Retail movement-sector initialization/deduplication: %u source-oracle cases passed\n",cases);
    const uint16_t heights[]={0,1,56,128,0x7fff,0x8000,0xffff};
    const uint16_t flags[]={0,1,2,3,0x0200,0x0201,0xffff};
    for(unsigned angle=0;angle<65536;angle+=257)for(unsigned flag=0;flag<7;flag++)
        for(unsigned n=0;n<3;n++)for(unsigned h=0;h<7;h++)
            Line(fx,expected,(uint16_t)angle,flags[flag],n,(n+1)%3,heights[h],heights[6-h],heights[(h+2)%7],heights[(h+4)%7]);
    printf("Retail solid/two-sided line classification: %u source-oracle cases passed\n",line_cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
