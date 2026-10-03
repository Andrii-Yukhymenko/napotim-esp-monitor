# Напотім · Magic Cube

Custom firmware for GeekMagic SmallTV-Ultra (ESP8266 / ESP-12F, ST7789 240×240).
One quiet, static screen shows the six highest-ranked approved active tasks due
today or earlier. Each row shows task type, high priority, category and deadline.
High priority sorts first, then overdue deadlines, then ascending due time.
Tasks for a whole day sort at the end of that day. Hidden tasks are counted.

The firmware polls `https://napotim.duckdns.org/api/device/v1/today` once a minute,
validates TLS using ISRG Root X1 and NTP time, and only redraws changed rows.
The footer records successful receipt, validation and rendering. Network errors
retain the last snapshot with a stale indicator; an invalid/revoked key clears it.

Version 1.1.3 starts Wi-Fi/NTP before display initialization and filesystem loading,
so connection establishment overlaps the display reset delays and initial drawing.
It also removes repeated full-screen startup redraws and avoids clearing rows,
the header and footer again after a full-screen clear. Only changed startup text
is repainted. A terminal negative BearSSL read now rejects a truncated response
immediately instead of waiting out the remaining 15-second deadline. Positive
reads continue draining buffered TLS records even after TCP closes; a live slow
connection still gets the existing deadline. Host regression checks cover these
cases, Wi-Fi loss, allocation failures and timer rollover in
`bash scripts/test-snapshot-reader.sh`.

Authenticated `/status` includes a `timing` object: observed Wi-Fi/clock readiness,
the first request and successful rendering times measured from boot, and the most
recent HTTP, body read, cleanup, validation/rendering and complete poll durations,
all in milliseconds. These distinguish network waiting from firmware work without
storing task text or credentials. HTTPS certificate validation remains enabled.
On-device verification after authenticated OTA confirmed version 1.1.3 and five
rendered tasks on the first request: Wi-Fi observed ready at 10.232 s, clock at
11.561 s, GET started at 11.935 s and rendering finished at 22.539 s. HTTP took
6741 ms, body reading 74 ms, cleanup 2 ms and validation/rendering 3787 ms.
The complete HTTP 200 response contained 1782 bytes, with no TLS/body error,
RSSI -74 dBm and about 38 KB free heap. Brightness 50, rotation 2 and pairing
were preserved; successful receipt restored the normal 60-second polling interval.

Version 1.1.1 removes the startup polling delay: the first HTTPS request starts
as soon as both Wi-Fi and NTP time are ready. Until the first valid snapshot,
failed requests retry 3 seconds after completion instead of waiting 1–5 minutes.
Successful receipt restores the normal 60-second interval and later failures
retain the existing backoff. Reconnecting Wi-Fi triggers an immediate request.
The initial screen distinguishes Wi-Fi connection, clock synchronization, task
retrieval and waiting to retry; Wi-Fi readiness changes clear obsolete errors.
TLS validation and the existing 15-second network/body timeouts remain enabled.
`/status` also reports clock readiness, Wi-Fi RSSI and the current poll interval.
Run `bash scripts/test-poll-schedule.sh` to verify startup readiness, retry timing,
reconnection, normal polling and `millis()` rollover. Version 1.1.1 was installed
through authenticated OTA and verified on the physical cube: its first poll
received and rendered five tasks from a complete 1771-byte HTTP 200 response.
The first successful snapshot arrived approximately 26 seconds after the reboot
with RSSI -77 dBm. Brightness 50, rotation 2 and the saved pairing survived;
the poll interval returned to 60 seconds and free heap was about 39 KB.

## Build

```bash
python3 -m venv .venv
.venv/bin/pip install platformio==6.1.18
.venv/bin/pio run -e ultra
```

Output: `.pio/build/ultra/firmware.bin`. The main firmware has no compiled Wi-Fi
password, account password or device token. It uses saved ESP SDK Wi-Fi settings.
Runtime config lives in LittleFS and survives ordinary OTA updates. Version
1.1.3 is installed on the connected cube, with rotation 2 saved and standard
HTTP Digest login verified on the actual hardware. The device has completed a
certificate-validated HTTPS request with the real account key and rendered its
account snapshot.

