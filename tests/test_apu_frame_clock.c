#include "apu_frame_clock.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
int main(void) {
    RtlApuFrameClock beam = {0}, ordinary = {0};
    /* A DMA overshoots one field by 1/4 and the next field repays it. */
    const uint64_t long_end = RTL_MASTER_CYCLES_PER_FRAME * 5 / 4;
    const uint64_t pair_end = RTL_MASTER_CYCLES_PER_FRAME * 2;
    rtl_apu_clock_begin(&beam, 0);
    rtl_apu_clock_begin(&ordinary, 0);
    CHECK(rtl_apu_clock_finish_beam(&beam, long_end) == 21360);
    CHECK(rtl_apu_clock_finish(&ordinary, long_end) == 21360);
    rtl_apu_clock_begin(&beam, long_end);
    rtl_apu_clock_begin(&ordinary, long_end);
    CHECK(rtl_apu_clock_finish_beam(&beam, pair_end) == 34176);
    CHECK(beam.last_duration == 12816);
    CHECK(rtl_apu_clock_finish(&ordinary, pair_end) == 38448);
    /* WAI/minimum and multi-field loader contracts remain intact. */
    rtl_apu_clock_begin(&ordinary, pair_end);
    CHECK(rtl_apu_clock_finish(&ordinary, pair_end) == 55536);
    rtl_apu_clock_begin(&ordinary, pair_end);
    CHECK(rtl_apu_clock_finish(&ordinary, pair_end + 3 * RTL_MASTER_CYCLES_PER_FRAME) == 106800);
    rtl_apu_clock_begin(&beam, pair_end);
    CHECK(rtl_apu_clock_finish_beam(&beam, pair_end) == 34176);
    CHECK(beam.last_duration == 0);
    return 0;
}
