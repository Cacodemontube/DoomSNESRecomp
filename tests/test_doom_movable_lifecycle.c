#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned cases;
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static unsigned Object(unsigned i) { return 0x5b00+i*38; }
static void Case(SuperFx *fx,uint8_t *expected,unsigned sector,unsigned length,unsigned position,
                 uint16_t flags_type,uint16_t count,unsigned mode) {
    superfx_reset(fx);memset(expected,0xa5,65536);
    unsigned object=Object(position),previous=position ? Object(position-1) : 0;
    unsigned next=position+1<length ? Object(position+1) : 0;
    Put(expected,0x308c+sector*14,0x5b00);
    for(unsigned i=0;i<length;i++) {
        Put(expected,Object(i),(uint16_t)(i+1<length ? Object(i+1) : 0));
        Put(expected,Object(i)+2,(uint16_t)(i ? Object(i-1) : 0));expected[Object(i)+22]=(uint8_t)sector;
    }
    Put(expected,object+4,flags_type);Put(expected,0x172,count);Put(expected,0x174,count);
    Put(expected,0x7000,(uint16_t)(object-1));memcpy(fx->ram,expected,65536);
    Put(expected,0x174,(uint16_t)(count-1));Put(expected,0x7000,0xffff);
    if(mode==0) {
        Put(expected,0x172,(uint16_t)(count-1));Put(expected,object,0xffff);
        Put(expected,previous ? previous : 0x308c+sector*14,(uint16_t)next);
        if(next)Put(expected,next+2,(uint16_t)previous);
    }
    if(mode==2)flags_type|=0x10;
    Put(expected,object+4,(uint16_t)(flags_type&~0x0028u));
    unsigned pc=mode==0 ? 0x90fd : mode==1 ? 0x9129 : 0x90f3;
    superfx_set_reg(fx,12,(uint16_t)object);superfx_set_reg(fx,10,0x7002);superfx_set_reg(fx,11,0xf800);
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"movable lifecycle mode=%u sector=%u length=%u position=%u flags/type=%04x count=%04x\n",mode,sector,length,position,flags_type,count);
        for(unsigned a=0;a<65536;a++)if(fx->ram[a]!=expected[a])
            fprintf(stderr,"RAM[%04x] expected=%02x actual=%02x\n",a,expected[a],fx->ram[a]);exit(1);
    }
    CHECK(superfx_reg(fx,12)==object && superfx_reg(fx,10)==0x7002);CHECK(superfx_reg(fx,0)==0xffff);cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t convert[]={0xbc,0x3e,0x54,0x40,0xa1,0x10,0xc1,0x05,0x2d,0x90};
    const uint8_t remove[]={0xa0,0x16,0x5c,0x3d,0x40,0x3f,0x8e,0xf1,0x8c,0x30,0x21,0x50,0x3d,0xa0,0xb9,0xe0,0x90};
    const uint8_t corpse[]={0x3d,0xa0,0xba,0xe0,0x90,0xbc,0x3e,0x54,0x40,0xa1,0x28,0x3d,0x71,0x90};
    CHECK(!memcmp(rom+0x10f3,convert,sizeof(convert)));CHECK(!memcmp(rom+0x10fd,remove,sizeof(remove)));
    CHECK(!memcmp(rom+0x1129,corpse,sizeof(corpse)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    for(unsigned flags=0;flags<65536;flags++)for(unsigned mode=0;mode<3;mode++)
        Case(fx,expected,17,4,1,(uint16_t)flags,4,mode);
    const uint16_t flags[]={0,0x0642,0x06a8,0x06ff,0xffff};
    for(unsigned sector=0;sector<256;sector++)for(unsigned length=1;length<=8;length++)
        for(unsigned position=0;position<length;position++)for(unsigned fl=0;fl<5;fl++)for(unsigned mode=0;mode<3;mode++)
            Case(fx,expected,sector,length,position,flags[fl],(uint16_t)length,mode);
    const uint16_t counts[]={0,1,0x7fff,0x8000,0xffff};
    for(unsigned c=0;c<5;c++)for(unsigned fl=0;fl<5;fl++)for(unsigned mode=0;mode<3;mode++)
        Case(fx,expected,17,4,1,flags[fl],counts[c],mode);
    printf("Retail movable free/corpse/item lifecycle: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
