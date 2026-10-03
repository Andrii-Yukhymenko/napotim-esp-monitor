#include <assert.h>
#include <stdio.h>
#include "update_status.h"

int main() {
  constexpr uint16_t muted = 0x9CF3, red = 0xF924;
  UpdateStatusCache cache;
  auto changes = cache.update("", muted);
  assert(!changes.label && changes.glyphs == 0);

  changes = cache.update("19:52", muted);
  assert(changes.label && changes.glyphs == 0b11111);
  changes = cache.update("19:52", muted);
  assert(!changes.label && changes.glyphs == 0);
  changes = cache.update("19:53", muted);
  assert(!changes.label && changes.glyphs == 0b10000);
  changes = cache.update("19:59", muted);
  assert(!changes.label && changes.glyphs == 0b10000);
  changes = cache.update("20:00", muted);
  assert(!changes.label && changes.glyphs == 0b11011); // Colon stays untouched.

  // Recolor the timestamp on failure/recovery, leaving the label in place.
  changes = cache.update("20:00", red);
  assert(!changes.label && changes.glyphs == 0b11111);
  changes = cache.update("20:00", red);
  assert(!changes.label && changes.glyphs == 0);
  changes = cache.update("20:01", muted);
  assert(!changes.label && changes.glyphs == 0b11111);
  changes = cache.update("20:01", muted, true); // Screen clear or rotation.
  assert(changes.label && changes.glyphs == 0b11111);

  changes = cache.update("23:59", muted);
  assert(!changes.label && changes.glyphs == 0b11010);
  changes = cache.update("00:00", muted);
  assert(!changes.label && changes.glyphs == 0b11011);
  changes = cache.update("", muted); // Reset pairing: remove the whole status.
  assert(changes.label && changes.glyphs == 0b11111);
  changes = cache.update("", muted, true);
  assert(!changes.label && changes.glyphs == 0);
  changes = cache.update("00:00", muted);
  assert(changes.label && changes.glyphs == 0b11111);

  puts("Update status: changed digits, rollover, colors, clearing and forced redraw checks passed.");
}
