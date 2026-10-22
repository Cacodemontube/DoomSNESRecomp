/* Profile 7: bounded 2 MiB asset repacking, including recompressed SPC chunks.
 * No 65816/GSU code allocation is writable through the owned-data mask. */
static unsigned AssetBlobSize(const uint8_t *b,size_t size,unsigned p){
    if(p+6>size)return 0;unsigned kind=Word16(b+p),left=Word16(b+p+2),q=p+5;
    if(kind==0)return left&&p+4+left<=size?left+4:0;
    if(kind==4){unsigned n=Word16(b+p+4)+4;return n>=6&&p+n<=size?n:0;}
    if(kind!=8&&kind!=12)return 0;unsigned stagger=b[p+4];
    while(left&&q<size){unsigned c=b[q++],n;if(c&128){n=((c>>3)&15)+3;q++;}else if(c&64){n=(c&63)+1;q+=n;}else if(c&32){n=stagger==1?1:(c&31)+3;if(stagger!=1)q++;}else{n=c&31;if(!n)return 0;n+=2;q++;}if(n>left||q>size)return 0;left-=n;}return left?0:q-p;
}
typedef struct AssetBits {const uint8_t *b;unsigned p,word;int valid;} AssetBits;
static unsigned AssetReadBits(AssetBits *s,unsigned n){unsigned v=0;while(n--){unsigned bit=s->word&1;s->word>>=1;if(!s->word){if(s->p<2){s->valid=0;return 0;}s->p-=2;unsigned next=Word16(s->b+s->p);bit=next&1;s->word=(next>>1)|32768;}v=(v<<1)|bit;}return v;}
static int GraphicsBlobValid(const uint8_t *b,size_t size,unsigned address){
    unsigned stored=AssetBlobSize(b,size,address);if(!stored)return 0;unsigned kind=Word16(b+address),total=Word16(b+address+2),left=total;if(!total)return 0;if(!kind)return 1;
    if(kind==4){unsigned end=address+stored-2;AssetBits s={b,end,Word16(b+end),1};while(left&&s.valid){unsigned n,offset=0;if(!AssetReadBits(&s,1)){if(!AssetReadBits(&s,1))n=AssetReadBits(&s,3)+1;else{n=2;offset=AssetReadBits(&s,8);}}else{unsigned t=AssetReadBits(&s,2);if(t==3)n=AssetReadBits(&s,8)+9;else if(t==2){n=AssetReadBits(&s,8)+5;offset=AssetReadBits(&s,12);}else{n=t+3;offset=AssetReadBits(&s,t+9);}}if(n>left||left+offset>total)return 0;if(!offset)for(unsigned i=0;i<n;i++)AssetReadBits(&s,8);if(s.p<address+6)return 0;left-=n;}return s.valid&&!left;}
    unsigned q=address+5,stagger=b[address+4];if((kind!=8&&kind!=12)||(stagger!=1&&stagger!=2))return 0;
    while(left&&q<address+stored){unsigned c=b[q++],n,offset=0;if(c&128){n=((c>>3)&15)+3;offset=(((c&7)<<8)|b[q++])+1;}else if(c&64){n=(c&63)+1;q+=n;}else if(c&32){if(stagger==1){n=1;offset=(c&31)+1;}else{n=(c&31)+3;q++;}}else{n=c&31;if(!n)return 0;n+=2;q++;}if(n>left||left+offset>total||q>address+stored)return 0;left-=n;}return !left;
}
static int AssetTurboDecode(const uint8_t *b,size_t size,unsigned address,uint8_t out[642]){
    unsigned bytes=AssetBlobSize(b,size,address);if(!bytes||Word16(b+address)!=4||Word16(b+address+2)!=642)return 0;
    unsigned end=address+2+Word16(b+address+4);if(end+2>size)return 0;AssetBits s={b,end,Word16(b+end),1};unsigned p=642;
    while(p&&s.valid){unsigned n,offset=0;if(!AssetReadBits(&s,1)){if(!AssetReadBits(&s,1))n=AssetReadBits(&s,3)+1;else{n=2;offset=AssetReadBits(&s,8);}}else{unsigned t=AssetReadBits(&s,2);if(t==3)n=AssetReadBits(&s,8)+9;else if(t==2){n=AssetReadBits(&s,8)+5;offset=AssetReadBits(&s,12);}else{n=t+3;offset=AssetReadBits(&s,t+9);}}if(n>p||p+offset>642)return 0;while(n--){p--;out[p]=offset?out[p+offset]:(uint8_t)AssetReadBits(&s,8);}}return s.valid&&p==0;
}
static void AssetMark(uint8_t *mask,unsigned p,unsigned n){if(p<0x200000&&n<=0x200000-p)memset(mask+p,1,n);}
static int AssetRange(const uint8_t *mask,unsigned p,unsigned n){if(!n||p>=0x200000||n>0x200000-p)return 0;for(unsigned i=0;i<n;i++)if(!mask[p+i])return 0;return 1;}
static unsigned AssetChunkCount(const uint8_t *b,size_t size,unsigned lists){unsigned highest=370;for(unsigned module=0;module<48;module++){unsigned p=lists+Word16(b+0xeeb96+module*2);int done=0;for(unsigned group=0;group<256;group++){if(p+2>size)return 0;unsigned dest=Word16(b+p);p+=2;if(!dest){done=1;break;}int terminated=0;for(unsigned n=0;n<512;n++){if(p+2>size)return 0;unsigned id=Word16(b+p);p+=2;if(id&32768){terminated=1;break;}if(id>=512)return 0;if(id>highest)highest=id;}if(!terminated)return 0;}if(!done)return 0;}return highest+1;}
static int AssetOwnership(const uint8_t *b,size_t size,uint8_t *mask){
    GraphicDataMask(b,size,mask);unsigned lists=FarAddress(b+0xefdd7),chunks=FarAddress(b+0xefe14),count=AssetChunkCount(b,size,lists);if(!count||chunks+count*5>size)return 0;
    unsigned list_end=lists;for(unsigned module=0;module<48;module++){unsigned p=lists+Word16(b+0xeeb96+module*2);for(unsigned g=0;g<256;g++){unsigned dest=Word16(b+p);p+=2;if(!dest)break;while(!(Word16(b+p)&32768))p+=2;p+=2;}if(p>list_end)list_end=p;}
    AssetMark(mask,lists,list_end-lists);AssetMark(mask,chunks,count*5);AssetMark(mask,0xeebf6,0xefd6d-0xeebf6);
    for(unsigned i=0;i<count;i++){const uint8_t *e=b+chunks+i*5;unsigned encoded=Word16(e+3),length=(encoded&32768)?(encoded&32767)+2:encoded;AssetMark(mask,FarAddress(e),length);}
    unsigned turbo=Word16(b+0xefd71)|((b[0xefd74]&63)<<16);uint8_t data[642];if(!AssetTurboDecode(b,size,turbo,data))return 0;AssetMark(mask,turbo,AssetBlobSize(b,size,turbo));for(unsigned i=0;i<107;i++)if(data[i*6])AssetMark(mask,FarAddress(data+i*6+1),Word16(data+i*6+4));
    const unsigned tables[]={0xeff89,0x1ff03d,0x1f5d94,0x1f5dd6,0x1f5e18};for(unsigned screen=0;screen<5;screen++)for(unsigned row=0;row<(screen?4u:3u);row++){unsigned e=tables[screen]+row*16,p=FarAddress(b+e+2),n=(Word16(b+e)&0x4000)?Word16(b+e+10):AssetBlobSize(b,size,p);if(!n)return 0;AssetMark(mask,p,n);}
    const unsigned hud[]={0x1f5d84,0x1ff08f,0x1ff5f3,0x1fa3cc,0x1fa3ec,0x1fa1a0,0x1fa1c0,0x1fa1b0};for(unsigned i=0;i<8;i++){unsigned p=FarAddress(b+hud[i]),n=AssetBlobSize(b,size,p);if(!n)return 0;AssetMark(mask,p,n);}
    for(unsigned i=0;i<41;i++){unsigned e=0x1ff0a8+i*6;AssetMark(mask,FarAddress(b+e+2),b[e+5]);}for(unsigned i=0;i<18;i++){unsigned e=0x1ff603+i*6;AssetMark(mask,FarAddress(b+e+2),b[e+5]);}return 1;
}
static int CompactAssetTables(const uint8_t *source,const uint8_t *b,const uint8_t *mask){
    if(!AudioSceneSongsValid(b))return 0;
    if(memcmp(source+0xed652,b+0xed652,0x1544)){uint8_t digest[32];char hex[65];sha256_compute(b+0xed652,0x1544,digest);for(unsigned n=0;n<32;n++)snprintf(hex+n*2,3,"%02x",digest[n]);if(strcmp(hex,"ba4f920586267d9e804d4355a51f9b1d5e6a25f6b1f1fbd80063ded95fb18450")&&strcmp(hex,"27a27cf46e6c5d955fe18bec0651613b80352420b10bc3bdaa4b24b2a080bcdd")&&strcmp(hex,"1ebfd8d907a28879c652be0ee438d3f735141c6c34317e1ad5d26c4706c3222f"))return 0;}
    unsigned lists=FarAddress(b+0xefdd7),chunks=FarAddress(b+0xefe14),count=AssetChunkCount(b,0x200000,lists);if(!count||!AssetRange(mask,chunks,count*5)||!AssetRange(mask,lists,2)||FarAddress(b+0xefdec)!=lists||FarAddress(b+0xefe01)!=lists||FarAddress(b+0xefe0f)!=chunks+3||FarAddress(b+0xefe1c)!=chunks+2)return 0;
    for(unsigned id=0;id<count;id++){const uint8_t *e=b+chunks+id*5;unsigned encoded=Word16(e+3),n=(encoded&32768)?(encoded&32767)+2:encoded;if(!n||!CpuBankValid(e[2],0)||!AssetRange(mask,FarAddress(e),n))return 0;}
    for(unsigned module=0;module<48;module++){unsigned p=lists+Word16(b+0xeeb96+module*2);for(unsigned g=0;g<256;g++){if(!AssetRange(mask,p,2))return 0;unsigned dest=Word16(b+p);p+=2;if(!dest)break;if(dest<0x1ee0)return 0;for(unsigned i=0;i<512;i++){if(!AssetRange(mask,p,2))return 0;unsigned id=Word16(b+p);p+=2;if(id&32768)break;if(id>=count)return 0;const uint8_t *e=b+chunks+id*5;unsigned encoded=Word16(e+3),n=(encoded&32768)?Word16(b+FarAddress(e)):encoded;if(n>65536-dest)return 0;dest+=n;}}}
    unsigned turbo=Word16(b+0xefd71)|((b[0xefd74]&63)<<16);uint8_t data[642];if(!CpuBankValid(b[0xefd74],0)||!AssetRange(mask,turbo,AssetBlobSize(b,0x200000,turbo))||!AssetTurboDecode(b,0x200000,turbo,data))return 0;for(unsigned i=0;i<107;i++)if(data[i*6]&&(!CpuBankValid(data[i*6+3],0)||!AssetRange(mask,FarAddress(data+i*6+1),Word16(data+i*6+4))))return 0;
    const unsigned tables[]={0xeff89,0x1ff03d,0x1f5d94,0x1f5dd6,0x1f5e18};for(unsigned s=0;s<5;s++)for(unsigned r=0;r<(s?4u:3u);r++){unsigned e=tables[s]+r*16,p=FarAddress(b+e+2),flags=Word16(b+e),n=(flags&0x4000)?Word16(b+e+10):AssetBlobSize(b,0x200000,p);if((flags&~0x4000)!=(Word16(source+e)&~0x4000)||!CpuBankValid(b[e+4],0)||b[e+5]||!n||!AssetRange(mask,p,n)||(p&65535)+n>65536)return 0;}
    const unsigned hud[]={0x1f5d84,0x1ff08f,0x1ff5f3,0x1fa3cc,0x1fa3ec,0x1fa1a0,0x1fa1c0,0x1fa1b0};for(unsigned i=0;i<8;i++){unsigned p=FarAddress(b+hud[i]);if(!CpuBankValid(b[hud[i]+2],0)||!AssetRange(mask,p,AssetBlobSize(b,0x200000,p)))return 0;}
    for(unsigned i=0;i<41;i++){unsigned e=0x1ff0a8+i*6,p=FarAddress(b+e+2);if(!CpuBankValid(b[e+4],0)||!AssetRange(mask,p,b[e+5]))return 0;}
    for(unsigned i=0;i<18;i++){unsigned e=0x1ff603+i*6,p=FarAddress(b+e+2),dest=Word16(b+e),n=b[e+5];if(dest<0x2000||dest>=0x2380||n>32-dest%32||!CpuBankValid(b[e+4],0)||!AssetRange(mask,p,n))return 0;for(unsigned k=0;k<n;k++)if(b[p+k]<32||b[p+k]>129)return 0;}
    for(unsigned i=0;i<27;i++)if(b[0x1f3783+i*2]>=48||b[0x1f3782+i*2]>=19)return 0;return 1;
}
static int CompactAssetApply(const uint8_t *source,size_t source_size,const uint8_t *patch,size_t patch_size,uint8_t *target,char *error,size_t cap){
    uint8_t digest[32];sha256_compute(source,source_size,digest);if(memcmp(digest,patch+8,32)||!DoomEditorRomCompatible(source,source_size))return Fail(error,cap,"Asset patch requires its exact compatible base ROM.");
    uint8_t *mask=calloc(0x200000,1),*candidate=malloc(0x200000);if(!mask||!candidate){free(mask);free(candidate);return Fail(error,cap,"Asset patch allocation failed.");}int valid=AssetOwnership(source,source_size,mask);memcpy(candidate,source,0x200000);size_t p=76;unsigned previous=0,count=Word32(patch+72);if(count>0x200000)valid=0;
    for(unsigned i=0;valid&&i<count;i++){if(p+8>patch_size){valid=0;break;}unsigned start=Word32(patch+p),n=Word32(patch+p+4);p+=8;if(!n||start<previous||start>=0x200000||n>0x200000-start||n>patch_size-p||!(AssetRange(mask,start,n)||GraphicsPointer(start,n,source)||AudioField(start,n)||(start>=0x7fdc&&start+n<=0x7fe0)||(start==0x7fd7&&n==1))){valid=0;break;}memcpy(candidate+start,patch+p,n);p+=n;previous=start+n;}
    if(p!=patch_size)valid=0;sha256_compute(candidate,0x200000,digest);if(memcmp(digest,patch+40,32))valid=0;if(valid)valid=CompactAssetTables(source,candidate,mask);
    unsigned sum=0;for(unsigned i=0;i<0x200000;i++)sum=(sum+candidate[i])&65535;if(candidate[0x7fd7]!=11||Word16(candidate+0x7fde)!=sum||(Word16(candidate+0x7fdc)^sum)!=65535)valid=0;
    if(valid)memcpy(target,candidate,0x200000);free(mask);free(candidate);if(!valid)return Fail(error,cap,"Invalid compacted asset patch, allocation or checksum.");if(error&&cap)error[0]=0;return 1;
}

