#define main legacy_patch_main
#include "test_doom_editor_patch.c"
#undef main
int main(int argc,char **argv){
    if(argc!=4)return 2;size_t ns,np,nt;unsigned char *s=Read(argv[1],&ns),*p=Read(argv[2],&np),*expected=Read(argv[3],&nt),*out=malloc(nt);char error[256];
    if(ns!=0x200000||nt!=0x200000||!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error))||memcmp(out,expected,nt)){puts("FAIL scene patch exact target");return 1;}
    const unsigned fields[]={0x1fef6b,0x1fef6c,0x1f5811,0x1f5812,0x1fef6a,0x1f5813};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++){
        unsigned char *bad=malloc(nt);memcpy(bad,expected,nt);bad[fields[i]]=i<4?(i&1?48:19):0;Checksum(bad,nt);
        size_t length;unsigned char *encoded=CampaignPatch(s,bad,ns,&length);memcpy(encoded,p,8);memset(out,0x5a,nt);
        if(DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error))){printf("FAIL accepted invalid scene field %x\n",fields[i]);return 1;}
        for(size_t n=0;n<nt;n++)if(out[n]!=0x5a){puts("FAIL scene rejection changed output");return 1;}free(encoded);free(bad);
    }
    free(s);free(p);free(expected);free(out);puts("PASS scene music patch: exact target; invalid song/module IDs and surrounding opcodes rejected transactionally");return 0;
}
