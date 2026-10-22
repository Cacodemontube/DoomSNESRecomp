#include "doom_editor.h"
#include "doom_editor_patch.h"
#include "doom_rotations.h"
#include "sha256.h"
#include "crc32.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDL_MAIN_HANDLED 1
#include "desktop/sdl_compat.h"
#ifdef _WIN32
#include <windows.h>
#endif
static uint8_t *pending;
static size_t pending_size;
static char campaign_path[32768];
static char modified_sha[65],modified_crc[9];
const char *DoomEditorModifiedSha(void){return modified_sha[0]?modified_sha:NULL;}
const char *DoomEditorModifiedCrc(void){return modified_sha[0]?modified_crc:NULL;}
int DoomEditorConfigure(int *argc,char **argv) {
    int modified=0;for(int i=1;i<*argc;i++)if(!strcmp(argv[i],"--modified-rom")){modified=1;for(int j=i;j<*argc;j++)argv[j]=argv[j+1];(*argc)--;i--;}
    if(modified){const char *path=NULL;for(int i=1;i+1<*argc;i++)if(!strcmp(argv[i],"--rom"))path=argv[i+1];FILE *f=NULL;
#ifdef _WIN32
        wchar_t wide[32768];if(path&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,32768))f=_wfopen(wide,L"rb");
#else
        if(path)f=fopen(path,"rb");
#endif
        if(!f){fprintf(stderr,"[DoomSNESconv] --modified-rom requires an accessible --rom file.\n");return 0;}
        if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long length=ftell(f);rewind(f);if(length!=0x200000&&length!=0x200200&&length!=0x400000&&length!=0x400200){fclose(f);return 0;}
        uint8_t *data=malloc((size_t)length);if(!data){fclose(f);return 0;}size_t got=fread(data,1,(size_t)length,f);fclose(f);size_t skip=(length&1023)==512?512:0,size=(size_t)length-skip;
        if(got!=(size_t)length||!DoomEditorRomCompatible(data+skip,size)){free(data);fprintf(stderr,"[DoomSNESconv] Unsupported modified Doom USA engine layout.\n");return 0;}
        uint8_t digest[32];sha256_compute(data+skip,size,digest);for(unsigned n=0;n<32;n++)snprintf(modified_sha+n*2,3,"%02x",digest[n]);snprintf(modified_crc,sizeof(modified_crc),"%08x",crc32_compute(data+skip,size));free(data);
    }
    for(int i=1;i<*argc;i++)if(!strcmp(argv[i],"--campaign")) {
        if(campaign_path[0] || i+1>=*argc || strlen(argv[i+1])>=sizeof(campaign_path)-1) {
            fprintf(stderr,"[DoomSNESconv] --campaign requires one campaign .dse path.\n");return 0;
        }
        strcpy(campaign_path,argv[i+1]);
        /* A converted ROM's matching sidecar contains its verified changes. */
        char *extension=strrchr(campaign_path,'.');
        if(extension && (!strcmp(extension,".sfc") || !strcmp(extension,".smc")))strcpy(extension,".dse");
        for(int j=i;j+2<=*argc;j++)argv[j]=argv[j+2];*argc-=2;i--;
    }
    return 1;
}
static FILE *OpenPatch(void) {
    if(modified_sha[0]&&!campaign_path[0])return NULL;
#ifdef _WIN32
    if(campaign_path[0]) {
        wchar_t campaign[32768];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,campaign_path,-1,campaign,32768))return NULL;
        return _wfopen(campaign,L"rb");
    }
    wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n || n>=32768)return NULL;wchar_t *slash=wcsrchr(path,L'\\');
    if(!slash || slash-path>32700)return NULL;wcscpy(slash+1,L"editor\\active.dse");return _wfopen(path,L"rb");
#else
    if(campaign_path[0])return fopen(campaign_path,"rb");
    const char *base=SDL_GetBasePath();char path[4096];snprintf(path,sizeof(path),"%seditor/active.dse",base?base:"");return fopen(path,"rb");
