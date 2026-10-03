#pragma once
#include <stdint.h>

struct UpdateStatusChanges {
  bool label;
  uint8_t glyphs;
};

// The status is either empty or a fixed-width HH:MM. Keep the label independent
// of timestamp/color changes, and invalidate everything after a screen clear.
class UpdateStatusCache {
 public:
  UpdateStatusChanges update(const char* stamp, uint16_t color, bool force = false) {
    bool visible = stamp[0] != '\0';
    bool wasVisible = paintedStamp[0] != '\0';
    UpdateStatusChanges changes{(force && visible) || visible != wasVisible, 0};
    for (uint8_t i = 0; i < 5; ++i) {
      char next = visible ? stamp[i] : '\0';
      if ((force && visible) || next != paintedStamp[i] ||
          (visible && color != paintedColor)) changes.glyphs |= 1 << i;
      paintedStamp[i] = next;
    }
    paintedColor = color;
    return changes;
  }

 private:
  char paintedStamp[5] = {};
  uint16_t paintedColor = 0;
};
