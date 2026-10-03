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

## Build

```bash
python3 -m venv .venv
.venv/bin/pip install platformio==6.1.18
.venv/bin/pio run -e ultra
```

Output: `.pio/build/ultra/firmware.bin`. The main firmware has no compiled Wi-Fi
password, account password or device token. It uses saved ESP SDK Wi-Fi settings.
Runtime config lives in LittleFS and survives ordinary OTA updates. Version
1.0.9 is installed on the connected cube, with rotation 2 saved and standard
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
Red and white titles share exactly the same native glyph masks, baseline, size
and renderer. Only their color differs.

Version 1.0.5 also drains the complete bounded HTTPS body before parsing it.
During hardware checks, `HTTPClient::getString()` returned only 458 of 1505 bytes
on later polls; the new reader continues across TLS records even after TCP starts
closing, checks Content-Length, and rejects incomplete responses after 15 seconds.
TLS certificate/hostname validation and the 6144-byte body limit remain enabled.
Hardware verification of 1.0.5 received and rendered two full 1505-byte, five-task
snapshots without another restart, with over 38 KB free heap. An intervening
connection failure retained the previous snapshot and recovered automatically.

## Configure

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
