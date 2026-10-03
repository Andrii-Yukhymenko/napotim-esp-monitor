#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "snapshot_reader.h"

struct Payload {
  std::string text;
  bool canReserve = true, canAppend = true;
  bool reserve(unsigned size) { if (!canReserve) return false; text.reserve(size); return true; }
  unsigned length() const { return text.length(); }
  bool concat(const char* bytes, unsigned size) {
    if (!canAppend) return false;
    text.append(bytes, size); return true;
  }
};

struct Client {
  std::vector<int> reads;
  unsigned cursor = 0;
  bool terminal = true;
  int read(uint8_t* bytes, unsigned requested) {
    if (cursor == reads.size()) return terminal ? -1 : 0;
    int& step = reads[cursor];
    if (step <= 0) return reads[cursor++];
    unsigned count = static_cast<unsigned>(step) < requested ? step : requested;
    memset(bytes, 'x', count);
    step -= count;
    if (!step) ++cursor;
    return count;
  }
};

int main() {
  uint32_t clock = 0;
  auto now = [&]() { return clock; };
  auto idle = [&]() { ++clock; };
  auto online = []() { return true; };

  // Positive reads must be drained even after the network has disconnected:
  // buffered TLS records are still valid. Do not use TCP connected() as EOF.
  Client buffered{{300, 300}}; Payload complete;
  assert(!readSnapshotBody(buffered, complete, 600, 15000, now, []() { return false; }, idle));
  assert(complete.length() == 600 && clock == 0);

  // A gap between TLS records while Wi-Fi is up must not truncate the response.
  Client records{{2, 0, 3}}; Payload split;
  assert(!readSnapshotBody(records, split, 5, 15000, now, online, idle));
  assert(split.length() == 5 && clock == 1);

  // A terminal negative read after a truncated response must fail immediately,
  // instead of needlessly waiting the entire 15-second body timeout.
  Client closed{{2, -1}}; Payload truncated;
  const char* error = readSnapshotBody(closed, truncated, 5, 15000, now, online, idle);
  assert(error && std::string(error) == "incomplete_body");
  assert(truncated.length() == 2 && clock == 1);

  Client lostWifi{{0}}; Payload disconnected;
  error = readSnapshotBody(lostWifi, disconnected, 5, 15000, now, []() { return false; }, idle);
  assert(error && clock == 1);

  // A slow, still-open connection gets the full deadline, including rollover.
  Client stalled{{}, 0, false}; Payload waiting;
  clock = UINT32_MAX - 100;
  uint32_t started = clock;
  error = readSnapshotBody(stalled, waiting, 5, 300, now, online, idle);
  assert(error && static_cast<uint32_t>(clock - started) == 300);

  Client unused{{5}}; Payload noSpace;
  noSpace.canReserve = false;
  error = readSnapshotBody(unused, noSpace, 5, 15000, now, online, idle);
  assert(error && std::string(error) == "body_allocation" && unused.cursor == 0);
  Payload appendFailed; appendFailed.canAppend = false;
  error = readSnapshotBody(unused, appendFailed, 5, 15000, now, online, idle);
  assert(error && std::string(error) == "body_allocation");
  puts("Buffered TLS records, terminal EOF, Wi-Fi loss, body deadline, rollover and allocation checks passed.");
}
