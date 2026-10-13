#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
typedef struct {uint16_t x,y,dx,dy,left,right;} Node;
static Node nodes[7];
static unsigned cases;
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static unsigned Oracle(uint16_t x,uint16_t y) {
    unsigned index=0;
    for(unsigned depth=0;depth<4;depth++) {
        Node n=nodes[index];
        int16_t rx=(int16_t)(uint16_t)(x-n.x),ry=(int16_t)(uint16_t)(y-n.y);
        uint32_t cross=(uint32_t)((int16_t)n.dx*ry)-(uint32_t)((int16_t)n.dy*rx);
        unsigned child=(cross&0x80000000u) ? n.right : n.left;
        if(child&0x8000)return (child-0x8000)/4;
        CHECK(child%28==0);index=child/28;CHECK(index<7);
    }
    CHECK(0);return 0;
}
static void Tree(SuperFx *fx,unsigned salt) {
    const uint16_t vectors[][2]={{0x7fff,0},{0,0x7fff},{0x8000,0},{0,0x8000},{0x5a82,0x5a82},{0x5a82,0xa57e}};
    for(unsigned i=0;i<7;i++) {
        Node *n=&nodes[i];unsigned v=(i+salt)%6;
        n->x=(uint16_t)(i*128+salt*0x7fff);n->y=(uint16_t)(i*257+salt*0x8000);
        n->dx=vectors[v][0];n->dy=vectors[v][1];
        unsigned left=i*2+1,right=i*2+2;
        n->left=(uint16_t)(left<7 ? left*28 : 0x8000+(left-7)*4);
        n->right=(uint16_t)(right<7 ? right*28 : 0x8000+(right-7)*4);
        unsigned address=0xf8000+i*28;
        Put(fx->rom,address,n->y);Put(fx->rom,address+2,n->dx);
        Put(fx->rom,address+4,n->x);Put(fx->rom,address+6,n->dy);
        Put(fx->rom,address+16,n->left);Put(fx->rom,address+26,n->right);
    }
    for(unsigned leaf=0;leaf<8;leaf++)fx->rom[0xf9003+leaf*4]=(uint8_t)(leaf*31+salt*7);
}
static void Case(SuperFx *fx,uint8_t *expected,uint16_t x,uint16_t y,unsigned mode,unsigned salt) {
    superfx_reset(fx);memset(expected,0xa5,65536);
    Put(expected,0x7c,0x8000);expected[0x7e]=0x4f;
    Put(expected,0x80,0);Put(expected,0x82,0x1000);memcpy(fx->ram,expected,65536);
    unsigned leaf=Oracle(x,y),sector=(leaf*31+salt*7)&255;
    unsigned pc=mode==0 ? 0x8ce3 : mode==1 ? 0x8cc9 : 0x8cb3;
    superfx_set_reg(fx,7,x);superfx_set_reg(fx,8,y);superfx_set_reg(fx,11,0xf800);
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+20000);
    if(superfx_is_running(fx)) {
        fprintf(stderr,"BSP did not return: mode=%u x=%04x y=%04x pc=%04x R0=%04x R14=%04x ROMBR=%02x\n",
            mode,x,y,superfx_reg(fx,15),superfx_reg(fx,0),superfx_reg(fx,14),fx->rombr);exit(1);
    }
    unsigned actual=mode==0 ? superfx_reg(fx,0) : superfx_reg(fx,6);
    unsigned want=mode==0 ? 0x9000+leaf*4 : sector;
    if(actual!=want) {
        fprintf(stderr,"BSP x=%04x y=%04x mode=%u salt=%u leaf=%u expected=%04x actual=%04x\n",x,y,mode,salt,leaf,want,actual);exit(1);
    }
    if(mode==2)CHECK(superfx_reg(fx,9)==0x3080+sector*14);
    CHECK(superfx_reg(fx,7)==x && superfx_reg(fx,8)==y);
    CHECK(!memcmp(fx->ram,expected,65536));cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t entry[]={0xa0,0x7e,0x3d,0x40,0x3f,0xdf,0x3d,0xa5,0x3e,0x3d,0xa0,0x40,0x02};
    CHECK(!memcmp(rom+0xce3,entry,sizeof(entry)));rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    for(unsigned salt=0;salt<2;salt++) {
        Tree(fx,salt);
        for(unsigned value=0;value<65536;value++)for(unsigned mode=0;mode<3;mode++)
            Case(fx,expected,(uint16_t)value,(uint16_t)~value,mode,salt);
        const uint16_t edges[]={0,1,127,128,0x7fff,0x8000,0xffff};
        for(unsigned x=0;x<7;x++)for(unsigned y=0;y<7;y++)for(unsigned mode=0;mode<3;mode++)
            Case(fx,expected,edges[x],edges[y],mode,salt);
    }
    printf("Retail BSP area/sector traversal: %u source-oracle cases passed\n",cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
