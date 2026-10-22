#pragma once
#include <stddef.h>
#include <stdint.h>
/* target must hold DoomEditorPatchTargetSize bytes and must not alias source. */
size_t DoomEditorPatchTargetSize(const uint8_t *patch,size_t patch_size);
int DoomEditorRomCompatible(const uint8_t *rom,size_t size);
int DoomEditorPatchApply(const uint8_t *source,size_t source_size,
    const uint8_t *patch,size_t patch_size,uint8_t *target,char *error,size_t cap);
