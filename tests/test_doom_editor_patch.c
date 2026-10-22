#include "doom_editor_patch.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *Read(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}
static void W32(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static void Checksum(unsigned char *b,size_t n){b[0x7fdc]=b[0x7fdd]=255;b[0x7fde]=b[0x7fdf]=0;unsigned sum=0;for(size_t i=0;i<n;i++)sum=(sum+b[i])&65535;b[0x7fde]=(unsigned char)sum;b[0x7fdf]=(unsigned char)(sum>>8);sum^=65535;b[0x7fdc]=(unsigned char)sum;b[0x7fdd]=(unsigned char)(sum>>8);}
static unsigned char *CampaignPatch(const unsigned char *source,const unsigned char *image,size_t n,size_t *length){unsigned char *p=malloc(n*9+76);assert(p);memcpy(p,"DSECNV2\0",8);sha256_compute(source,n,p+8);sha256_compute(image,n,p+40);unsigned count=0;size_t pos=76;for(size_t i=0;i<n;){if(source[i]==image[i]){i++;continue;}size_t start=i;while(i<n&&source[i]!=image[i])i++;W32(p+pos,(unsigned)start);W32(p+pos+4,(unsigned)(i-start));pos+=8;memcpy(p+pos,image+start,i-start);pos+=i-start;count++;}W32(p+72,count);*length=pos;return p;}
int main(int argc,char **argv) {
    assert(argc==4);size_t ns,np,nt;unsigned char *s=Read(argv[1],&ns),*p=Read(argv[2],&np),*expected=Read(argv[3],&nt),*out=malloc(nt);assert(out);
    char error[256];assert(DoomEditorPatchTargetSize(p,np)==nt);assert(DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));assert(!memcmp(out,expected,nt));
    if(!memcmp(p,"DSEMOD6\0",8)) {
        unsigned char *baseline=malloc(nt);assert(baseline);memset(baseline,255,nt);memcpy(baseline,s,ns);
        const unsigned offsets[]={0xefd80,0xed652,0x1fa000,0x3f0000,0x1ffbc6,0x1fa1a2};
        for(unsigned k=0;k<sizeof(offsets)/sizeof(*offsets);k++){
            unsigned char *bad=malloc(nt);assert(bad);memcpy(bad,expected,nt);bad[offsets[k]]^=0xff;Checksum(bad,nt);
            size_t length;unsigned char *encoded=CampaignPatch(baseline,bad,nt,&length);memcpy(encoded,"DSEMOD6\0",8);sha256_compute(s,ns,encoded+8);memset(out,0x5a,nt);
            assert(!DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error)));for(size_t i=0;i<nt;i++)assert(out[i]==0x5a);free(encoded);free(bad);
        }
        free(baseline);puts("PASS: modified-base profile rejects code, excluded banks, invalid routes and graphics with recomputed hashes; requires exact base and remains transactional");
    }
    if(!memcmp(p,"DSEMOD5\0",8)) {
        unsigned char *baseline=malloc(nt);assert(baseline);memset(baseline,255,nt);memcpy(baseline,s,ns);
        const unsigned offsets[]={0xefd70,0xed652,0x189ed4,0x1ffbc6,0x7fd7,0x1f3783,0x1f5d98,0x1f5ec6,0x1ff0ad};
        for(unsigned k=0;k<sizeof(offsets)/sizeof(*offsets);k++){
            unsigned char *bad=malloc(nt);assert(bad);memcpy(bad,expected,nt);bad[offsets[k]]^=0xff;if(offsets[k]==0x1f5ec6)bad[offsets[k]]=0x80;Checksum(bad,nt);
            size_t length;unsigned char *encoded=CampaignPatch(baseline,bad,nt,&length);memcpy(encoded,"DSEMOD5\0",8);sha256_compute(s,ns,encoded+8);memset(out,0x5a,nt);
            assert(!DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error)));for(size_t i=0;i<nt;i++)assert(out[i]==0x5a);free(encoded);free(bad);
        }
        free(baseline);puts("PASS: expanded retail profile rejects opcode/map/progression/header/song/graphics mutations with valid recomputed hashes; preserves output transactionally");
    }
    if(!memcmp(p,"DSEAUD4\0",8)) {
        memcpy(p,"DSEGFX3\0",8);assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));memcpy(p,"DSEAUD4\0",8);
        unsigned chunk_table=(expected[0xefe14]|expected[0xefe15]<<8|expected[0xefe16]<<16)&0x1fffff;
        const unsigned bad_offsets[]={0xefdd7,0xefe14,0x1f3783,0xefd70,chunk_table+371*5+4};
        const unsigned char bad_values[]={0,0,255,0xea,0x80};
        for(unsigned k=0;k<5;k++) {
            unsigned char *bad=malloc(nt);assert(bad);memcpy(bad,expected,nt);bad[bad_offsets[k]]=bad_values[k];Checksum(bad,nt);
            size_t length;unsigned char *encoded=CampaignPatch(s,bad,ns,&length);memcpy(encoded,"DSEAUD4\0",8);memset(out,0x5a,ns);
            assert(!DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error)));for(size_t i=0;i<ns;i++)assert(out[i]==0x5a);
            free(encoded);free(bad);
        }
        puts("PASS: audio profile rejects mismatched relocated tables, invalid songs, opcode changes, compressed custom chunks and profile downgrades with recomputed hashes");
    }
    if(!memcmp(p,"DSECNV2\0",8)) {
        memcpy(p,"DSEMAP1\0",8);assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));memcpy(p,"DSECNV2\0",8);
        const unsigned bad_offsets[]={0x1ffb4d,0x1ffb4e,0x1ffbc6,0x1f32aa};
        const unsigned char bad_values[]={0xea,0xea,0xff,0x00};
        for(unsigned k=0;k<4;k++) {
            unsigned char *bad=malloc(nt);assert(bad);memcpy(bad,expected,nt);bad[bad_offsets[k]]=bad_values[k];Checksum(bad,nt);
            size_t length;unsigned char *encoded=CampaignPatch(s,bad,ns,&length);memset(out,0x5a,ns);
            assert(!DoomEditorPatchApply(s,ns,encoded,length,out,error,sizeof(error)));for(size_t i=0;i<ns;i++)assert(out[i]==0x5a);
            free(encoded);free(bad);
        }
        puts("PASS: campaign profile rejects arbitrary code, invalid gate operands, route destinations and far-pointer aliases even with recomputed hashes/checksum");
    }
    memset(out,0x5a,ns);p[40]^=1;assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));for(size_t i=0;i<ns;i++)assert(out[i]==0x5a);p[40]^=1;
    assert(!DoomEditorPatchApply(s,ns,p,np-1,out,error,sizeof(error)));
    unsigned char save[4];memcpy(save,p+76,4);memset(p+76,0,4);assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));memcpy(p+76,save,4);
    s[0]^=1;assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));s[0]^=1;
    p[72]=255;p[73]=255;p[74]=255;p[75]=255;assert(!DoomEditorPatchApply(s,ns,p,np,out,error,sizeof(error)));
    free(out);free(expected);free(p);free(s);puts("PASS: native editor patch, exact target, corrupt/truncated patch, code range, wrong source, record overflow, transactional failure");return 0;
}
