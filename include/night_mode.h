#pragma once
#include "display_settings.h"

// Keep the ESP clock/network running: timed deep sleep requires GPIO16 -> RST.
// No wall-clock guesses after power loss. A bounded notice then a dark screen
// avoids lighting the room indefinitely while Wi-Fi/NTP recovers.
struct NightMode {
  enum State { Awake, SleepNotice, Sleeping, ClockNotice, ClockWait };
  static constexpr uint32_t SLEEP_NOTICE_MS = 4000, CLOCK_NOTICE_MS = 15000;
  State state = Awake;
  uint32_t entered = 0;

  bool blank() const { return state == Sleeping || state == ClockWait; }
  bool notice() const { return state == SleepNotice || state == ClockNotice; }

  bool update(const DisplaySettings& settings, bool configured, bool clockReady,
              int32_t localSecond, uint32_t now) {
    State next = Awake;
    if (configured && settings.sleepEnabled) {
      if (!clockReady) {
        next = state == ClockNotice || state == ClockWait ? state : ClockNotice;
        if (next == ClockNotice && now - entered >= CLOCK_NOTICE_MS && state == ClockNotice) next = ClockWait;
      } else if (sleepAt(settings, localSecond)) {
        next = state == SleepNotice || state == Sleeping ? state : SleepNotice;
        if (next == SleepNotice && now - entered >= SLEEP_NOTICE_MS && state == SleepNotice) next = Sleeping;
      }
    }
    if (next == state) return false;
    state = next; entered = now;
    return true;
  }
};
