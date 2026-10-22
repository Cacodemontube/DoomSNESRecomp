#include "doom_rotations.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
int main(int argc,char **argv) {
    CHECK(argc==3);
    {
        std::vector<uint8_t> original(0x200000),modified;
        for(unsigned i=0;i<232;i++){
            unsigned offset=i*8;original[0x1a8000+i*2]=(uint8_t)offset;
            original[0x1a8001+i*2]=(uint8_t)(offset>>8);original[0x1a81d0+offset+1]=1;
        }
        modified=original;modified[0x1a81d0+26*8+2]=1;
        DoomRotationsNativeOverrides(original.data(),modified.data());
        CHECK(DoomRotationsNativeOverride(26));CHECK(!DoomRotationsNativeOverride(27));
        DoomRotationsNativeOverrides(nullptr,nullptr);CHECK(!DoomRotationsNativeOverride(26));
    }
    CHECK(DoomRotationsDirection(10,0,0)==0);
    CHECK(DoomRotationsDirection(0,10,0)==2);
    CHECK(DoomRotationsDirection(-10,0,0)==4);
    CHECK(DoomRotationsDirection(0,-10,0)==6);
    CHECK(DoomRotationsDirection(0,10,16384)==0);
    char error[512];
    CHECK(DoomRotationsImport(argv[1],argv[2],error,sizeof(error)) || (std::fprintf(stderr,"%s\n",error),false));
    CHECK(DoomRotationsEnabled());
    unsigned images[]={26,39,51,65,77,87,95,108,121,218,219,223};
    for(unsigned image:images)for(unsigned direction=0;direction<8;direction++) {
        const DoomRotationPatch *p=DoomRotationsPatch(image,direction);
        CHECK(p && p->pixels && p->width && p->height);
    }
    const auto *left=DoomRotationsPatch(26,1),*right=DoomRotationsPatch(26,7);
    CHECK(left->width==right->width && left->height==right->height);
    CHECK(left->flip!=right->flip);
    // The native walk pairs need opposite leg poses at the front and back.
    // Pinky/Baron store separate PC artwork; Spider pairs share one lump.
    // Do not apply an additional global flip to any of them.
    const unsigned mirror_pairs[][2]={{66,68},{95,97},{121,122}};
    for(const auto& pair:mirror_pairs)for(unsigned direction:{0u,4u}) {
        const auto *a=DoomRotationsPatch(pair[0],direction),*b=DoomRotationsPatch(pair[1],direction);
        CHECK(a && b && a->width==b->width && a->height==b->height);
        unsigned differences=0;
        for(unsigned x=0;x<a->width;x++)for(unsigned y=0;y<a->height;y++) {
            unsigned ax=a->flip?a->width-1-x:x,bx=b->flip?b->width-1-x:x;
            differences+=a->pixels[ax*a->height+y]!=b->pixels[bx*b->height+y];
        }
        CHECK(differences>0);
        if(pair[0]==121) {
            CHECK(a->flip!=b->flip && a->left+b->left==int(a->width));
            for(unsigned i=0;i<a->width*a->height;i++)CHECK(a->pixels[i]==b->pixels[i]);
        }
        std::printf("Walk-pose pair %u/%u, direction %u: distinct rendered poses (%u different pixels)\n",
            pair[0],pair[1],direction,differences);
    }
    // Side-on Spiderdemon steps E and B are distinct frames, not a
    // horizontal reflection of one camera-facing image.
    const auto *bside=DoomRotationsPatch(121,1),*eside=DoomRotationsPatch(122,1);
    bool different=bside->width!=eside->width || bside->height!=eside->height;
    if(!different)for(unsigned i=0;i<bside->width*bside->height;i++)different|=bside->pixels[i]!=eside->pixels[i];
    CHECK(different);
    CHECK(!DoomRotationsPatch(33,1)); // death imagery stays native
    for(unsigned image=208;image<=231;image++)
        CHECK((DoomRotationsPatch(image,0)!=nullptr)==(image==218 || image==219 || image==223));
    uint16_t cgram[256]{};
    for(unsigned i=0;i<256;i++)cgram[i]=i*127;
    DoomRotationsPalette(cgram);
    CHECK(DoomRotationsPixel(0)==0);
    CHECK(DoomRotationsPixel(1)!=0); // opaque palette black is not transparency
    bool exported=false;
    for(const auto& f:std::filesystem::recursive_directory_iterator(argv[2]))
        exported|=f.path().filename()=="PLAYPAL.lmp";
    CHECK(exported);
    DoomRotationsClear();CHECK(!DoomRotationsEnabled());
    auto bad=std::filesystem::path(argv[2]).parent_path()/"invalid.wad";
    { std::ofstream out(bad,std::ios::binary);out<<"IWAD\xff\xff\xff\xff\xff\xff\xff\xff"; }
    CHECK(!DoomRotationsImport(bad.string().c_str(),argv[2],error,sizeof(error)));
    CHECK(error[0] && !DoomRotationsEnabled());
    std::puts("Rotation import, all actors/projectiles, mirrored walk pairs (Pinky/Baron/Spiderdemon), side steps and rejection passed.");
}
