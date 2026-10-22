#include "doom_rotations.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace fs = std::filesystem;
struct Frame { const char *prefix; char frame; unsigned image; };
const Frame frames[] = {
#include "doom_rotation_frames.inc"
};
struct Patch { DoomRotationPatch view{}; std::vector<uint16_t> pixels; std::string lump; };
using Key = std::pair<unsigned,unsigned>;
std::map<Key,Patch> patches;
std::array<uint8_t,768> palette{};
std::array<uint16_t,256> previous_colors{};
std::array<uint8_t,256> native_colors{};
bool colors_valid;
unsigned word(const uint8_t *p) { return p[0] | (p[1]<<8); }
uint32_t dword(const uint8_t *p) { return word(p) | (uint32_t(word(p+2))<<16); }
void require(bool ok,const char *why) { if(!ok)throw std::runtime_error(why); }
Patch decode(const std::vector<uint8_t>& bytes) {
    require(bytes.size()>=8,"Truncated sprite header");
    Patch p;
    p.view.width=word(bytes.data());p.view.height=word(bytes.data()+2);
    p.view.left=int16_t(word(bytes.data()+4));p.view.top=int16_t(word(bytes.data()+6));
    require(p.view.width && p.view.width<=1024 && p.view.height && p.view.height<=256,
            "Unsupported sprite dimensions");
    require(bytes.size()>=8+p.view.width*4,"Truncated sprite column table");
    p.pixels.resize(p.view.width*p.view.height);
    for(unsigned x=0;x<p.view.width;x++) {
        size_t pos=dword(bytes.data()+8+x*4);
        unsigned visits=0;
        for(;;) {
            require(pos<bytes.size() && visits++<=256,"Invalid sprite column");
            unsigned top=bytes[pos++];if(top==255)break;
            require(pos+2<=bytes.size(),"Truncated sprite post");
            unsigned count=bytes[pos++];pos++;
            require(top+count<=p.view.height && pos+count+1<=bytes.size(),"Invalid sprite post");
            for(unsigned y=0;y<count;y++)p.pixels[x*p.view.height+top+y]=unsigned(bytes[pos+y])+1;
            pos+=count+1;
        }
    }
    return p;
}
void write(const fs::path& path,const std::vector<uint8_t>& data) {
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),data.size());
    require(bool(out),"Cannot export rotation sprites; use a writable build folder");
}
}
extern "C" {
bool DoomRotationsEnabled(void) { return !patches.empty(); }
static bool native_overrides[232];
bool DoomRotationsNativeOverride(unsigned image) {return image<232 && native_overrides[image];}
void DoomRotationsNativeOverrides(const uint8_t *original,const uint8_t *modified) {
    if(!original || !modified){memset(native_overrides,0,sizeof(native_overrides));return;}
    for(unsigned i=0;i<232;i++) {
        unsigned h=0x1a81d0+original[0x1a8000+i*2]+(original[0x1a8001+i*2]<<8);
        native_overrides[i]=memcmp(original+h+2,modified+h+2,original[h+1]*3)!=0;
    }
}
void DoomRotationsClear(void) { patches.clear();colors_valid=false; }
bool DoomRotationsImport(const char *wad,const char *directory,char *error,size_t cap) {
    DoomRotationsClear();
    if(error && cap)error[0]=0;
    fs::path staging;
    try {
        std::ifstream file(fs::u8path(wad),std::ios::binary|std::ios::ate);
        require(bool(file),"Cannot open DOOM.WAD");
        auto length=file.tellg();require(length>=12 && length<=64*1024*1024,"Invalid IWAD size");
        std::vector<uint8_t> data(static_cast<size_t>(length));file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()),data.size());
        require(bool(file) && !memcmp(data.data(),"IWAD",4),"Select an original Doom or Ultimate Doom IWAD");
        unsigned count=dword(data.data()+4),offset=dword(data.data()+8);
        require(count<=65536 && offset<=data.size() && count<=(data.size()-offset)/16,"Invalid WAD directory");
        std::map<std::string,std::vector<uint8_t>> lumps;
        for(unsigned i=0;i<count;i++) {
            const uint8_t *entry=data.data()+offset+i*16;
            unsigned start=dword(entry),size=dword(entry+4);
            require(start<=data.size() && size<=data.size()-start,"Invalid WAD lump bounds");
            char name[9]{};memcpy(name,entry+8,8);
            lumps[std::string(name)]={data.begin()+start,data.begin()+start+size};
        }
        require(lumps.count("E1M1") && lumps.count("E2M1") && lumps.count("E3M1") && !lumps.count("MAP01"),
                "Use full Doom or Ultimate Doom; shareware and Doom II are not supported");
        require(lumps["PLAYPAL"].size()>=768,"Missing Doom PLAYPAL");
        std::map<Key,Patch> loaded;
        std::map<std::string,std::vector<uint8_t>> exported;
        for(const auto& lump:lumps) {
            const auto& name=lump.first;
            if(name.size()!=6 && name.size()!=8)continue;
            if(name[4]<'A' || name[4]>'Z' || name[5]<'0' || name[5]>'8' ||
               (name.size()==8 && (name[6]<'A' || name[6]>'Z' || name[7]<'0' || name[7]>'8')))continue;
            for(const auto& f:frames) {
                if(name.compare(0,4,f.prefix))continue;
                for(unsigned part=4;part<name.size();part+=2) {
                    if(name[part]!=f.frame || name[part+1]<'0' || name[part+1]>'8')continue;
                    Patch p=decode(lump.second);p.view.flip=part==6;p.lump=name;
                    if(p.view.flip)p.view.left=int(p.view.width)-p.view.left;
                    unsigned rotation=unsigned(name[part+1]-'0');
                    if(rotation)loaded[{f.image,rotation-1}]=std::move(p);
                    else for(unsigned r=0;r<8;r++)loaded[{f.image,r}]=p;
                    exported[name]=lump.second;
                }
            }
        }
        for(const auto& f:frames)for(unsigned r=0;r<8;r++)
            require(loaded.count({f.image,r})!=0,"IWAD is missing required enemy/projectile rotations");
        /* Publish a WAD-specific cache with a rename. Existing exports are
         * verified and reused; no previous cache or player files are deleted. */
        uint64_t fingerprint=14695981039346656037ull;
        for(uint8_t b:data)fingerprint=(fingerprint^b)*1099511628211ull;
        char cache_name[32];std::snprintf(cache_name,sizeof(cache_name),"%016llx",(unsigned long long)fingerprint);
        /* Mapping revision 2 adds the missing Spiderdemon E directions.
         * Re-export automatically without overwriting earlier caches. */
        fs::path destination=fs::u8path(directory)/(std::string("v2-")+cache_name);
        exported["PLAYPAL"]={lumps["PLAYPAL"].begin(),lumps["PLAYPAL"].begin()+768};
        if(fs::exists(destination)) {
            for(const auto& lump:exported) {
                std::ifstream cached(destination/(lump.first+".lmp"),std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(cached)),{});
                require(bytes==lump.second,"Rotation cache is incomplete or modified; rename its folder and retry");
            }
        } else {
        staging=destination;staging+=".importing";
        require(!fs::exists(staging),"An unfinished rotation import exists; rename the .importing folder and retry");
        fs::create_directories(staging);
        for(const auto& lump:exported)write(staging/(lump.first+".lmp"),lump.second);
        fs::rename(staging,destination);
        staging.clear();
        }
        /* The renderer consumes the exported cache, through immutable decoded
         * patches. No file IO occurs during simulation or raster work. */
        std::map<std::string,std::vector<uint8_t>> cached;
        for(const auto& lump:exported) {
            std::ifstream in(destination/(lump.first+".lmp"),std::ios::binary);
            auto& bytes=cached[lump.first];bytes.assign(std::istreambuf_iterator<char>(in),{});
            require(bytes==lump.second,"Cannot read exported rotation cache");
        }
        for(auto& entry:loaded) {
            bool flip=entry.second.view.flip;
            std::string name=entry.second.lump;
            entry.second=decode(cached.at(name));
            entry.second.view.flip=flip;entry.second.lump=name;
            if(flip)entry.second.view.left=int(entry.second.view.width)-entry.second.view.left;
        }
        std::copy_n(cached.at("PLAYPAL").begin(),768,palette.begin());
        patches=std::move(loaded);
        for(auto& entry:patches)entry.second.view.pixels=entry.second.pixels.data();
        return true;
    } catch(const std::exception& e) {
        if(error && cap)std::snprintf(error,cap,"%s",e.what());
        return false;
    }
}
const DoomRotationPatch *DoomRotationsPatch(unsigned image,unsigned rotation) {
    auto it=patches.find({image,rotation&7});return it==patches.end()?nullptr:&it->second.view;
}
unsigned DoomRotationsDirection(int dx,int dy,unsigned facing) {
    /* rle.i/rloAngle: full 16-bit turn, +X=0; rlvobjs.a uses the same
     * world coordinate system. PC rotation 1 faces the viewer. */
    double angle=std::atan2(double(dy),double(dx))*65536.0/(2*3.14159265358979323846);
    unsigned delta=(unsigned(std::lround(angle))-facing+4096)&65535;
    return delta>>13;
}
void DoomRotationsPalette(const uint16_t *cgram) {
    if(!DoomRotationsEnabled() || (colors_valid && !memcmp(cgram,previous_colors.data(),512)))return;
    memcpy(previous_colors.data(),cgram,512);colors_valid=true;
    for(unsigned i=0;i<256;i++) {
        unsigned best=~0u,chosen=1;
        for(unsigned j=1;j<256;j++) {
            int r=int(palette[i*3])-int((cgram[j]&31)*255/31);
            int g=int(palette[i*3+1])-int(((cgram[j]>>5)&31)*255/31);
            int b=int(palette[i*3+2])-int(((cgram[j]>>10)&31)*255/31);
            unsigned distance=r*r+g*g+b*b;
            if(distance<best) {best=distance;chosen=j;}
        }
        native_colors[i]=uint8_t(chosen);
    }
}
unsigned DoomRotationsPixel(unsigned pixel) { return pixel?native_colors[(pixel-1)&255]:0; }
}
