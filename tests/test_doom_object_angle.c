#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint8_t inverse[32768];
static uint16_t atan_table[1024],actual;
static unsigned reached,cases;
static uint16_t Word(const uint8_t *data,unsigned address) {
    return data[address]|((uint16_t)data[address+1]<<8);
}
static uint16_t Oracle(int dx,int dy) {
    if(!dx)return dy<0 ? 0xc000 : 0x4000;
    if(!dy)return dx<0 ? 0x8000 : 0;
    unsigned x=(unsigned)abs(dx),y=(unsigned)abs(dy),angle;
    if(x==y)angle=0x2000;
    else {
        int swap=y>x;
        if(swap){unsigned temporary=x;x=y;y=temporary;}
        unsigned reciprocal=Word(inverse,x*2);
        unsigned slope=x<=128 ? y*reciprocal : (2*y*reciprocal)>>8;
        slope&=65535;CHECK((slope>>5)<1024);
        angle=atan_table[slope>>5];
        if(swap)angle=0x4000-angle;
    }
    if(dx<0)angle=dy<0 ? 0x8000+angle : 0x8000-angle;
    else if(dy<0)angle=0-angle;
    return (uint16_t)angle;
}
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa393);CHECK(!reached);reached=1;
    actual=superfx_reg(fx,0);superfx_hook_redirect(fx,0xf800);
}
static void GeometryDone(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa2a8);CHECK(!reached);reached=1;
    CHECK(superfx_reg(fx,8)==0x7196);superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static unsigned geometry_cases;
static void Geometry(SuperFx *fx,uint8_t *expected,uint16_t angle,unsigned type,unsigned flags,uint16_t x,uint16_t y) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    expected[0x98]=0x4f;expected[0x5b04]=(uint8_t)flags;expected[0x5b05]=(uint8_t)type;
    Put(expected,0xd80,0x3082);memcpy(fx->ram,expected,65536);
    unsigned index=(angle>>6)*2;
    uint16_t pux=(uint16_t)(0-Word(fx->rom,0xae6d1+index));
    uint16_t puy=Word(fx->rom,0xae4d1+index);
    unsigned diameter=fx->rom[0x1bfe9c+type*5];
    uint16_t ox=(uint16_t)((uint32_t)((int16_t)pux*(int)diameter)>>16);
    uint16_t oy=(uint16_t)((uint32_t)((int16_t)puy*(int)diameter)>>16);
    uint16_t centerx=(uint16_t)(x+ox),centery=(uint16_t)(y-oy);
    Put(expected,0x7180,0x5b00);Put(expected,0x7182,(uint16_t)(centerx-oy));
    Put(expected,0x7184,(uint16_t)(centery-ox));Put(expected,0x7186,pux);Put(expected,0x7188,puy);
    Put(expected,0x718a,(flags&0x10)?0x8000:0x8001);
    Put(expected,0x718c,(uint16_t)(centerx+oy));Put(expected,0x718e,(uint16_t)(centery+ox));
    Put(expected,0x7190,0x308c);if(flags&0x10)expected[0x123]=1;
    superfx_set_reg(fx,0,angle);superfx_set_reg(fx,7,0x5b00);superfx_set_reg(fx,5,0xd80);
    superfx_set_reg(fx,8,0x7180);superfx_set_reg(fx,9,x);superfx_set_reg(fx,10,y);
    superfx_cpu_write_io(fx,0x301e,0x93);superfx_cpu_write_io(fx,0x301f,0xa3);
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(reached);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"geometry angle=%04x type=%u flags=%02x diameter=%u\n",angle,type,flags,diameter);
        for(unsigned a=0;a<65536;a++)if(fx->ram[a]!=expected[a])
            fprintf(stderr,"RAM[%04x] expected=%02x actual=%02x\n",a,expected[a],fx->ram[a]);
        exit(1);
    }
    geometry_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==4);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1);CHECK(rom && ram);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    f=fopen(argv[2],"rb");CHECK(f);CHECK(fread(inverse,1,sizeof(inverse),f)==sizeof(inverse));CHECK(fgetc(f)==EOF);fclose(f);
    CHECK(!memcmp(rom+0x1a0000,inverse,sizeof(inverse)));
    f=fopen(argv[3],"r");CHECK(f);char line[256];unsigned count=0;int active=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"PArcTanTable",12)){active=1;continue;}
        if(!strncmp(line,"TanTable",8))break;
        unsigned value;if(active && sscanf(line," dc.w $%x",&value)==1) {
            CHECK(count<1024);atan_table[count++]=(uint16_t)value;
        }
    }
    fclose(f);CHECK(count==1024);
    for(unsigned i=0;i<1024;i++)CHECK(Word(rom,0xaeed1+i*2)==atan_table[i]);
    const uint8_t entry[]={0x26,0xb2,0x08,0x11,0x23,0xb3,0x0a,0x07,0xf0,0x00,0xc0,0xff,0x93,0xa3,0x01,0x00,0x40};
    CHECK(!memcmp(rom+0x22f6,entry,sizeof(entry)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa393,Done,NULL));
    for(int dx=-256;dx<256;dx++)for(int dy=-256;dy<256;dy++) {
        superfx_reset(fx);reached=0;memset(ram,0xa5,65536);
        superfx_set_reg(fx,2,(uint16_t)dx);superfx_set_reg(fx,3,(uint16_t)dy);
        superfx_cpu_write_io(fx,0x301e,0xf6);superfx_cpu_write_io(fx,0x301f,0xa2);
        superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(reached);
        uint16_t expected=Oracle(dx,dy);
        if(actual!=expected){fprintf(stderr,"dx=%d dy=%d expected=%04x actual=%04x\n",dx,dy,expected,actual);exit(1);}
        for(unsigned a=0;a<65536;a++)CHECK(ram[a]==0xa5);cases++;
    }
    printf("Retail object collision angle: %u source-oracle cases passed; inverse and arctangent tables match original source\n",cases);
    CHECK(superfx_set_pc_hook(fx,0xa393,NULL,NULL));
    CHECK(superfx_set_pc_hook(fx,0xa2a8,GeometryDone,NULL));
    uint8_t *expected=malloc(65536);CHECK(expected);
    const uint8_t geometry[]={0x03,0x03,0x03,0x03,0x03,0x11,0x3f,0x71,0xa0,0x4a,0x3f,0xdf};
    CHECK(!memcmp(rom+0x2393,geometry,sizeof(geometry)));
    for(unsigned angle=0;angle<65536;angle++) {
        Geometry(fx,expected,(uint16_t)angle,angle%70,0,0x7fff,0x8000);
        Geometry(fx,expected,(uint16_t)angle,angle%70,0x10,0xffff,1);
        Geometry(fx,expected,(uint16_t)angle,angle%70,0xff,0,0);
    }
    printf("Retail object collision plane geometry: %u source-oracle cases passed\n",geometry_cases);
    free(expected);
    superfx_destroy(fx);free(rom);free(ram);return 0;
}
