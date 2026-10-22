#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef DOOM_SNESED_TEST
int DoomEditorConfigure(int *argc,char **argv);
const char *DoomEditorModifiedSha(void);
const char *DoomEditorModifiedCrc(void);
void DoomEditorRomLoaded(const uint8_t *rom,size_t size);
void DoomEditorBeforeFrame(void);
void DoomEditorShutdown(void);
int DoomEditorOpen(const char *rom_path);
int DoomConverterOpen(const char *rom_path);
int DoomEditorPlayModified(const char *rom_path);
int DoomEditorResetInstall(void);
int DoomEditorPatchSelected(void);
int DoomEditorNetplay(int from_lobby,char *reason,size_t cap);
#else
static inline void DoomEditorBeforeFrame(void) {}
static inline void DoomEditorShutdown(void) {}
#endif
#ifdef __cplusplus
}
#endif
