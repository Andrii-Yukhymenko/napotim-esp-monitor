#include <assert.h>
#include <stdio.h>
#include "night_mode.h"

int main() {
  DisplaySettings s;
  assert(!sleepAt(s, 3600)); // Disabled for existing devices.
  s.sleepEnabled = true;
  for (int second = -86400; second < 172800; ++second) {
    int normalized = (second % 86400 + 86400) % 86400;
    assert(sleepAt(s, second) == (normalized < 8 * 3600));
  }
  s.sleepStart = 23 * 60;
  assert(!sleepAt(s, 23 * 3600 - 1));
  assert(sleepAt(s, 23 * 3600));
  assert(sleepAt(s, 8 * 3600 - 1));
  assert(!sleepAt(s, 8 * 3600));
  s.dayStart = 20 * 60; s.sleepStart = 6 * 60; // A daytime sleep interval.
  for (int second = 0; second < 86400; ++second)
    assert(sleepAt(s, second) == (second >= 6 * 3600 && second < 20 * 3600));
  s.scheduled = true; s.nightStart = 2 * 60;
  assert(validSleepSchedule(s));
  s.nightStart = 7 * 60; assert(!validSleepSchedule(s));
  s = DisplaySettings(); s.sleepEnabled = true;
  s.sleepStart = s.dayStart; assert(!validSleepSchedule(s)); assert(!sleepAt(s, 0));
  s.sleepStart = 0;

  // Power-on at 01:00 with a valid clock shows a notice, then stays dark.
  NightMode n;
  assert(n.update(s, true, true, 3600, 10)); assert(n.state == NightMode::SleepNotice);
  assert(!n.update(s, true, true, 3600, 4009));
  assert(n.update(s, true, true, 3600, 4010)); assert(n.blank());
  assert(!n.update(s, true, true, 4 * 3600, 100000)); // No network input needed.
  assert(n.update(s, true, true, 8 * 3600, 100001)); assert(n.state == NightMode::Awake);
  assert(!n.update(s, true, true, 8 * 3600, 100002));

  // Power outage loses the clock. No assumption that the last saved time is now.
  n = NightMode();
  assert(n.update(s, true, false, 0, 100)); assert(n.state == NightMode::ClockNotice);
  assert(!n.update(s, true, false, 0, 15099));
  assert(n.update(s, true, false, 0, 15100)); assert(n.state == NightMode::ClockWait);
  assert(!n.update(s, true, false, 0, 500000));
  assert(n.update(s, true, true, 3600, 500001)); assert(n.state == NightMode::SleepNotice);
  assert(n.update(s, true, true, 3600, 504001)); assert(n.state == NightMode::Sleeping);
  n = NightMode();
  n.update(s, true, false, 0, 0); n.update(s, true, false, 0, 15000);
  assert(n.update(s, true, true, 12 * 3600, 16000)); assert(n.state == NightMode::Awake);

  // Settings remain serviceable while asleep; disabling or unpairing wakes it.
  n.update(s, true, true, 0, 20000); n.update(s, true, true, 0, 24000);
  s.sleepEnabled = false;
  assert(n.update(s, true, true, 0, 25000)); assert(n.state == NightMode::Awake);
  s.sleepEnabled = true;
  n.update(s, true, false, 0, 26000);
  assert(n.update(s, false, false, 0, 27000)); assert(n.state == NightMode::Awake);

  // Notice timeout also works across millis() overflow (~49 days).
  n = NightMode(); n.update(s, true, true, 0, UINT32_MAX - 2000);
  assert(!n.update(s, true, true, 0, 1998));
  assert(n.update(s, true, true, 0, 1999)); assert(n.blank());

  // Cached timezone transitions: spring gap wakes; repeated fall hour sleeps.
  OffsetTransition spring[] = {{1806195600, 10800}};
  s.dayStart = 4 * 60;
  assert(sleepAt(s, (1806195599 + offsetAt(1806195599, 7200, spring, 1)) % 86400));
  assert(!sleepAt(s, (1806195600 + offsetAt(1806195600, 7200, spring, 1)) % 86400));
  OffsetTransition fall[] = {{1792890000, 7200}};
  s.dayStart = 8 * 60;
  assert(sleepAt(s, (1792889999 + offsetAt(1792889999, 10800, fall, 1)) % 86400));
  assert(sleepAt(s, (1792890000 + offsetAt(1792890000, 10800, fall, 1)) % 86400));
  puts("Night sleep, offline wake, power loss, DST, settings changes and overflow passed.");
}
