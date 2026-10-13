#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned outcome;
static unsigned range_prefix_seen;
static void RangePrefix(SuperFx *fx,uint32_t pc,void *unused) {
 (void)unused;CHECK(pc==0x9d8f);CHECK(fx->sreg==13);CHECK(fx->dreg==0);range_prefix_seen++;
}
static void Observe(SuperFx *fx,uint32_t pc,void *unused) { (void)unused;outcome=pc==0x9da7?1:pc==0x9de0?2:3;superfx_hook_redirect(fx,0xf800); }
static unsigned Magnitude(unsigned d) { d&=65535u;return (int16_t)d<0 ? d^65535u : d; }
static void Case(SuperFx *fx,uint8_t *ram,unsigned damage,unsigned x,unsigned y,unsigned tx,unsigned ty) {
 superfx_reset(fx);memset(ram,0xa5,65536);outcome=0;
 unsigned radius=40+(damage>>8),expected=Magnitude(x-tx)<radius && Magnitude(y-ty)<radius?1:2;
 superfx_set_reg(fx,4,damage);superfx_set_reg(fx,3,x);superfx_set_reg(fx,5,y);
 superfx_set_reg(fx,7,tx);superfx_set_reg(fx,8,ty);superfx_set_reg(fx,13,0x5b00);
 superfx_cpu_write_io(fx,0x301e,0x8e);superfx_cpu_write_io(fx,0x301f,0x9d);
 superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
 if(outcome!=expected){fprintf(stderr,"damage=%04x x/y=%04x/%04x target=%04x/%04x outcome=%u expected=%u\n",damage,x,y,tx,ty,outcome,expected);exit(1);}
 for(unsigned a=0;a<65536;a++)CHECK(ram[a]==0xa5);
}
int main(int argc,char **argv) {
 CHECK(argc==2);uint8_t *rom=malloc(0x200000),*ram=malloc(65536);CHECK(rom&&ram);
 FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);fclose(f);
 const uint8_t code[]={0xb4,0xc0,0xa1,0x28,0x21,0x50,0xb3,0x67,0x0a,0x02,0x01,0x4f,0x61,0x06,0x43,0xb5,0x68,0x0a,0x02,0x01,0x4f,0x61,0x06,0x3a};
 CHECK(!memcmp(rom+0x1d8e,code,sizeof(code)));rom[0x7800]=0;rom[0x7801]=1;
 SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
 CHECK(superfx_set_pc_hook(fx,0x9da7,Observe,NULL));CHECK(superfx_set_pc_hook(fx,0x9de0,Observe,NULL));unsigned cases=0;
 const unsigned high[]={0,1,63,127,255};
 for(unsigned h=0;!getenv("RADIUS_CONNECTED_ONLY") && h<5;h++)for(unsigned axis=0;axis<2;axis++)for(unsigned d=0;d<65536;d++) {
  superfx_reset(fx);memset(ram,0xa5,65536);outcome=0;
  unsigned delta=(int16_t)d<0 ? (d^65535u) : d;
  unsigned expected=delta<40+high[h]?1:2;
  superfx_set_reg(fx,4,high[h]<<8);superfx_set_reg(fx,3,axis?0:d);superfx_set_reg(fx,5,axis?d:0);
  superfx_set_reg(fx,7,0);superfx_set_reg(fx,8,0);superfx_set_reg(fx,13,0x5b00);
  superfx_cpu_write_io(fx,0x301e,0x8e);superfx_cpu_write_io(fx,0x301f,0x9d);
  superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  if(outcome!=expected){fprintf(stderr,"high=%u axis=%u delta=%04x outcome=%u expected=%u\n",high[h],axis,d,outcome,expected);return 1;}
  for(unsigned a=0;a<65536;a++)CHECK(ram[a]==0xa5);cases++;
 }
 /* Joint axis boundaries for all routed range bytes, plus direct byte 127.
    Translated origins
    cross coordinate wrap and low damage bytes exercise HIB isolation. */
 const unsigned origins[]={0,0x7fff,0x8000,0xffff};
 const unsigned lows[]={0,1,127,255};
 for(unsigned index=0;!getenv("RADIUS_CONNECTED_ONLY") && index<129;index++) {
  unsigned h=index==128?255:index;
  int radius=40+(int)h;
  int deltas[]={0,1,-1,radius-1,radius,radius+1,-radius+1,-radius,-radius-1,-radius-2,32767,-32768};
  for(unsigned xi=0;xi<12;xi++)for(unsigned yi=0;yi<12;yi++)for(unsigned o=0;o<4;o++) {
   unsigned tx=origins[o],ty=origins[3-o];
   Case(fx,ram,(h<<8)|lows[o],(tx+deltas[xi])&65535u,(ty+deltas[yi])&65535u,tx,ty);cases++;
  }
 }
 printf("Radius range: %u source-oracle cases passed\n",cases);
 const uint8_t route[]={0x3d,0xa0,0xbf,0x3e,0x66,0x14,0x40,0xb4,0xc0,0xd0,0x4d,0x0a,0x0c,0xbd,0x3e,0x55,0x3d,0x40,0xe0,0x0b,0x55,0x01,0x05,0x18};
 CHECK(!memcmp(rom+0x1d76,route,sizeof(route)));CHECK(superfx_set_pc_hook(fx,0x9d8f,Observe,NULL));cases=0;
 const unsigned types[]={0,1,2,63,127,128,129,255};
 for(unsigned damage=0;!getenv("RADIUS_CONNECTED_ONLY") && damage<65536;damage++)for(unsigned t=0;t<8;t++) {
  superfx_reset(fx);memset(ram,0xa5,65536);outcome=0;
  ram[0x17e]=0x86;ram[0x17f]=0x2b;ram[0x2b80]=(uint8_t)damage;ram[0x2b81]=(uint8_t)(damage>>8);ram[0x5b05]=(uint8_t)types[t];
  superfx_set_reg(fx,13,0x5b00);superfx_cpu_write_io(fx,0x301e,0x76);superfx_cpu_write_io(fx,0x301f,0x9d);
  superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
  unsigned bfg=((damage>>8)+1)&128u;
  unsigned expected=bfg ? (types[t]!=0?1:2):3;
  if(outcome!=expected){fprintf(stderr,"route damage=%04x type=%u outcome=%u expected=%u\n",damage,types[t],outcome,expected);return 1;}
  for(unsigned a=0;a<65536;a++) {
   unsigned value=a==0x17e?0x86:a==0x17f?0x2b:a==0x2b80?(damage&255):a==0x2b81?(damage>>8):a==0x5b05?types[t]:0xa5;
   CHECK(ram[a]==value);
  }
  cases++;
 }
 printf("Radius damage routing: %u source-oracle cases passed\n",cases);
 superfx_clear_pc_hooks(fx);CHECK(superfx_set_pc_hook(fx,0x9da7,Observe,NULL));CHECK(superfx_set_pc_hook(fx,0x9de0,Observe,NULL));CHECK(superfx_set_pc_hook(fx,0x9d8f,RangePrefix,NULL));cases=0;
 uint8_t *expected_ram=malloc(65536);CHECK(expected_ram);
 const unsigned target_positions[]={0,1,31,63,99};
 for(unsigned position=0;position<5;position++)for(unsigned high_byte=0;high_byte<256;high_byte++) {
  unsigned target=0x5a8e + target_positions[position]*38;
  /* BPL +1 skips FROM R4; its FROM R13 delay slot survives into HIB.
     The connected retail path therefore uses the target pointer high byte.
     This follows source instruction semantics, including the delay slot,
     corroborated by ares GSU Branch/FROM/HIB and pipeline implementations. */
  int radius=40+(int)(target>>8);
  int delta[]={0,1,-1,radius-1,radius,radius+1,-radius+1,-radius,-radius-1,-radius-2,32767,-32768};
  for(unsigned xi=0;xi<12;xi++)for(unsigned yi=0;yi<12;yi++)for(unsigned type=0;type<2;type++) {
   unsigned damage=(high_byte<<8)|((xi*19+yi*7)&255),tx=0xffff,ty=0x8000;
   unsigned x=(tx+delta[xi])&65535u,y=(ty+delta[yi])&65535u;
   superfx_reset(fx);memset(ram,0xa5,65536);outcome=0;range_prefix_seen=0;
   ram[0x17e]=0x86;ram[0x17f]=0x2b;ram[0x2b80]=(uint8_t)damage;ram[0x2b81]=(uint8_t)high_byte;ram[target+5]=(uint8_t)type;memcpy(expected_ram,ram,65536);
   superfx_set_reg(fx,13,target);superfx_set_reg(fx,3,x);superfx_set_reg(fx,5,y);superfx_set_reg(fx,7,tx);superfx_set_reg(fx,8,ty);
   superfx_cpu_write_io(fx,0x301e,0x76);superfx_cpu_write_io(fx,0x301f,0x9d);superfx_sync(fx,fx->master_clock+10000);CHECK(!superfx_is_running(fx));
   unsigned bfg=(high_byte+1)&128u;
   unsigned expected=bfg?(type?1:2):(Magnitude(x-tx)<(unsigned)radius && Magnitude(y-ty)<(unsigned)radius?1:2);
   CHECK(range_prefix_seen==(bfg?0u:1u));if(!bfg)CHECK(superfx_reg(fx,1)==(unsigned)radius);
   if(outcome!=expected){fprintf(stderr,"connected high=%u x=%d y=%d type=%u outcome=%u expected=%u r1=%04x r4=%04x\n",high_byte,delta[xi],delta[yi],type,outcome,expected,superfx_reg(fx,1),superfx_reg(fx,4));return 1;}
   CHECK(!memcmp(ram,expected_ram,65536));cases++;
  }
 }
 printf("Connected radius routing/range: %u source-oracle cases passed\n",cases);free(expected_ram);superfx_destroy(fx);free(rom);free(ram);return 0;
}



