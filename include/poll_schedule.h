#pragma once
#include <stdint.h>

struct PollSchedule {
  static constexpr uint32_t NORMAL_INTERVAL = 60000;
  static constexpr uint32_t STARTUP_RETRY = 3000;
  static constexpr uint32_t MAX_RETRY = 300000;
  uint32_t next = 0, interval = NORMAL_INTERVAL;
  bool ready = false;

  void observeReadiness(bool current, uint32_t now) {
    // Wi-Fi/NTP becoming ready must bypass a previously scheduled wait.
    if (current && !ready) next = now;
    ready = current;
  }

  bool due(uint32_t now) const {
    return ready && static_cast<int32_t>(now - next) >= 0;
  }

  void retry(bool hasSnapshot) {
    interval = !hasSnapshot ? STARTUP_RETRY :
      (interval >= MAX_RETRY / 2 ? MAX_RETRY : interval * 2);
  }

  void scheduleFrom(uint32_t now) { next = now + interval; }
};
