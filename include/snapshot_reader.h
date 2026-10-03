#pragma once
#include <stdint.h>

// BearSSL may retain readable TLS records after TCP closes. A negative read,
// unlike connected()==false, means those records have also been exhausted.
template<class Client, class Payload, class Clock, class Online, class Idle>
const char* readSnapshotBody(Client& client, Payload& payload, unsigned expected,
                            uint32_t timeout, Clock now, Online online, Idle idle) {
  if (!payload.reserve(expected)) return "body_allocation";
  uint8_t chunk[256];
  uint32_t started = now();
  while (payload.length() < expected && static_cast<uint32_t>(now() - started) < timeout) {
    unsigned remaining = expected - payload.length();
    int count = client.read(chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk));
    if (count > 0) {
      if (!payload.concat(reinterpret_cast<const char*>(chunk), count)) return "body_allocation";
    } else if (count < 0 || !online()) {
      break;
    } else idle();
  }
  return payload.length() == expected ? nullptr : "incomplete_body";
}
