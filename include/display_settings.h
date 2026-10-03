#pragma once
#include <stdint.h>

struct DisplaySettings {
  uint8_t brightness = 22, rotation = 2;
  bool scheduled = false;
  uint8_t dayBrightness = 30, nightBrightness = 5;
  uint16_t dayStart = 480, nightStart = 1320; // Minutes since local midnight.
};

inline bool settingsEqual(const DisplaySettings& a, const DisplaySettings& b) {
  return a.brightness == b.brightness && a.rotation == b.rotation && a.scheduled == b.scheduled &&
    a.dayBrightness == b.dayBrightness && a.nightBrightness == b.nightBrightness &&
    a.dayStart == b.dayStart && a.nightStart == b.nightStart;
}

struct OffsetTransition { int32_t at = 0, offset = 0; };

inline int32_t offsetAt(int32_t epoch, int32_t base, const OffsetTransition* changes, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    if (epoch < changes[i].at) break;
    base = changes[i].offset;
  }
  return base;
}

// Hundredths of a percent keep a 15-minute fade smooth at the PWM resolution.
inline uint16_t brightnessAt(const DisplaySettings& settings, int32_t localSecond, bool clockReady) {
  if (!settings.scheduled || !clockReady) return settings.brightness * 100;
  localSecond = (localSecond % 86400 + 86400) % 86400;
  int32_t sinceDay = (localSecond - settings.dayStart * 60 + 86400) % 86400;
  int32_t sinceNight = (localSecond - settings.nightStart * 60 + 86400) % 86400;
  bool day = sinceDay < sinceNight;
  int32_t elapsed = day ? sinceDay : sinceNight;
  int32_t from = (day ? settings.nightBrightness : settings.dayBrightness) * 100;
  int32_t to = (day ? settings.dayBrightness : settings.nightBrightness) * 100;
  return elapsed >= 900 ? to : from + (to - from) * elapsed / 900;
}
