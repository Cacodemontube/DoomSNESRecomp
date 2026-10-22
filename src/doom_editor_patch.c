#include "doom_editor_patch.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Range { unsigned start,size; } Range;
static unsigned AssetBlobSize(const uint8_t *b,size_t size,unsigned p);
static int GraphicsBlobValid(const uint8_t *b,size_t size,unsigned p);
static const Range ranges[]={
#include "doom_editor_ranges.inc"
    {0x1f3314,0x252}, /* geometry pointer/count tables, excludes BSP far pointers */
    {0x1f35d2,0x1b0}, /* sector counts and B pointers, excludes sector far pointers */
};
static uint32_t Word32(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned Word16(const uint8_t *p){return p[0]|p[1]<<8;}
static unsigned FarAddress(const uint8_t *p){return (p[0]|p[1]<<8|p[2]<<16)&0x3fffff;}
static int MapStorage(unsigned start,unsigned size){for(unsigned i=0;i+2<sizeof(ranges)/sizeof(ranges[0]);i++)if(start>=ranges[i].start&&start<ranges[i].start+ranges[i].size&&size<=ranges[i].start+ranges[i].size-start)return 1;return 0;}
static int AudioField(unsigned start,unsigned size){
    static const Range fields[]={{0xed652,0x1544},{0xeeb96,96},{0x1f3782,54},{0xefdd7,3},{0xefdec,3},{0xefe01,3},{0xefe0f,3},{0xefe14,3},{0xefe1c,3},{0xefd71,2},{0xefd74,1},{0x1fef6b,2},{0x1f5811,2}};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)if(start>=fields[i].start&&start<fields[i].start+fields[i].size&&size<=fields[i].start+fields[i].size-start)return 1;return 0;
}
static int CpuBankValid(unsigned bank,int expanded){return (bank>=0x40&&bank<=0x5f)||(expanded&&((bank>=0x60&&bank<=0x7d)||(bank>=0xe0&&bank<=0xfd)));}
static int DataStorage(unsigned start,unsigned size,int expanded){return MapStorage(start,size)||(expanded&&start>=0x200000&&start<0x3e0000&&size<=0x3e0000-start);}
int DoomEditorRomCompatible(const uint8_t *rom,size_t size){
    static const unsigned offsets[]={0xefd80,0x1fa000},lengths[]={48,64};
    static const char *hashes[]={"43a8e09cbf2726e2e3e44ff952a0cb8b61f598f6e59fa882fc8c5f0672c5dca7","8bbd982d199a09e9d405cbcb92b9c016317d483875bbd85992598166f4b06b74"};
    if(!rom||(size!=0x200000&&size!=0x400000))return 0;
    for(unsigned i=0;i<2;i++){uint8_t digest[32];char hex[65];sha256_compute(rom+offsets[i],lengths[i],digest);for(unsigned n=0;n<32;n++)snprintf(hex+n*2,3,"%02x",digest[n]);if(strcmp(hex,hashes[i]))return 0;}return 1;
}
size_t DoomEditorPatchTargetSize(const uint8_t *patch,size_t size){return patch&&size>=76&&(!memcmp(patch,"DSEMOD5\0",8)||!memcmp(patch,"DSEMOD6\0",8))?0x400000:0x200000;}
static int AudioSceneSongsValid(const uint8_t *b){
    const unsigned fields[]={0x1fef6b,0x1f5811};
    for(unsigned i=0;i<2;i++){unsigned p=fields[i];if(b[p-1]!=0xa9||b[p+2]!=0x22||b[p+3]!=0xb4||b[p+4]!=0xfe||b[p+5]!=0x4e||b[p]>=19||b[p+1]>=48)return 0;}
    return 1;
}
static int AudioTablesValid(const uint8_t *source,const uint8_t *candidate,int expanded){
    if(!AudioSceneSongsValid(candidate))return 0;
    if(memcmp(source+0xed652,candidate+0xed652,0x1544)){
        static const char expected[]="ba4f920586267d9e804d4355a51f9b1d5e6a25f6b1f1fbd80063ded95fb18450";
        uint8_t digest[32];char hex[65];sha256_compute(candidate+0xed652,0x1544,digest);
        for(unsigned n=0;n<32;n++)snprintf(hex+n*2,3,"%02x",digest[n]);if(strcmp(hex,expected)&&strcmp(hex,"27a27cf46e6c5d955fe18bec0651613b80352420b10bc3bdaa4b24b2a080bcdd")&&strcmp(hex,"1ebfd8d907a28879c652be0ee438d3f735141c6c34317e1ad5d26c4706c3222f"))return 0;
    }
    unsigned lists=FarAddress(candidate+0xefdd7),chunks=FarAddress(candidate+0xefe14);
    if(FarAddress(candidate+0xefdec)!=lists||FarAddress(candidate+0xefe01)!=lists||FarAddress(candidate+0xefe0f)!=chunks+3||FarAddress(candidate+0xefe1c)!=chunks+2||!DataStorage(lists,2428,expanded)||!DataStorage(chunks,512*5,expanded))return 0;
    unsigned original_chunks=FarAddress(source+0xefe14);
    for(unsigned id=0;id<371;id++) {
        const uint8_t *old=source+original_chunks+id*5,*now=candidate+chunks+id*5;
        if(!memcmp(old,now,5))continue;
        unsigned encoded=Word16(old+3),bytes=(encoded&0x8000)?(encoded&32767)+2:encoded;
        unsigned before=FarAddress(old),after=FarAddress(now);
        if(encoded!=Word16(now+3)||before+bytes>(expanded?0x400000u:0x200000u)||!DataStorage(after,bytes,expanded)||memcmp(source+before,candidate+after,bytes))return 0;
    }
    for(unsigned module=0;module<48;module++){
        unsigned p=lists+Word16(candidate+0xeeb96+module*2);int complete=0;
        for(unsigned group=0;group<256;group++){
            if(!DataStorage(p,2,expanded))return 0;unsigned apu=Word16(candidate+p);p+=2;if(!apu){complete=1;break;}
            if(apu<0x1ee0)return 0;int terminated=0;
            for(unsigned count=0;count<512;count++){
                if(!DataStorage(p,2,expanded))return 0;unsigned id=Word16(candidate+p);p+=2;if(id&0x8000){terminated=1;break;}if(id>=512)return 0;
                const uint8_t *entry=candidate+chunks+id*5;unsigned address=FarAddress(entry),encoded=Word16(entry+3),length=encoded;
                if(id>=371){if((encoded&0x8000)||!encoded||!DataStorage(address,encoded,expanded))return 0;}
                if(address>=(expanded?0x400000u:0x200000u)||!encoded)return 0;if(encoded&0x8000){if(address+2>(expanded?0x400000u:0x200000u))return 0;length=Word16(candidate+address);}if(length>65536-apu)return 0;apu+=length;
            }if(!terminated)return 0;
        }if(!complete)return 0;
    }
    unsigned turbo=Word16(candidate+0xefd71)|((candidate[0xefd74]&63)<<16);
    if(turbo!=0xefcb1){if(!DataStorage(turbo,6,expanded)||Word16(candidate+turbo)!=4||Word16(candidate+turbo+2)!=642||!DataStorage(turbo,Word16(candidate+turbo+4)+4,expanded))return 0;}
    for(unsigned map=0;map<27;map++)if(candidate[0x1f3783+map*2]>=48||candidate[0x1f3782+map*2]>=19)return 0;
    return 1;
}
static void GraphicDataMask(const uint8_t *source,size_t length,uint8_t *mask) {
    unsigned chunks=FarAddress(source+0xefe14);
    if(chunks+371*5<=length)for(unsigned id=0;id<371;id++) {
        const uint8_t *entry=source+chunks+id*5;unsigned address=FarAddress(entry),encoded=Word16(entry+3),size=(encoded&32767)+2;
        if((encoded&0x8000)&&address>=0x10000&&address+size<0xed652)memset(mask+address,1,size);
    }
    for(unsigned sprite=0;sprite<2;sprite++) {
        unsigned root=sprite?0x1a8000:0x1b0000,base=sprite?0x1d0:0x7e;
        for(unsigned id=sprite?0:2;id<(sprite?232:63);id++) {
            unsigned header=root+base+Word16(source+root+id*2);
            if(header+2>=length)continue;
            unsigned height=source[header]&(sprite?255:254),width=source[header+1]+(sprite?0:1);if(!height)height=256;
            if(header+2+width*3>length)continue;
            for(unsigned x=0;x<width;x++) {
                const uint8_t *ptr=source+header+2+x*3;unsigned bank=ptr[2];
                unsigned start=bank<64?(bank&63)*32768+(Word16(ptr)&32767):(bank&63)*65536+Word16(ptr),p=start,y=0;int ended=0;
                while(y<height&&p+2<length) {
                    int value=source[p++];if(sprite&&value){y++;continue;}
                    int run=(int8_t)(sprite?source[p++]:value);if(!run){ended=sprite;break;}
                    y+=(unsigned)(run<0?-run:run);p+=run<0?1:sprite?0:(unsigned)run;
                }
                if(sprite&&y>=height&&p+1<length&&source[p]==0&&source[p+1]==0)p+=2;
                if((y>=height||ended)&&start<0x200000&&p<=0x200000)memset(mask+start,1,p-start);
            }
        }
    }
}
static int GraphicDataAllowed(const uint8_t *mask,unsigned start,unsigned size) {
    if(start>=0x200000||size>0x200000-start)return 0;
    for(unsigned i=0;i<size;i++)if(!mask[start+i])return 0;return 1;
}
static int GraphicsPointer(unsigned start,unsigned size,const uint8_t *source) {
    if(start>=0x1f150e&&start<0x1f238e&&size<=0x1f238e - start)return 1;
    static const Range weapon_tiles[]={{0x1df63f,2496},{0x1f649d,6272},{0x1ecaed,6656},{0x1f238e,3488},{0x1fb468,5056},{0x1f4b66,3232},{0x1f649d,3200},{0x1fb468,2592}};
    static const unsigned hud_pointers[]={0x1f5d84,0x1ff08f,0x1ff5f3,0x1fa3cc,0x1fa3ec,0x1fa1a0,0x1fa1c0,0x1fa1b0};
    for(unsigned i=0;i<sizeof(weapon_tiles)/sizeof(weapon_tiles[0]);i++)if(start>=weapon_tiles[i].start&&start<weapon_tiles[i].start+weapon_tiles[i].size&&size<=weapon_tiles[i].start+weapon_tiles[i].size-start)return 1;
    for(unsigned i=0;i<sizeof(hud_pointers)/sizeof(hud_pointers[0]);i++)if(start>=hud_pointers[i]&&start<hud_pointers[i]+3&&size<=hud_pointers[i]+3-start)return 1;
    if(start>=0x1f5ec6&&start<0x1f649d&&size<=0x1f649d-start)return 1;
    if(start>=0x1ff0a8&&start<0x1ff19e&&size<=0x1ff19e - start)return 1;
    if(start>=0x1ff603&&start<0x1ff66f&&size<=0x1ff66f-start)return 1;
    /* Both native 128x128 sky panoramas are raw, bottom-up columns. */
    if(start>=0x8000 && start<0x10000 && size<=0x10000-start)return 1;
    for(unsigned screen=0;screen<5;screen++) {
        const unsigned tables[]={0xeff89,0x1ff03d,0x1f5d94,0x1f5dd6,0x1f5e18};unsigned table=tables[screen],count=screen?4:3;
        for(unsigned row=0;row<count;row++) {
            unsigned entry=table+row*16;
            if(start>=entry && start<entry+6 && size<=entry+6-start)return 1;
            if(start>=entry+10 && start<entry+12 && size<=entry+12-start)return 1;
        }
    }
    for(unsigned sprite=0;sprite<2;sprite++) {
        unsigned root=sprite?0x1a8000:0x1b0000,count=sprite?232:63,base=sprite?0x1d0:0x7e;
        for(unsigned id=0;id<count;id++) {
            unsigned header=root+base+source[root+id*2]+(source[root+id*2+1]<<8);
            unsigned first=header+2,end=first+(source[header+1]+(sprite?0:1))*3;
            if(start>=first && start<end && size<=end-start)return 1;
        }
    }
    return 0;
}
static int Allowed(unsigned start,unsigned size,int campaign) {
    for(unsigned i=0;i<sizeof(ranges)/sizeof(ranges[0]);i++)
        if(start>=ranges[i].start && start<ranges[i].start+ranges[i].size && size<=ranges[i].start+ranges[i].size-start)return 1;
    /* Converter campaigns can alias E1M1 to an allocated native slot and
     * change only these verified retail routing/checksum fields. Editor
     * patches retain their original, narrower permissions. */
    static const Range campaign_ranges[]={
        {0x7fdc,4},{0x1f32a8,4},{0x1f3566,4},
        {0x1ffbc6,27},{0x1ffb4e,1},{0x1ffb55,1},
    };
    if(campaign)for(unsigned i=0;i<sizeof(campaign_ranges)/sizeof(campaign_ranges[0]);i++)
        if(start>=campaign_ranges[i].start && start<campaign_ranges[i].start+campaign_ranges[i].size && size<=campaign_ranges[i].start+campaign_ranges[i].size-start)return 1;
    return 0;
}
static int Fail(char *error,size_t cap,const char *message) {if(error && cap)snprintf(error,cap,"%s",message);return 0;}
static int TextValid(const uint8_t *source,const uint8_t *candidate,int expanded){
    static const unsigned starts[]={0x1f5ec6,0x1f609b,0x1f628a,0x1f649d};
    for(unsigned e=0;e<3;e++){
        unsigned p=starts[e],end=starts[e+1];int done=0;
        while(p<end){unsigned c=candidate[p++];if(c==255){done=1;break;}if(c==0){if(end-p<2)return 0;unsigned v=Word16(candidate+p);p+=2;if(v<0x7800||v>=0x7b80)return 0;}else if(c<32||c>126)return 0;}
        if(!done)return 0;
    }
    for(unsigned i=0;i<41;i++){
        unsigned p=0x1ff0a8+i*6;if(!memcmp(source+p,candidate+p,6))continue;
        unsigned dest=Word16(candidate+p),page=Word16(source+p)>=0x7400?0x7400:0x7000;
        unsigned length=candidate[p+5],address=FarAddress(candidate+p+2);
        if(dest<page||dest>=page+28*32||!length||length>32-(dest%32)||!DataStorage(address,length,expanded))return 0;
        for(unsigned n=0;n<length;n++)if(candidate[address+n]<32||candidate[address+n]>126)return 0;
    }
    for(unsigned i=0;i<18;i++){
        unsigned p=0x1ff603+i*6;if(!memcmp(source+p,candidate+p,6))continue;
        unsigned dest=Word16(candidate+p),length=candidate[p+5],address=FarAddress(candidate+p+2);
        if(dest<0x2000||dest>=0x2380||!length||length>32-(dest%32)||!CpuBankValid(candidate[p+4],expanded)||address+length>(expanded?0x400000u:0x200000u))return 0;
        unsigned before=FarAddress(source+p+2);
        if(!DataStorage(address,length,expanded)&&(address!=before||length!=source[p+5]))return 0;
        for(unsigned n=0;n<length;n++)if(candidate[address+n]<32||candidate[address+n]>129)return 0;
    }return 1;
}
static int GraphicsTransfersValid(const uint8_t *source,const uint8_t *candidate,int expanded) {
    static const unsigned pointers[]={0x1f5d84,0x1ff08f,0x1ff5f3,0x1fa3cc,0x1fa3ec,0x1fa1a0,0x1fa1c0,0x1fa1b0};
    static const unsigned lengths[]={3168,3168,3168,3456,1280,12288,2048,192};
    for(unsigned i=0;i<sizeof(pointers)/sizeof(pointers[0]);i++){
        unsigned p=pointers[i];if(!memcmp(source+p,candidate+p,3))continue;
        unsigned address=FarAddress(candidate+p),length=lengths[i];
        size_t limit=expanded?0x400000u:0x200000u;
        if(!CpuBankValid(candidate[p+2],expanded)||address+6>limit||Word16(candidate+address+2)!=length)return 0;
        unsigned stored=AssetBlobSize(candidate,limit,address);
        if(!stored||!GraphicsBlobValid(candidate,limit,address)||!DataStorage(address,stored,expanded)||(address&65535)+stored>65536)return 0;
    }
    if((memcmp(source+0x1f5d84,candidate+0x1f5d84,3)||memcmp(source+0x1ff08f,candidate+0x1ff08f,3)||memcmp(source+0x1ff5f3,candidate+0x1ff5f3,3))&&(memcmp(candidate+0x1f5d84,candidate+0x1ff08f,3)||memcmp(candidate+0x1f5d84,candidate+0x1ff5f3,3)))return 0;
    for(unsigned screen=0;screen<5;screen++)for(unsigned row=0;row<(screen?4u:3u);row++) {
        const unsigned tables[]={0xeff89,0x1ff03d,0x1f5d94,0x1f5dd6,0x1f5e18};unsigned entry=tables[screen]+row*16;
        if(!memcmp(source+entry,candidate+entry,12))continue;
        unsigned flags=candidate[entry]|candidate[entry+1]<<8;
        unsigned expected=(source[entry]|source[entry+1]<<8)&~0x4000u;
        unsigned bank=candidate[entry+4],offset=candidate[entry+2]|candidate[entry+3]<<8;
        unsigned length=candidate[entry+10]|candidate[entry+11]<<8;
        if((flags&~0x4000u)!=expected || !CpuBankValid(bank,expanded) || candidate[entry+5])return 0;
        unsigned address=(bank&63)*65536+offset,allowed=0;
        if(!(flags&0x4000)){
            size_t limit=expanded?0x400000u:0x200000u;
            if(address+6>limit||!Word16(candidate+address+2)||(Word16(candidate+address+2)&1))return 0;
            length=AssetBlobSize(candidate,limit,address);
            if(!GraphicsBlobValid(candidate,limit,address))return 0;
        }
        if(!length || ((flags&0x4000)&&(length&1)) || offset+length>65536)return 0;
        for(unsigned i=0;i+2<sizeof(ranges)/sizeof(ranges[0]);i++)
            if(address>=ranges[i].start && address<ranges[i].start+ranges[i].size && length<=ranges[i].start+ranges[i].size-address)allowed=1;
        if(!allowed&&!(expanded&&address>=0x200000&&address+length<=0x3e0000))return 0;
    }
    return 1;
}
#include "doom_asset_compact.inc"
int DoomEditorPatchApply(const uint8_t *source,size_t source_size,
    const uint8_t *patch,size_t patch_size,uint8_t *target,char *error,size_t cap) {
    static const uint8_t retail[32]={0xd4,0x5e,0x26,0xeb,0x10,0xc3,0x23,0xec,0xd4,0x80,0xe5,0xf2,0x32,0x6b,0x22,0x3e,
        0x29,0x26,0x4c,0x3a,0xdd,0xe6,0x7f,0x48,0xf0,0xd2,0x79,0x11,0x28,0xe5,0x19,0xe8};
    uint8_t digest[32];
    if(!source || !target || source==target || (source_size!=0x200000&&source_size!=0x400000) || !patch || patch_size<76 || patch_size>16*1024*1024)return Fail(error,cap,"Invalid editor patch header or ROM size.");
    if(!memcmp(patch,"DSEAST7\0",8))return CompactAssetApply(source,source_size,patch,patch_size,target,error,cap);
    int rebased=!memcmp(patch,"DSEMOD6\0",8),expanded=rebased||!memcmp(patch,"DSEMOD5\0",8);size_t target_size=DoomEditorPatchTargetSize(patch,patch_size);int audio=expanded||!memcmp(patch,"DSEAUD4\0",8);
    int graphics=audio || !memcmp(patch,"DSEGFX3\0",8);
    int campaign=rebased||(!expanded && (graphics || !memcmp(patch,"DSECNV2\0",8)));
    if(!expanded && !campaign && memcmp(patch,"DSEMAP1\0",8))return Fail(error,cap,"Unknown level/campaign patch format.");
    sha256_compute(source,source_size,digest);
    if(memcmp(digest,patch+8,32)||(!rebased&&(source_size!=0x200000||memcmp(digest,retail,32)))||(rebased&&!DoomEditorRomCompatible(source,source_size)))return Fail(error,cap,"Editor patch requires its exact compatible base ROM.");
    uint32_t count=Word32(patch+72),previous_end=0;size_t pos=76;
    if(count>source_size)return Fail(error,cap,"Invalid editor record count.");
    uint8_t *graphic_mask=graphics?calloc(0x200000,1):NULL;
    if(graphics&&!graphic_mask)return Fail(error,cap,"Unable to allocate graphics validation.");
    if(graphic_mask)GraphicDataMask(source,source_size,graphic_mask);
    for(uint32_t i=0;i<count;i++) {
        if(patch_size-pos<8){free(graphic_mask);return Fail(error,cap,"Truncated editor record.");}
        uint32_t start=Word32(patch+pos),size=Word32(patch+pos+4);pos+=8;
        if(!size || start<previous_end || start>=target_size || size>target_size-start || size>patch_size-pos || (!(expanded?((start>=0x200000&&start<0x3e0000&&size<=0x3e0000-start)||(start>=0x7fdc&&start+size<=0x7fe0)||(start==0x7fd7&&size==1)||(rebased&&Allowed(start,size,1))):Allowed(start,size,campaign)) && !(graphics && (GraphicsPointer(start,size,source)||GraphicDataAllowed(graphic_mask,start,size))) && !(audio && AudioField(start,size)))){free(graphic_mask);return Fail(error,cap,"Editor patch contains an invalid or non-level range.");}
        previous_end=start+size;pos+=size;
    }
    free(graphic_mask);
    if(pos!=patch_size)return Fail(error,cap,"Editor patch contains trailing data.");
    /* Even a failed target hash must leave caller output untouched. */
    uint8_t *candidate=malloc(target_size);if(!candidate)return Fail(error,cap,"Unable to allocate editor ROM image.");
    memset(candidate,255,target_size);memcpy(candidate,source,source_size);pos=76;
    for(uint32_t i=0;i<count;i++) {uint32_t start=Word32(patch+pos),size=Word32(patch+pos+4);pos+=8;memcpy(candidate+start,patch+pos,size);pos+=size;}
    sha256_compute(candidate,target_size,digest);
    if(memcmp(digest,patch+40,32)) {free(candidate);return Fail(error,cap,"Editor patch checksum does not match.");}
    if(graphics && !GraphicsTransfersValid(source,candidate,expanded)) {free(candidate);return Fail(error,cap,"Invalid native graphics transfer.");}
    if(graphics && !TextValid(source,candidate,expanded)){free(candidate);return Fail(error,cap,"Invalid native ending or credits text.");}
    int audio_changed=memcmp(source+0xed652,candidate+0xed652,0x1544)||memcmp(source+0xeeb96,candidate+0xeeb96,96)||memcmp(source+0x1f3782,candidate+0x1f3782,54)||memcmp(source+0xefd70,candidate+0xefd70,0xb0)||memcmp(source+0x1fef6b,candidate+0x1fef6b,2)||memcmp(source+0x1f5811,candidate+0x1f5811,2);
    if(audio && (!expanded||audio_changed) && !AudioTablesValid(source,candidate,expanded)) {free(candidate);return Fail(error,cap,"Invalid native audio tables, SPC ranges or song assignments.");}
    if(campaign && (!rebased || memcmp(source+0x1ffbc6,candidate+0x1ffbc6,27) || source[0x1ffb4e]!=candidate[0x1ffb4e] || source[0x1ffb55]!=candidate[0x1ffb55])) {
        int alias=0;
        for(unsigned slot=0;slot<27;slot++)
            if(!memcmp(candidate+0x1f32a8,source+0x1f32a8+slot*4,4) &&
               !memcmp(candidate+0x1f3566,source+0x1f3566+slot*4,4) &&
               (source[0x1f33b6+slot*2] || source[0x1f33b7+slot*2]))alias=1;
        unsigned checksum=0;for(size_t i=0;i<target_size;i++)checksum=(checksum+candidate[i])&65535;
        unsigned expected=candidate[0x7fde]|candidate[0x7fdf]<<8;
        unsigned complement=candidate[0x7fdc]|candidate[0x7fdd]<<8;
        int route_ok=1;for(unsigned i=0;i<27;i++)if(candidate[0x1ffbc6+i]>27)route_ok=0;
        if(!alias || !route_ok || candidate[0x1ffb4e]!=255 || candidate[0x1ffb55]!=255 || checksum!=expected || (expected^complement)!=65535) {
            free(candidate);return Fail(error,cap,"Invalid campaign alias, route, difficulty gate or ROM checksum.");
        }
    }
    if(expanded){unsigned sum=0;for(size_t i=0;i<target_size;i++)sum=(sum+candidate[i])&65535;if(candidate[0x7fd7]!=12||Word16(candidate+0x7fde)!=sum||(Word16(candidate+0x7fdc)^sum)!=65535){free(candidate);return Fail(error,cap,"Invalid expanded ROM header or checksum.");}}memcpy(target,candidate,target_size);free(candidate);if(error && cap)error[0]=0;return 1;
}
