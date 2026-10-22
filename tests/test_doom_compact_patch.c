#define main legacy_patch_main
#include "test_doom_editor_patch.c"
#undef main
int main(int argc,char **argv){
    if(argc!=4)return 2;size_t ns,np,nt;unsigned char *source=Read(argv[1],&ns),*patch=Read(argv[2],&np),*expected=Read(argv[3],&nt),*out=malloc(nt);char error[256];
    if(ns!=0x200000||nt!=0x200000||memcmp(patch,"DSEAST7\0",8)||DoomEditorPatchTargetSize(patch,np)!=nt||!DoomEditorPatchApply(source,ns,patch,np,out,error,sizeof(error))||memcmp(out,expected,nt)){puts("FAIL exact compacted target");return 1;}
    const unsigned offsets[]={0xefd70,0xed652,0x189ed4,0x1ffbc6,0x7fd7,0x1f3783,0x1f5d86,0xefe16,0xefd74};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++){unsigned char *bad=malloc(nt);memcpy(bad,expected,nt);bad[offsets[i]]=i==4?12:255;Checksum(bad,nt);size_t length;unsigned char *encoded=CampaignPatch(source,bad,ns,&length);memcpy(encoded,"DSEAST7\0",8);memset(out,0x5a,nt);if(DoomEditorPatchApply(source,ns,encoded,length,out,error,sizeof(error))){printf("FAIL accepted mutation %x\n",offsets[i]);return 1;}for(size_t n=0;n<nt;n++)if(out[n]!=0x5a){puts("FAIL failure was not transactional");return 1;}free(encoded);free(bad);}
    memset(out,0x5a,nt);patch[40]^=1;if(DoomEditorPatchApply(source,ns,patch,np,out,error,sizeof(error)))return 1;patch[40]^=1;if(DoomEditorPatchApply(source,ns,patch,np-1,out,error,sizeof(error)))return 1;source[0]^=1;if(DoomEditorPatchApply(source,ns,patch,np,out,error,sizeof(error)))return 1;
    free(source);free(patch);free(expected);free(out);puts("PASS compacted asset profile: exact target, protected code/driver/maps/routes, invalid header/song/pointers, source/hash/truncation and transactional failures");return 0;
}
