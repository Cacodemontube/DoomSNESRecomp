/* Frozen original pixel-cache flush used as a timing/RAM oracle. */
static void flush_pixel_reference(SuperFx *f, SuperFxPixelCache *p) {
  if (!p->bitpend) return;
  uint8_t x0 = (uint8_t)(p->offset << 3), y = (uint8_t)(p->offset >> 5);
  uint32_t a = pixel_address(f, x0, y);
  for (unsigned n = 0; n < bpp(f); n++) {
    uint32_t by = ((n >> 1) << 4) + (n & 1);
    uint8_t d = 0;
    for (unsigned x = 0; x < 8; x++) d |= ((p->data[x] >> n) & 1) << x;
    if (p->bitpend != 0xff) {
      step_clocks(f, f->clsr ? 5 : 6);
      d = (d & p->bitpend) | (gsu_read(f, a + by) & (uint8_t)~p->bitpend);
    }
    step_clocks(f, f->clsr ? 5 : 6);
    gsu_write(f, a + by, d);
  }
  p->bitpend = 0;
}
