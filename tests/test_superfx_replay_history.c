#include "snes/superfx.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void Redirect(SuperFx *fx, uint32_t pc, void *context) {
    CHECK(pc == 1);
    ++*(unsigned *)context;
    superfx_set_reg(fx, 9, 0x1234);
    superfx_hook_redirect(fx, 2);
}
int main(void) {
    uint8_t *rom = calloc(65536, 1), *ram = calloc(65536, 1);
    uint8_t *a = calloc(65536, 1), *b = calloc(65536, 1);
    CHECK(rom && ram && a && b);
    /* PLOT, DEC X, ALT1, RPIX, STOP: exercises buffered pixel RAM writes. */
    const uint8_t program[] = {0x4c, 0xe1, 0x3d, 0x4c, 0x00};
    memcpy(rom, program, sizeof(program));
    SuperFx *fx = superfx_create(rom, 65536, ram, 65536);
    CHECK(fx);
    fx->colr = 3; fx->r[1].data = 4; fx->r[2].data = 3;
    superfx_cpu_write_io(fx, 0x301e, 0);
    superfx_cpu_write_io(fx, 0x301f, 0);
    fx->enhancement_mode = kSuperFxEnhancement_PresentationReplay;
    const SuperFx original = *fx;
    for (unsigned hooked = 0; hooked < 2; ++hooked) {
        SuperFx traced, fast;
        unsigned calls_a = 0, calls_b = 0;
        SuperFxReplayPcHook hook_a = {1, Redirect, &calls_a};
        SuperFxReplayPcHook hook_b = {1, Redirect, &calls_b};
        CHECK(superfx_replay_snapshot_with_history(fx, a, &traced, hooked ? &hook_a : NULL, hooked, true));
        CHECK(superfx_replay_snapshot_with_history(fx, b, &fast, hooked ? &hook_b : NULL, hooked, false));
        CHECK(memcmp(&traced.r, &fast.r, offsetof(SuperFx, instruction_count) - offsetof(SuperFx, r)) == 0);
        CHECK(memcmp(a, b, 65536) == 0);
        CHECK(traced.instruction_count == fast.instruction_count);
        CHECK(traced.pipeline_pc == fast.pipeline_pc);
        CHECK(calls_a == calls_b && calls_a == hooked);
        CHECK(memcmp(&original, fx, sizeof(original)) == 0);
        CHECK(memcmp(original.trace, fast.trace, sizeof(fast.trace)) == 0);
        CHECK(memcmp(original.trace, traced.trace, sizeof(traced.trace)) != 0);
        CHECK(!fast.pc_hooks && !fast.pc_hook_count && !fast.pc_hook_cap);
        CHECK(!superfx_replay_snapshot_with_history(fx, fx->ram, &fast, NULL, 0, false));
    }
    superfx_destroy(fx);
    free(rom); free(ram); free(a); free(b);
    return 0;
}
