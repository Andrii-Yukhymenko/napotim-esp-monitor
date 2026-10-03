#include <assert.h>
#include <stdio.h>
#include "display_settings.h"

int main() {
  DisplaySettings settings;
  settings.brightness = 77;
  assert(brightnessAt(settings, 12 * 3600, true) == 7700);
  settings.scheduled = true;
  assert(brightnessAt(settings, 0, false) == 7700); // Reboot before NTP.
  assert(brightnessAt(settings, 8 * 3600 - 1, true) == 500);
  assert(brightnessAt(settings, 8 * 3600, true) == 500);
  assert(brightnessAt(settings, 8 * 3600 + 450, true) == 1750);
  assert(brightnessAt(settings, 8 * 3600 + 900, true) == 3000);
  assert(brightnessAt(settings, 22 * 3600, true) == 3000);
  assert(brightnessAt(settings, 22 * 3600 + 450, true) == 1750);
  assert(brightnessAt(settings, 22 * 3600 + 900, true) == 500);
  assert(brightnessAt(settings, 0, true) == 500);
  for (int elapsed = 0; elapsed < 900; ++elapsed) {
    assert(brightnessAt(settings, 8 * 3600 + elapsed + 1, true) >= brightnessAt(settings, 8 * 3600 + elapsed, true));
    assert(brightnessAt(settings, 22 * 3600 + elapsed + 1, true) <= brightnessAt(settings, 22 * 3600 + elapsed, true));
  }
  // A night transition crosses midnight without restarting its fade.
  settings.nightStart = 23 * 60 + 55;
  assert(brightnessAt(settings, 0, true) == 2167);
  assert(brightnessAt(settings, 10 * 60, true) == 500);
  assert(brightnessAt(settings, -1, true) == brightnessAt(settings, 86399, true));
  // A night-shift owner can make daytime span midnight.
  settings.dayStart = 20 * 60; settings.nightStart = 6 * 60;
  assert(brightnessAt(settings, 0, true) == 3000);
  assert(brightnessAt(settings, 12 * 3600, true) == 500);
  // Equal levels and reversed levels are valid, and always stay bounded.
  settings.dayBrightness = 1; settings.nightBrightness = 100;
  for (int second = 0; second < 86400; ++second) {
    int level = brightnessAt(settings, second, true);
    assert(level >= 100 && level <= 10000);
  }
  settings.dayBrightness = settings.nightBrightness = 15;
  assert(brightnessAt(settings, 20 * 3600 + 200, true) == 1500);
  // Cached offset changes survive an outage and apply exactly at their epoch.
  OffsetTransition changes[] = {{1792890000, 7200}, {1806195600, 10800}};
  assert(offsetAt(1792889999, 10800, changes, 2) == 10800);
  assert(offsetAt(1792890000, 10800, changes, 2) == 7200);
  assert(offsetAt(1806195600, 10800, changes, 2) == 10800);
  assert(offsetAt(1792890000, -18000, changes, 0) == -18000);
  puts("Display schedule, midnight, clock fallback and cached DST checks passed.");
}
