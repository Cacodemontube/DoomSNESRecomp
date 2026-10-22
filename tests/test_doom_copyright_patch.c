#define main legacy_patch_main
#include "test_doom_editor_patch.c"
#undef main
int main(int argc,char **argv){
    if(argc!=4)return 2;size_t ns,np,nt;unsigned char *s=Read(argv[1],&ns),*p=Read(argv[2],&np),*expected=Read(argv[3],&nt),*out=malloc(nt);char error[256];
    if(ns!=0x200000||nt!=0x200000||!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error))||memcmp(out,expected,nt)){puts("FAIL copyright exact target");return 1;}
    unsigned text=(expected[0x1ff605]|expected[0x1ff606]<<8|expected[0x1ff607]<<16)&0x3fffff;
    const unsigned fields[]={0x1ff603,0x1ff604,0x1ff608,0x1ff607,text,0x1ff570};
    const unsigned values[]={31,0x24,33,0x7f,130,0};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++){
        unsigned char *bad=malloc(nt);memcpy(bad,expected,nt);bad[fields[i]]=values[i];Checksum(bad,nt);
        size_t length;unsigned char *encoded=CampaignPatch(s,bad,ns,&length);memcpy(encoded,p,8);memset(out,0x5a,nt);
        if(DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error))){printf("FAIL accepted invalid copyright field %x\n",fields[i]);return 1;}
        for(size_t n=0;n<nt;n++)if(out[n]!=0x5a){puts("FAIL rejection changed output");return 1;}free(encoded);free(bad);
    }
    free(s);free(p);free(expected);free(out);puts("PASS copyright patch exact target; invalid row/column, length, bank, glyph and engine mutation rejected transactionally");return 0;
}
