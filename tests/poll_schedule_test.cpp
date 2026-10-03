#include <assert.h>
#include <stdio.h>
#include "poll_schedule.h"

int main() {
  PollSchedule schedule;
  // The old firmware attempted at 3 s and then slept until 63 s. A network
  // connecting at 5 s must now produce the first request at 5 s.
  schedule.observeReadiness(false, 3000);
  assert(!schedule.due(3000));
  schedule.observeReadiness(true, 5000);
  assert(schedule.due(5000));

  // A failed first HTTPS request retries after 3 s, even if Wi-Fi stays up.
  schedule.retry(false);
  schedule.scheduleFrom(7000);
  schedule.observeReadiness(true, 8000);
  assert(!schedule.due(9999));
  assert(schedule.due(10000));
  schedule.retry(false);
  schedule.scheduleFrom(12000);
  assert(!schedule.due(14999));
  assert(schedule.due(15000));

  // Wait for NTP as well as Wi-Fi, then request immediately on readiness.
  schedule.observeReadiness(false, 16000);
  assert(!schedule.due(63000));
  schedule.observeReadiness(true, 64000);
  assert(schedule.due(64000));

  // Successful synchronization restores minute polling; repeated observation
  // of the ready state must not trigger a tight polling loop.
  schedule.interval = PollSchedule::NORMAL_INTERVAL;
  schedule.scheduleFrom(65000);
  schedule.observeReadiness(true, 65001);
  assert(!schedule.due(124999));
  assert(schedule.due(125000));

  // Keep outage backoff after obtaining a snapshot, but bypass it on reconnect.
  schedule.retry(true); assert(schedule.interval == 120000);
  schedule.retry(true); assert(schedule.interval == 240000);
  schedule.retry(true); assert(schedule.interval == 300000);
  schedule.retry(true); assert(schedule.interval == 300000);
  schedule.scheduleFrom(130000);
  schedule.observeReadiness(false, 131000);
  assert(!schedule.due(430000));
  schedule.observeReadiness(true, 432000);
  assert(schedule.due(432000));

  // millis() rollover must not cause an early request or a multi-day wait.
  schedule.interval = PollSchedule::STARTUP_RETRY;
  schedule.scheduleFrom(UINT32_MAX - 1000);
  assert(!schedule.due(UINT32_MAX));
  assert(!schedule.due(1998));
  assert(schedule.due(1999));
  puts("Startup Wi-Fi/NTP readiness, retries, reconnect, minute polling and rollover checks passed.");
}
