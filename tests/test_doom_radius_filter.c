#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned accepted;
static void Word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
static void Observe(SuperFx *fx,uint32_t pc,void *unused) {
 (void)unused;accepted=pc==0x9e07;superfx_hook_redirect(fx,0xf800);
}
int main(int argc,char **argv) {
 CHECK(argc==2);uint8_t *rom=malloc(0x200000),*ram=malloc(65536),*expected=malloc(65536);CHECK(rom&&ram&&expected);
 FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
 const uint8_t code[]={0xbd,0x3e,0x54,0x11,0x3d,0x40,0xb1,0x4d,0x50,0xa4,0x26,0x0b,0x03,0x01,0xa4,0x08,0x2a,0x0c,0xe4,0x54,0x50,0x0a,0xe0,0x01};
 CHECK(!memcmp(rom+0x1def,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
 SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
 CHECK(superfx_set_pc_hook(fx,0x9de6,Observe,NULL));CHECK(superfx_set_pc_hook(fx,0x9e07,Observe,NULL));unsigned cases=0;
 for(unsigned flags=0;flags<256;flags++)for(unsigned type=0;type<256;type++) {
  superfx_reset(fx);memset(expected,0xa5,65536);expected[0x5b04]=(uint8_t)flags;expected[0x5b05]=(uint8_t)type;memcpy(ram,expected,65536);accepted=2;
  superfx_set_reg(fx,13,0x5b00);superfx_set_reg(fx,10,0x5b00);
  superfx_cpu_write_io(fx,0x301e,0xef);superfx_cpu_write_io(fx,0x301f,0x9d);superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  unsigned want=(flags&0xa0)==0xa0,stride=flags&0x40?38:8;
  if(accepted!=want||superfx_reg(fx,10)!=0x5b00+stride||memcmp(ram,expected,65536)) {
   fprintf(stderr,"radius filter flags=%02x type=%02x accepted=%u expected=%u cursor=%04x\n",flags,type,accepted,want,superfx_reg(fx,10));return 1;
  }
  CHECK(superfx_reg(fx,13)==0x5b00);cases++;
 }
 printf("Radius target filter: %u source-oracle cases passed\n",cases);
 superfx_clear_pc_hooks(fx);CHECK(superfx_set_pc_hook(fx,0x9e07,Observe,NULL));CHECK(superfx_set_pc_hook(fx,0x9d56,Observe,NULL));cases=0;
 for(unsigned length=0;length<=8;length++)for(unsigned selected=0;selected<=length;selected++)for(unsigned pattern=0;pattern<256;pattern++) {
  superfx_reset(fx);memset(expected,0xa5,65536);unsigned cursor=0x5b00,target=0,after=0;
  for(unsigned i=0;i<length;i++) {
   unsigned movable=(pattern>>i)&1u;
   unsigned flags=i==selected?0xa0:((i%3)==0?0:(i%3)==1?0x80:0x20);
   flags|=movable?0x40:0;expected[cursor+4]=(uint8_t)flags;expected[cursor+5]=(uint8_t)(pattern+i*17);
   if(i==selected){target=cursor;after=cursor+(movable?38:8);}
   cursor+=movable?38:8;
  }
  memcpy(ram,expected,65536);accepted=2;superfx_set_reg(fx,10,0x5b00);superfx_set_reg(fx,6,cursor);
  superfx_cpu_write_io(fx,0x301e,0xe6);superfx_cpu_write_io(fx,0x301f,0x9d);superfx_sync(fx,fx->master_clock+30000);CHECK(!superfx_is_running(fx));
  unsigned want=selected<length;
  if(accepted!=want||superfx_reg(fx,10)!=(want?after:cursor)||superfx_reg(fx,13)!=(want?target:cursor)||memcmp(ram,expected,65536)) {
   fprintf(stderr,"radius traversal length=%u selected=%u pattern=%02x accepted=%u cursor=%04x target=%04x\n",length,selected,pattern,accepted,superfx_reg(fx,10),superfx_reg(fx,13));return 1;
  }
  cases++;
 }
 printf("Radius target traversal: %u source-oracle cases passed\n",cases);
 const uint8_t coords[]={0x3e,0xaa,0xbc,0x0c,0x0c,0x01,0xa0,0x08,0x5d,0x17,0x40,0x3e,0x54,0x18,0x05,0x18,0x40};
 CHECK(!memcmp(rom+0x1e07,coords,sizeof(coords)));
 superfx_clear_pc_hooks(fx);CHECK(superfx_set_pc_hook(fx,0x9e2f,Observe,NULL));cases=0;
 for(unsigned x=0;x<65536;x++) {
  unsigned y=(x*40503u+17u)&65535u,target=0x5b00;
  superfx_reset(fx);memset(expected,0xa5,65536);expected[target+4]=0xe0;Word(expected,target+8,x);Word(expected,target+12,y);
  memcpy(ram,expected,65536);Word(expected,0x178,target+38);
  superfx_set_reg(fx,10,target);superfx_set_reg(fx,13,target);
  superfx_cpu_write_io(fx,0x301e,0xef);superfx_cpu_write_io(fx,0x301f,0x9d);superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  if(superfx_reg(fx,7)!=x||superfx_reg(fx,8)!=y||superfx_reg(fx,10)!=target+38||memcmp(ram,expected,65536)) {
   fprintf(stderr,"radius movable coordinates x=%04x y=%04x actual=%04x/%04x\n",x,y,superfx_reg(fx,7),superfx_reg(fx,8));return 1;
  }
  CHECK(superfx_reg(fx,13)==target);cases++;
 }
 printf("Radius movable coordinate loads: %u source-oracle cases passed\n",cases);
 const uint8_t fixed[]={0xf0,0x98,0x00,0x3d,0x40,0x3f,0xdf,0xbd,0x3e,0x56,0x1e,0x40,0xef,0xde,0x17,0x3d,0xef,0xde,0xef,0xde,0x18,0x3d,0xef};
 CHECK(!memcmp(rom+0x1e18,fixed,sizeof(fixed)));cases=0;
 for(unsigned bank_index=0;bank_index<2;bank_index++)for(unsigned x=0;x<65536;x++) {
  unsigned bank=bank_index?0x5f:0x4f,offset=bank_index?0x1f9000:0xf9000;
  unsigned y=(x*25173u+0x8000u)&65535u,target=0x5b00;
  Word(rom,offset,x);Word(rom,offset+2,y);Word(rom,offset^0x100000,x^0xffffu);Word(rom,(offset^0x100000)+2,y^0xffffu);
  superfx_reset(fx);memset(expected,0xa5,65536);expected[target+4]=0xa0;expected[0x98]=(uint8_t)bank;Word(expected,target+6,0x9000);
  memcpy(ram,expected,65536);Word(expected,0x178,target+8);
  superfx_set_reg(fx,10,target);superfx_set_reg(fx,13,target);
  superfx_cpu_write_io(fx,0x301e,0xef);superfx_cpu_write_io(fx,0x301f,0x9d);superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  if(superfx_reg(fx,7)!=x||superfx_reg(fx,8)!=y||superfx_reg(fx,10)!=target+8||superfx_reg(fx,14)!=0x9003||fx->rombr!=bank||memcmp(ram,expected,65536)) {
   fprintf(stderr,"radius fixed bank=%02x coordinates=%04x/%04x actual=%04x/%04x\n",bank,x,y,superfx_reg(fx,7),superfx_reg(fx,8));return 1;
  }
  CHECK(superfx_reg(fx,13)==target);cases++;
 }
 printf("Radius fixed ROM coordinate loads: %u source-oracle cases passed\n",cases);
 const uint8_t setup[]={0xa0,0x7e,0x3d,0x40,0x3f,0xdf,0xf9,0x82,0x71,0x3d,0xac,0x3e,0x3d,0xa1,0x40,0x3e,0xad,0xbd,0x3d,0xad,0x0c,0xdd,0xbd,0x90,0xff,0x69,0x9e,0xb1};
 CHECK(!memcmp(rom+0x1e2f,setup,sizeof(setup)));
 superfx_clear_pc_hooks(fx);CHECK(superfx_set_pc_hook(fx,0x9e69,Observe,NULL));cases=0;
 for(unsigned count=0;count<65536;count++)for(unsigned bank_index=0;bank_index<2;bank_index++) {
  unsigned bank=bank_index?0x5f:0x4f,root=(count*40503u+17u)&65535u;
  superfx_reset(fx);memset(expected,0xa5,65536);Word(expected,0x7c,0x8000);expected[0x7e]=(uint8_t)bank;Word(expected,0x80,root);Word(expected,0x18,count);
  memcpy(ram,expected,65536);Word(expected,0x17a,0x5b00);Word(expected,0x18,(count+1)&65535u);
  superfx_set_reg(fx,13,0x5b00);superfx_cpu_write_io(fx,0x301e,0x2f);superfx_cpu_write_io(fx,0x301f,0x9e);
  superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  if(memcmp(ram,expected,65536)||fx->rombr!=bank||superfx_reg(fx,9)!=0x7182||superfx_reg(fx,12)!=0x8000||superfx_reg(fx,1)!=root||superfx_reg(fx,13)!=((count+1)&65535u)||fx->sreg!=1) {
   fprintf(stderr,"radius LOS setup count=%04x bank=%02x root=%04x\n",count,bank,root);return 1;
  }
  cases++;
 }
 printf("Radius line-of-sight setup: %u source-oracle cases passed\n",cases);superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