Version 1.0.6 keeps six static rows and uses native Misc Fixed 7x14 title glyphs,
the larger size of the original 6x13 family. Its 10-pixel capitals and 7-pixel
lowercase retain the enlarged dimensions with crisp, uniform one-pixel strokes.
This replaces the fractional bitmap scaling from 1.0.3–1.0.5, which produced
uneven stroke widths on the LCD. The title font lives entirely in flash and no
title framebuffer is needed. See [font source and regeneration](fonts/README.md).
Checkboxes are gray, high-priority markers red, and purple is darker. Category
dots remove their pastel white component while preserving hue; near-neutral dots
stay gray. Metadata, last-success timestamp, brightness and orientation remain.
On-device verification of 1.0.6 received and rendered all five current tasks
from a complete 1505-byte HTTPS snapshot, with saved settings intact and over
39 KB free heap. All Ukrainian glyphs and row bounds were checked before upload.

Version 1.0.9 uses the same shared red (RGB565 0xF924) for overdue titles,
high-priority bars and overdue deadline text, at the owner's request.
In that version, red and white titles share exactly the same native glyph masks,
baseline, size and renderer; only their color differs.

Version 1.0.10 compensates for the thinner appearance of red titles observed on
the LCD. It adds a faint right edge (40% red over the background, RGB565 0x68A2)
inside each existing 7-pixel glyph cell. Original title pixels remain 0xF924;
white titles are pixel-for-pixel unchanged. Glyph height, baseline, character
advance, clipping, metadata and priority markers retain their previous values.
This is a first optical adjustment; its perceived weight needs review on the LCD.
The firmware build and a host comparison of all 357 glyphs passed. After OTA,
the cube reported version 1.0.10 and rendered all five tasks from a complete
1505-byte HTTPS response, with brightness 100 and rotation 2 preserved.

Version 1.0.5 also drains the complete bounded HTTPS body before parsing it.
During hardware checks, `HTTPClient::getString()` returned only 458 of 1505 bytes
on later polls; the new reader continues across TLS records even after TCP starts
closing, checks Content-Length, and rejects incomplete responses after 15 seconds.
TLS certificate/hostname validation and the 6144-byte body limit remain enabled.
Hardware verification of 1.0.5 received and rendered two full 1505-byte, five-task
snapshots without another restart, with over 38 KB free heap. An intervening
connection failure retained the previous snapshot and recovered automatically.

## Configure

Firmware 1.1.0 supports per-device brightness and rotation from Напотім →
Налаштування → Пристрої. Manual mode uses brightness 1–100%; scheduled mode
uses day/night levels and local start times with a fixed 15-minute transition.
The two starts must be at least 15 minutes apart, including across midnight.
Rotation values 0–3 correspond to 0°, 90°, 180° and 270°.

Settings arrive in the existing minute-by-minute task snapshot and are saved
only when configuration changes. On the first capable poll, the cube supplies
its existing brightness and rotation so an unconfigured server preserves them.
An explicit save in the app takes precedence over this initialization.
Old firmware ignores the additional fields; new firmware also accepts the old API.

The cube computes scheduled brightness every second without needing a live
server connection. It caches the account's UTC offset and up to four future
offset transitions covering approximately a year, so temporary outages do not
prevent seasonal clock changes. After a power loss, it uses manual brightness
until NTP restores the clock and a saved timezone is available. ESP8266 has no
battery-backed clock; a restart without internet cannot establish the time.
Changing a schedule or rotation can apply immediately; subsequent daily
transitions interpolate at PWM resolution without flash writes.

The local configuration page remains available for pairing and recovery.
Cloud-managed values replace local brightness/rotation changes on the next
successful poll. `/status` reports the saved display settings, effective
brightness and whether settings have been received from the server.