#endif
}
int DoomEditorPatchSelected(void) {FILE *f=OpenPatch();if(!f)return 0;fclose(f);return 1;}
int DoomEditorNetplay(int from_lobby,char *reason,size_t cap) {
    (void)from_lobby;if(!modified_sha[0]&&!DoomEditorPatchSelected())return 1;
    snprintf(reason,cap,"Modified ROMs and custom campaigns are local only. Launch the original ROM without an editor patch for online play.");return 0;
}
static void Fatal(const char *message) {fprintf(stderr,"[DoomSNESed] %s\n",message);SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"DoomSNESed level patch",message,NULL);exit(1);}
void DoomEditorRomLoaded(const uint8_t *rom,size_t size) {
    DoomRotationsNativeOverrides(NULL,NULL);
    free(pending);pending=NULL;FILE *f=OpenPatch();if(!f){if(campaign_path[0])Fatal("Cannot open the selected campaign patch. Keep its .dse sidecar beside the converted ROM.");return;}
    if(fseek(f,0,SEEK_END)) {fclose(f);Fatal("Cannot read editor patch.");}
    long length=ftell(f);rewind(f);
    if(length<76 || length>16*1024*1024) {fclose(f);Fatal("Invalid editor patch size. Select Original levels in DoomSNESed to disable it.");}
    uint8_t *patch=malloc((size_t)length);
    if(!patch) {fclose(f);Fatal("Unable to allocate editor patch memory.");}
    size_t read=fread(patch,1,(size_t)length,f);fclose(f);
    pending_size=DoomEditorPatchTargetSize(patch,read);pending=malloc(pending_size);
    if(!pending){free(patch);Fatal("Unable to allocate editor ROM image.");}
    if(size==0x200200||size==0x400200) {rom+=512;size-=512;}
    char error[256];int ok=read==(size_t)length && DoomEditorPatchApply(rom,size,patch,(size_t)length,pending,error,sizeof(error));free(patch);
    if(!ok) {free(pending);pending=NULL;Fatal(read==(size_t)length?error:"Editor patch read failed.");}
    DoomRotationsNativeOverrides(rom,pending);
    fprintf(stderr,"[DoomSNESed] verified level patch prepared\n");
}
void DoomEditorBeforeFrame(void) {
    if(!pending)return;if(!g_snes || !g_snes->cart || (g_snes->cart->romSize!=0x200000&&g_snes->cart->romSize!=0x400000))Fatal("Editor patch machine size mismatch.");
    Cart *cart=g_snes->cart;
    if(pending_size!=cart->romSize){uint8_t *image=realloc(cart->rom,pending_size);if(!image)Fatal("Unable to expand asset ROM.");cart->rom=image;cart->romSize=(uint32_t)pending_size;cart->romImageSize=(uint32_t)pending_size;if(cart->superfx){cart->superfx->rom=image;cart->superfx->rom_size=(uint32_t)pending_size;cart->superfx->rom_mask=(uint32_t)pending_size-1;}}
    g_rom=cart->rom;memcpy(cart->rom,pending,pending_size);free(pending);pending=NULL;
    fprintf(stderr,"[DoomSNESed] native level data applied before frame 1\n");
}
void DoomEditorShutdown(void) {free(pending);pending=NULL;}
static int OpenTool(const char *rom_path,int converter);
int DoomEditorOpen(const char *rom_path) {return OpenTool(rom_path,0);}
static int OpenTool(const char *rom_path,int converter) {
#ifdef _WIN32
    wchar_t exe[32768],rom[32768],command[65536];DWORD n=GetModuleFileNameW(NULL,exe,32768);
    if(!n || n>=32768)return 0;wchar_t *slash=wcsrchr(exe,L'\\');if(!slash || slash-exe>32700)return 0;wcscpy(slash+1,converter?L"DoomSNESconv.exe":L"DoomSNESed.exe");
    if(!rom_path || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,rom_path,-1,rom,32768))return 0;
    if(wcslen(exe)+wcslen(rom)+16>=65536)return 0;swprintf(command,65536,L"\"%ls\" --rom \"%ls\"",exe,rom);
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) {SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,converter?"DoomSNESconv":"DoomSNESed",converter?"Could not open DoomSNESconv.exe. Keep the converter beside the game.":"Could not open DoomSNESed.exe. Keep the editor beside the game.",NULL);return 0;}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 1;
#else
    (void)rom_path;return 0;
#endif
}
int DoomConverterOpen(const char *rom_path) {return OpenTool(rom_path,1);}
int DoomEditorPlayModified(const char *rom_path) {
#ifdef _WIN32
    wchar_t exe[32768],rom[32768],command[65536];
    if(!rom_path||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,rom_path,-1,rom,32768))return 0;
    FILE *f=_wfopen(rom,L"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long length=ftell(f);rewind(f);
    if(length!=0x200000&&length!=0x200200&&length!=0x400000&&length!=0x400200){fclose(f);return 0;}
    uint8_t *data=malloc(length);if(!data){fclose(f);return 0;}
    size_t read=fread(data,1,length,f),skip=(length&1023)==512?512:0;fclose(f);
    int valid=read==(size_t)length&&DoomEditorRomCompatible(data+skip,length-skip);free(data);
    if(!valid){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Modified Doom SNES ROM","This ROM changes or relocates the compiled USA engine. It cannot run natively with this build. Data-only modifications retaining the USA engine layout are supported.",NULL);return 0;}
    DWORD n=GetModuleFileNameW(NULL,exe,32768);if(!n||n>=32768||wcslen(exe)+wcslen(rom)+64>=65536)return 0;
    swprintf(command,65536,L"\"%ls\" --modified-rom --rom \"%ls\" --no-launcher",exe,rom);
    STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi))return 0;
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 1;
#else
    (void)rom_path;return 0;
#endif
}
int DoomEditorResetInstall(void) {
#ifdef _WIN32
    // Only the active patch is installed content. Projects, ROMs, exports and
    // saves are user files; preserve them. Keep the removed patch recoverable.
    wchar_t path[32768],backup[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);
    if(!n||n>=32768)return 0;wchar_t *slash=wcsrchr(path,L'\\');if(!slash||slash-path>32500)return 0;
    wcscpy(slash+1,L"editor\\active.dse");
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES){
        SYSTEMTIME t;GetSystemTime(&t);swprintf(backup,32768,L"%ls.reset-%04u%02u%02u-%02u%02u%02u-%03u.bak",path,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
        if(!MoveFileW(path,backup))return 0;
    }
    campaign_path[0]=0;modified_sha[0]=0;modified_crc[0]=0;free(pending);pending=NULL;return 1;
#else
    return 0;
#endif
}
