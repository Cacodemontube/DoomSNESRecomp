#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static unsigned cases;
static void Put(uint8_t *data,unsigned address,uint16_t value) {
    data[address]=(uint8_t)value;data[address+1]=(uint8_t)(value>>8);
}
static unsigned Object(unsigned base,unsigned index) { return base+index*38; }
static void List(uint8_t *data,unsigned sector,unsigned base,unsigned length) {
    Put(data,0x308c+sector*14,(uint16_t)(length ? base : 0));
    for(unsigned i=0;i<length;i++) {
        unsigned object=Object(base,i);
        Put(data,object,(uint16_t)(i+1<length ? Object(base,i+1) : 0));
        Put(data,object+2,(uint16_t)(i ? Object(base,i-1) : 0));
        data[object+22]=(uint8_t)sector;
    }
}
static void Run(SuperFx *fx,unsigned pc,unsigned object) {
    superfx_set_reg(fx,12,(uint16_t)object);superfx_set_reg(fx,11,0xf800);
    superfx_cpu_write_io(fx,0x301e,(uint8_t)pc);superfx_cpu_write_io(fx,0x301f,(uint8_t)(pc>>8));
    superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(superfx_reg(fx,12)==object);
}
static void Case(SuperFx *fx,uint8_t *expected,unsigned sector,unsigned length,unsigned position,unsigned destination_length,int same_sector) {
    superfx_reset(fx);memset(expected,0xa5,65536);
    unsigned destination=same_sector ? sector : sector^0x80;
    List(expected,sector,0x5b00,length);
    if(!same_sector)List(expected,destination,0x6000,destination_length);
    memcpy(fx->ram,expected,65536);
    unsigned object=Object(0x5b00,position);
    unsigned previous=position ? Object(0x5b00,position-1) : 0;
    unsigned next=position+1<length ? Object(0x5b00,position+1) : 0;
    Put(expected,previous ? previous : 0x308c+sector*14,(uint16_t)next);
    if(next)Put(expected,next+2,(uint16_t)previous);
    Run(fx,0x8d7f,object);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"sector remove: sector=%u length=%u position=%u\n",sector,length,position);exit(1);
    }
    /* Sector selection itself is audited separately. Supply that result to
     * exercise the real insertion routine after a completed removal. */
    expected[object+22]=(uint8_t)destination;fx->ram[object+22]=(uint8_t)destination;
    unsigned head=same_sector ? (position ? 0x5b00 : next) : (destination_length ? 0x6000 : 0);
    Put(expected,0x308c+destination*14,(uint16_t)object);
    Put(expected,object,(uint16_t)head);Put(expected,object+2,0);
    if(head)Put(expected,head+2,(uint16_t)object);
    Run(fx,0x8d3d,object);
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"sector insert: sector=%u destination=%u length=%u position=%u destination_length=%u\n",
            sector,destination,length,position,destination_length);exit(1);
    }
    cases++;
}
static unsigned pickup_cases,destruction_cases;
static void PickupRemove(SuperFx *fx,uint8_t *expected,unsigned sector,unsigned length,unsigned position,uint16_t flags_type,int destroy) {
    superfx_reset(fx);memset(expected,0xa5,65536);List(expected,sector,0x5b00,length);
    unsigned object=Object(0x5b00,position);
    Put(expected,object+4,flags_type);memcpy(fx->ram,expected,65536);
    unsigned previous=position ? Object(0x5b00,position-1) : 0;
    unsigned next=position+1<length ? Object(0x5b00,position+1) : 0;
    Put(expected,previous ? previous : 0x308c+sector*14,(uint16_t)next);
    if(next)Put(expected,next+2,(uint16_t)previous);
    if(destroy)Put(expected,object+4,(uint16_t)(flags_type&~0x00a8u));
    /* _RM7XY8800 supplies SectorData.Objects directly. The fixed object's
     * sector-byte field is not consulted by this original unlink entry. */
    if(destroy) {
        superfx_set_reg(fx,1,(uint16_t)(0x3080+sector*14));
        superfx_set_reg(fx,12,0x6000);superfx_set_reg(fx,13,(uint16_t)object);superfx_set_reg(fx,11,0xf800);
        superfx_cpu_write_io(fx,0x301e,0x6e);superfx_cpu_write_io(fx,0x301f,0x8d);
        superfx_sync(fx,fx->master_clock+20000);CHECK(!superfx_is_running(fx));CHECK(superfx_reg(fx,12)==object);
    } else {
        superfx_set_reg(fx,1,(uint16_t)(0x308c+sector*14));Run(fx,0x8d8b,object);
    }
    if(memcmp(fx->ram,expected,65536)) {
        fprintf(stderr,"pickup unlink: sector=%u length=%u position=%u flags/type=%04x\n",sector,length,position,flags_type);exit(1);
    }
    if(destroy)destruction_cases++;else pickup_cases++;
}
int main(int argc,char **argv) {
    CHECK(argc==2);uint8_t *rom=calloc(0x200000,1),*ram=calloc(65536,1),*expected=malloc(65536);CHECK(rom && ram && expected);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(rom,1,0x200000,f)==0x200000);CHECK(fgetc(f)==EOF);fclose(f);
    const uint8_t add[]={0xa0,0x16,0x5c,0x3d,0x40,0x3f,0x8e,0xf1,0x8c,0x30,0x51,0x40,0xbc,0x90,
        0x2c,0x11,0x31,0x20,0xb0,0x09,0x04,0xd0,0xd0,0xbc,0x30,0xd1,0xd1,0x60,0x9b,0x31};
    const uint8_t remove[]={0xa0,0x16,0x5c,0x3d,0x40,0x3f,0x8e,0xf1,0x8c,0x30,0x21,0x50,0x2c,0x12,0x12,0x42};
    CHECK(!memcmp(rom+0xd3d,add,sizeof(add)));CHECK(!memcmp(rom+0xd7f,remove,sizeof(remove)));
    rom[0x7800]=0;rom[0x7801]=1;
    SuperFx *fx=superfx_create(rom,0x200000,ram,65536);CHECK(fx);
    for(unsigned sector=0;sector<256;sector++)for(unsigned length=1;length<=8;length++)
        for(unsigned position=0;position<length;position++)for(unsigned target=0;target<=4;target++)
            Case(fx,expected,sector,length,position,target,0);
    for(unsigned sector=0;sector<256;sector++)for(unsigned length=1;length<=8;length++)
        for(unsigned position=0;position<length;position++)Case(fx,expected,sector,length,position,0,1);
    printf("Retail sector unlink/relink: %u source-oracle migrations passed\n",cases);
    const uint8_t pickup_entry[]={0x2c,0x12,0x12,0x42,0x02,0x41,0x3f,0x6c,0x20,0x08,0xfa,0x11};
    CHECK(!memcmp(rom+0xd8b,pickup_entry,sizeof(pickup_entry)));
    for(unsigned flags_type=0;flags_type<65536;flags_type++)PickupRemove(fx,expected,17,4,1,(uint16_t)flags_type,0);
    const uint16_t flags[]={0,0x0610,0x0650,0x0680,0x06a8,0x06ff,0x8000,0xffff};
    for(unsigned sector=0;sector<256;sector++)for(unsigned length=1;length<=8;length++)
        for(unsigned position=0;position<length;position++)for(unsigned f=0;f<8;f++)
            PickupRemove(fx,expected,sector,length,position,flags[f],0);
    printf("Retail fixed-item pickup unlink with preserved flags/type: %u source-oracle cases passed\n",pickup_cases);
    const uint8_t destruction_entry[]={0x21,0x3e,0x5c,0xbd,0x3e,0x54,0x40,0xf2,0xa8,0x00,0x3d,0x72,0x90,0x2d,0x05,0x0d,0x1c};
    CHECK(!memcmp(rom+0xd6e,destruction_entry,sizeof(destruction_entry)));
    for(unsigned flags_type=0;flags_type<65536;flags_type++)PickupRemove(fx,expected,17,4,1,(uint16_t)flags_type,1);
    for(unsigned sector=0;sector<256;sector++)for(unsigned length=1;length<=8;length++)
        for(unsigned position=0;position<length;position++)for(unsigned f=0;f<8;f++)
            PickupRemove(fx,expected,sector,length,position,flags[f],1);
    printf("Retail fixed-object destruction flags/unlink: %u source-oracle cases passed\n",destruction_cases);
    superfx_destroy(fx);free(rom);free(ram);free(expected);return 0;
}
