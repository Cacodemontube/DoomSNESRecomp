/* Compare packed-bit transposition with the original hardware flush loop,
 * including clock/buffer effects and partial-plane RAM read/modify/write. */
#include "../snesrecomp/runner/src/snes/superfx.c"
#include "fixtures/superfx_flush_reference.inc"
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(int argc,char **argv) {
    uint8_t *rom=malloc(65536), *seed=malloc(65536), *a=malloc(65536), *b=malloc(65536);
    CHECK(rom && seed && a && b);
    uint32_t random=0x12345678;
    for(unsigned i=0;i<65536;i++) {
        random=random*1664525u+1013904223u;rom[i]=(uint8_t)(random>>24);
        random=random*1664525u+1013904223u;seed[i]=(uint8_t)(random>>24);
    }
    SuperFx *base=superfx_create(rom,65536,seed,65536);CHECK(base);
    unsigned checked=0;
    for(unsigned mode=0;mode<4;mode++)
    for(unsigned height=0;height<4;height++)
    for(unsigned speed=0;speed<2;speed++)
    for(unsigned pending=0;pending<256;pending++)
    for(unsigned sample=0;sample<8;sample++) {
        SuperFx actual;memcpy(&actual,base,sizeof(actual));actual.ram=a;
        actual.scmr=mode|((height&1)<<2)|((height&2)<<4);
        actual.clsr=speed;actual.scbr=(uint8_t)sample*31;
        actual.por=sample&1 ? 16 : 0;actual.sfr=SFR_G|SFR_R;
        actual.ramcl=sample&2 ? 3 : 0;actual.romcl=sample&4 ? 5 : 0;
        for(unsigned i=0;i<8;i++) {
            random=random*1664525u+1013904223u;
            actual.pixel[0].data[i]=(uint8_t)(random>>24);
        }
        actual.pixel[0].offset=(uint16_t)(random>>8);actual.pixel[0].bitpend=(uint8_t)pending;
        actual.ramar=(uint16_t)pixel_address(&actual,(uint8_t)(actual.pixel[0].offset<<3),
            (uint8_t)(actual.pixel[0].offset>>5));
        actual.ramdr=(uint8_t)random;
        SuperFx expected;memcpy(&expected,&actual,sizeof(expected));expected.ram=b;
        memcpy(a,seed,65536);memcpy(b,seed,65536);
        flush_pixel(&actual,&actual.pixel[0]);flush_pixel_reference(&expected,&expected.pixel[0]);
        expected.ram=a;
        CHECK(memcmp(&actual,&expected,sizeof(actual))==0);
        CHECK(memcmp(a,b,65536)==0);
        checked++;
    }
    printf("Super FX pixel flush: %u mode/mask/buffer cases match full state and RAM\n",checked);
    if(argc==2 && !strcmp(argv[1],"--benchmark")) {
        base->scmr=0x1f;base->clsr=1;
        for(unsigned i=0;i<8;i++)base->pixel[0].data[i]=(uint8_t)(i*37+19);
        for(unsigned pair=0;pair<6;pair++) {
            double ms[2];
            for(unsigned order=0;order<2;order++) {
                unsigned variant=order^(pair&1);
                void (*volatile run)(SuperFx *,SuperFxPixelCache *) = variant ? flush_pixel : flush_pixel_reference;
                clock_t start=clock();
                for(unsigned i=0;i<2000000;i++) {
                    base->pixel[0].bitpend=255;run(base,&base->pixel[0]);
                }
                ms[variant]=1000.0*(clock()-start)/CLOCKS_PER_SEC;
            }
            printf("flush pair %u: original=%.3f ms packed=%.3f ms (2 million 8bpp flushes)\n",pair+1,ms[0],ms[1]);
        }
    }
    superfx_destroy(base);free(rom);free(seed);free(a);free(b);
    return 0;
}