Run `bash scripts/test-display-settings.sh` for host checks covering fades,
midnight, reversed day/night periods, clock fallback and cached timezone changes.
Build with `.venv/bin/pio run -e ultra`. Firmware 1.1.0 is installed on the
physical cube through authenticated OTA. It booted with brightness 100 and
rotation 2 preserved and received/rendered all five current tasks.
The companion application change was merged in
[pull request #33](https://github.com/Andrii-Yukhymenko/napotim/pull/33)
and deployed successfully. On the physical cube, `/status` confirmed
`settings_remote: true`, manual brightness 100, rotation 2, five tasks,
and a complete validated 1770-byte HTTPS response with about 39 KB free heap.
The current mode remains manual; choose scheduled mode in the application.

The first boot displays the device IP and a unique configuration password.
Open that address, sign in as `admin`, and paste a key created in
Напотім → Налаштування → Пристрої. The account password never goes to the cube.
Set brightness (1–100) and rotation (0–3). The default brightness is 22%; rotation 2 turns the Ultra screen 180 degrees.
The local settings and firmware upload require HTTP Digest authentication.
Settings never return the token or Wi-Fi password. The device API is read-only;
the server stores only a hash of the random key, which expires after one year.

If Wi-Fi cannot connect after 30 seconds, the device opens a WPA2 setup network
`Napotim-Setup`, using the same unique password shown on screen. Configure Wi-Fi
at `192.168.4.1`. The setup network closes after Wi-Fi connects and a token is set.

## First install on Ultra

The connected unit was identified over `/v.json` as `SmallTV-Ultra`,
`Ultra-V9.0.54`, at `192.168.31.195`. Stock firmware has `/update` and accepts
`.bin` and `.bin.gz`; it does not expose a flash-backup endpoint. A full factory
backup requires UART. USB-C on this ESP8266 board provides power only.

The main image was installed successfully through the factory updater as a
gzip-compressed `.bin.gz`; the private loader was not needed. After the first
boot, Wi-Fi needed to be entered again through the setup network. The factory
configuration endpoint returns a masked password, so it cannot be imported.

For other units whose stock application slot is too small, a minimal private
loader can be built with `pio run -e loader`. Install the ordinary main image
immediately after it. The loader is an optional fallback, not the normal path.

The loader needs `include/private_wifi.h` (excluded from Git):

```cpp
#define PRIVATE_WIFI_SSID "your-network"
#define PRIVATE_WIFI_PASSWORD "your-password"
#define PRIVATE_LOADER_PASSWORD "a-unique-temporary-password"
```

For this installation, `include/private_setup.h` supplies a unique initial local
configuration password, kept in `.secrets/provision.json` for authenticated
installation checks. This header is optional and excluded from Git. Generic
builds create their own random password on the device. `/status` provides
authenticated diagnostics without returning any credentials or task text.

The loader binary contains private Wi-Fi credentials. A build with the optional
setup header contains the local admin password. Do not publish these images.
Secrets, binaries, logs and build outputs are excluded from Git.

## Connector source

Implemented in the existing `napotim` repository, branch `codex/esp-display`:
device management, a scoped compact API, Alembic migration and integration tests.
[Connector pull request #32](https://github.com/Andrii-Yukhymenko/napotim/pull/32)
passed all GitHub checks, was merged and deployed successfully. The real device
is now paired with the account through its saved read-only key.

## References

- [SmallTV hardware and pin mapping](https://giovi321.github.io/smalltv-mod/getting-started/hardware/)
- [Ultra first-install constraints](https://giovi321.github.io/smalltv-mod/getting-started/flashing/)
- [ESP8266 BearSSL validation and memory](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/bearssl-client-secure-class.html)

## ESP8266 memory and diagnostics

`include/font_flash.h` is force-included for the U8g2 font data and decoder.
U8g2_for_Adafruit_GFX 1.8.0 otherwise keeps Cyrillic fonts in RAM on ESP8266.
The override places them in flash and reads each byte with `pgm_read_byte`.
Moving both fonts frees about 9 KB of RAM for BearSSL while retaining its full
16 KB receive buffer and certificate/hostname validation. Linked font symbols
must have flash addresses (`0x402...`), rather than RAM addresses (`0x3ffe...`).

Authenticated `/status` includes reset reason, boot count, last poll stage before
a reset, clock, poll count, heap diagnostics, expected/received response byte
counts and a JSON/body error identifier. Stage IDs: 1 readiness, 2 TLS
client construction, 3 trust anchor setup, 4 HTTPS GET, 5 response, 6 JSON
validation, 7 rendering, 0 idle. The trace uses RTC memory, not repeated flash
writes. It contains no credentials or task text.
