#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned reached,cases;
static void Done(SuperFx *fx,uint32_t pc,void *context) {
    (void)context;CHECK(pc==0xa132);CHECK(!reached);reached=1;
    superfx_hook_redirect(fx,0xf800);
}
static void Put(uint8_t *bytes,unsigned address,uint16_t value) {
    bytes[address]=(uint8_t)value;bytes[address+1]=(uint8_t)(value>>8);
}
static void Case(SuperFx *fx,uint8_t *expected,unsigned width,unsigned height,int sx,int sy,
                 uint16_t marker,unsigned premarked,unsigned salt) {
    superfx_reset(fx);reached=0;memset(expected,0xa5,65536);
    Put(expected,0x98,0x4f);Put(expected,0x9e,0x8000);Put(expected,0x18,marker);
    unsigned seen[16]={0};
    for(unsigned line=0;line<16;line++) {
        seen[line]=(premarked && line%premarked==0);
        Put(expected,0x51d2+line*2,seen[line] ? marker : (uint16_t)(marker+1));
    }
    memcpy(fx->ram,expected,65536);
    /* Relative offsets are based at the pointer's high-byte address, as the
     * original GETB/INC/GETBH sequence specifies, not the slot start. */
    unsigned list[20][4],length[20];
    for(unsigned cell=0;cell<width*height;cell++) {
        length[cell]=(cell+salt)%5;
        unsigned address=0x9000+cell*32;
        unsigned slot=0x8000+cell*2;
        Put(fx->rom,0xf0000+slot,(uint16_t)(address-slot-1));
        for(unsigned item=0;item<length[cell];item++) {
            list[cell][item]=(cell*3+item*7+salt)%16;
            if(item==2)list[cell][item]=list[cell][0];
            Put(fx->rom,0xf0000+address+item*2,(uint16_t)list[cell][item]);
        }
        Put(fx->rom,0xf0000+address+length[cell]*2,0xffff);
    }
    unsigned output=0x7180;
    for(unsigned row=0;row<height;row++)for(unsigned column=0;column<width;column++) {
        unsigned x=sx<0 ? width-1-column : column,y=sy<0 ? height-1-row : row;
        unsigned cell=y*width+x;
        for(unsigned item=0;item<length[cell];item++) {
            unsigned line=list[cell][item];
            if(seen[line])continue;seen[line]=1;
            Put(expected,0x51d2+line*2,marker);Put(expected,output,(uint16_t)line);output+=22;
        }
    }
    Put(expected,output,0xffff);
    superfx_set_reg(fx,1,(uint16_t)(sx<0 ? width-1 : 0));
    superfx_set_reg(fx,2,(uint16_t)(sy<0 ? height-1 : 0));
    superfx_set_reg(fx,3,(uint16_t)width);superfx_set_reg(fx,4,(uint16_t)height);
    superfx_set_reg(fx,5,(uint16_t)(sx*2));
    superfx_set_reg(fx,6,(uint16_t)(sy*(int)(width*2)-sx*(int)(width*2)));
    superfx_set_reg(fx,12,(uint16_t)(width*2));
    superfx_cpu_write_io(fx,0x301e,0xb4);superfx_cpu_write_io(fx,0x301f,0xa0);
    superfx_sync(fx,fx->master_clock+100000);
    CHECK(!superfx_is_running(fx));CHECK(reached);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"width=%u height=%u direction=%d,%d marker=%04x premarked=%u salt=%u\n",width,height,sx,sy,marker,premarked,salt);
        for(unsigned a=0;a<65536;a++)if(fx->ram[a]!=expected[a])
            fprintf(stderr,"RAM[%04x] expected=%02x actual=%02x\n",a,expected[a],fx->ram[a]);
        exit(1);
    }
    CHECK(superfx_reg(fx,10)==output);cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0xf0,0x98,0x00,0x3d,0x40,0x3f,0xdf,0xbc,0x82,0x3d,0xac,0x4f,0x51,0x51,0x1e,0x5c};
    const uint8_t finish[]={0x2a,0x5c,0xa0,0xff,0x3a,0xf0,0x32,0xa1,0xa8,0x00,0x3d,0x98,0x01};
    CHECK(!memcmp(rom+0x20b4,entry,sizeof(entry)));CHECK(!memcmp(rom+0x2110,finish,sizeof(finish)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    CHECK(superfx_set_pc_hook(fx,0xa132,Done,NULL));
    const uint16_t markers[]={0,1,0x7fff,0x8000,0xffff};
    for(unsigned w=1;w<=5;w++)for(unsigned h=1;h<=4;h++)
        for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2)
            for(unsigned m=0;m<5;m++)for(unsigned pre=0;pre<4;pre++)for(unsigned salt=0;salt<8;salt++)
                Case(fx,expected,w,h,sx,sy,markers[m],pre,salt);
    printf("Retail block-map line gathering: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
