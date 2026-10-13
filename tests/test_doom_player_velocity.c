#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint16_t table[1280];
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0x8296);CHECK(!reached);reached=1;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *ram,unsigned address,uint16_t value) {
    ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8);
}
static uint16_t Scale(uint16_t mouse,uint16_t ratio) {
    return (uint16_t)((uint32_t)((int16_t)mouse*(int16_t)ratio)>>8);
}
static void Add(uint16_t *vx,uint16_t *vy,uint16_t angle,uint16_t mouse,uint16_t ratio) {
    unsigned index=angle>>6;
    int16_t velocity=(int16_t)Scale(mouse,ratio);
    uint16_t dx=(uint16_t)((uint32_t)((int16_t)table[index+256]*velocity)>>16);
    uint16_t dy=(uint16_t)((uint32_t)((int16_t)table[index]*velocity)>>16);
    *vx=(uint16_t)(*vx+dx);*vy=(uint16_t)(*vy+dy);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t angle,uint16_t x,uint16_t y,
                 uint16_t ratio,unsigned pad,uint16_t vx,uint16_t vy) {
    superfx_reset(fx);reached=0;
    memset(expected,0xa5,65536);
    Put(expected,0x18c,0x5a8e);Put(expected,0x1f2,x);Put(expected,0x1f4,y);
    Put(expected,8,ratio);Put(expected,0x5aa2,angle);
    Put(expected,0x5a9e,vx);Put(expected,0x5aa0,vy);
    memcpy(fx->ram,expected,65536);
    superfx_set_reg(fx,9,(uint16_t)pad);
    if(pad&0x30)Add(&vx,&vy,(uint16_t)(angle+((pad&0x20)?0x4000:0xc000)),x,ratio);
    else CHECK(x==0); /* Rotation is verified by the other player test. */
    if(y)Add(&vx,&vy,angle,y,ratio);
    Put(expected,0x5a9e,vx);Put(expected,0x5aa0,vy);
    superfx_cpu_write_io(fx,0x301e,0xc1);superfx_cpu_write_io(fx,0x301f,0x81);
    superfx_sync(fx,fx->master_clock+20000);
    CHECK(!superfx_is_running(fx));CHECK(reached);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"angle=%04x x=%04x y=%04x ratio=%04x pad=%02x\n",angle,x,y,ratio,pad);
        for(unsigned a=0;a<65536;a++)if(fx->ram[a]!=expected[a])
            fprintf(stderr,"RAM[%04x]: expected=%02x actual=%02x\n",a,expected[a],fx->ram[a]);
        exit(1);
    }
    cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==3);
    uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    /* Read exact original assembly constants, independently of GSU ROM reads.
     * CosTable aliases SinTable+256 entries; 1280 entries cover both tables. */
    f=fopen(argv[2],"r");CHECK(f);char line[256];unsigned count=0;int active=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"SinTable",8)){active=1;continue;}
        if(!strncmp(line,"PArcTanTable",12))break;
        unsigned value;
        if(active && sscanf(line," dc.w $%x",&value)==1) {
            CHECK(count<1280);table[count++]=(uint16_t)value;
        }
    }
    fclose(f);CHECK(count==1280);
    for(unsigned i=0;i<1280;i++) {
        unsigned offset=0xae4d1+i*2;
        CHECK((rom[offset]|((unsigned)rom[offset+1]<<8))==table[i]);
    }
    const uint8_t entry[]={0x29,0x10,0xa1,0x20,0x71,0x09,0x07,0x01};
    const uint8_t forward[]={0x3d,0xa6,0xfa,0x26,0xb6,0x09,0x49,0x01};
    CHECK(!memcmp(rom+0x1c1,entry,sizeof(entry)));
    CHECK(!memcmp(rom+0x246,forward,sizeof(forward)));
    CHECK(rom[0x1fc]==0xf0 && rom[0x1fd]==0xd1 && rom[0x1fe]==0xe4);
    CHECK(rom[0x211]==0xf0 && rom[0x212]==0xd1 && rom[0x213]==0xe6);
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0x8296,Done,NULL));
    for(unsigned angle=0;angle<65536;angle++) {
        Case(fx,expected,(uint16_t)angle,0,512,256,0,0,0);
        Case(fx,expected,(uint16_t)angle,512,0,256,0x20,0,0);
        Case(fx,expected,(uint16_t)angle,512,0,256,0x10,0,0);
        Case(fx,expected,(uint16_t)angle,512,512,256,0x30,0x7fff,0xffff);
    }
    const uint16_t values[]={0,1,255,256,257,0x7fff,0x8000,0xffff};
    for(unsigned value=0;value<65536;value++) {
        Case(fx,expected,0x2345,0,(uint16_t)value,257,0,0xffff,0x7fff);
        Case(fx,expected,0x2345,(uint16_t)value,0,257,0x10,0xffff,0x7fff);
        Case(fx,expected,0x9876,0,513,(uint16_t)value,0,0x8000,0);
        Case(fx,expected,0x9876,513,513,(uint16_t)value,0x20,0x8000,0);
    }
    for(unsigned m=0;m<8;m++)for(unsigned r=0;r<8;r++)for(unsigned a=0;a<8;a++) {
        Case(fx,expected,values[a],0,values[m],values[r],0,0x7fff,0x8000);
        Case(fx,expected,values[a],values[m],values[m],values[r],0x20,0xffff,1);
    }
    printf("Retail player forward/strafe velocity: %u source-oracle cases passed; 1280 original table words match\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
