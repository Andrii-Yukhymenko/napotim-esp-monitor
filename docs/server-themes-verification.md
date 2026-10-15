# Local verification of server themes

Date: 2026-10-14. Firmware branch: `codex/universal-display-receiver`.
Napotim branch: `codex/server-rendered-display-themes`.
Initial implementation was local only. The subsequent user-requested review
authorizes committing and pushing the two feature branches; no production deploy
or pull request is part of this operation.

## HTTPS pairing follow-up: firmware 2.0.2, 2026-10-15

The user explicitly requested connecting the cube to their main Napotim account.
The authenticated local restore-source operation restored the saved pairing;
the device key was not extracted or printed. No server code or account settings
were changed during this setup.

The first real HTTPS run exposed TLS allocation error -1000 while downloading
the frame, followed by unreliable polling. Free the manifest's 4096-byte JSON
pool before the next handshake, copying all needed fields first. Store only the
active theme's actual bounded glyph atlas on the heap and transfer the prepared
atlas without an extra copy. Static RAM drops from 39464 to 36832 bytes.

The final 2.0.2 build was installed through authenticated local OTA. The real
HTTPS API supplied a Classic frame, followed by successful explicit refresh and
automatic minute polling with the same boot counter. The frame download count
remained one while the successful-update time advanced. TLS, frame and data
errors were empty/zero, the display was awake, and free heap was about 35 KB.
Brightness, rotation and night sleep remained controlled by the account.

All six C++ test scripts and the real U8g2 glyph test pass. Both the installed
private build and a public Ultra build without the private setup header pass.
Follow-up branch: `codex/https-frame-memory`. The earlier local-only test limits
below describe previous iterations, before this explicitly authorized HTTPS pairing.

## Pre-publication code review

Reviewed both repositories' changes from their pre-feature bases. Confirmed and
fixed the following cases:

1. A v2 404 cleared the accepted frame before the fallback v1 request succeeded.
   Clear it only after a complete valid v1 snapshot; retain frame/time and mark
   status red if fallback fails. Reuse the same revocation cleanup in both paths.
2. A stored compatible theme outside the eight-item catalog appeared as Classic
   in the HTML select while saving a different ID. Add an honest selected option,
   preserve its value on save and allow an explicit switch to a catalog theme.
3. Changing API origin accepted the existing key when it was entered again.
   Require a different key, and reject ambiguous leading-zero HTTP IPv4 octets
   that an inet_aton resolver could interpret as octal/public addresses.
4. Atlas file reads occurred after replacing the previous frame. Read into a
   bounded temporary heap buffer before promotion, then activate without another
   fallible file read. Allocation/read/commit failure retains the previous frame.
5. Empty status masks or identical status/background colors passed the firmware
   parser despite violating the server authoring contract. Reject both cases.

Validation: complete backend suite 642 passed, frontend 241 passed, design tools
9 passed, ESLint/build and shell syntax checks passed. Packet checks pass with
AddressSanitizer/UndefinedBehaviorSanitizer; all 64 server-generated packets pass
the decoder. Existing firmware checks and the public Ultra build pass.
`dev-check.sh` completed its code/test/build stages but stopped at Docker because
WSL integration does not provide the CLI. Both Compose configurations were then
validated successfully using the installed Windows Docker CLI, without starting
containers or deploying anything.

Real local ESP checks reproduce failed/successful fallback, v2 recovery and
same-key source-change rejection. All eight catalog themes pass pixel comparisons
after the receiver refactor. The device stays on the local source; no production
HTTPS requests were used. Branch pushes do not trigger this repository's or
Napotim's main-branch deployment workflow.

## Latest iteration: approved originals and exact classic, firmware 2.0.1

The user selected original prototypes 02,03,04,08,10,11,12 and requested the
pre-theme classic unchanged. These eight now form the browser catalog. The
previous generic 16-theme draft described below is historical; its other IDs
remain compatible with saved settings but are no longer offered in the catalog.

- Seven approved layouts match their original prototype PNGs with zero differing
  RGB565 pixels on matching synthetic inputs.
- Classic matches the actual pre-theme C++ renderer at `8c7b2cc` with zero differing
  pixels in six cases: normal, empty and long tasks, each with normal/red time.
  References were generated with the original native title masks and U8g2 decoder,
  not with the new Python implementation.
- 83 focused backend tests pass, including those visual regressions, packet
  scenarios, ownership/revocation and stable frame IDs. Frontend tests (240),
  design tests (9), ESLint and build pass. The initial complete 628-test backend
  run below predates the new visual tests; it was not relabeled as a current run.
- All 64 approved/compatibility scenario packets pass Python and the new C++
  validator. It now allows a declared flat status rectangle anywhere in bounds
  and rejects decorations under it, for both raw and RLE frames.
- Actual ESP OTA installed 2.0.1. All eight catalog themes transferred, retained
  boot count 38 through switching, and passed byte-exact frame/status comparison.
- Selecting Metro and Classic in the real local browser was verified on the
  device. Classic's original grey/red timestamp is at (205,5); a failed local
  request retained its precise timestamp and tasks, then recovered without
  downloading another frame. The device remains awake on Classic and the local API.
- Display memory remains bounded: about 547 KB program flash, 39 KB static RAM
  and 32–34 KB free heap after requests. Row rendering and unchanged-frame polling
  remain in use; there are no individual theme implementations in firmware.

Evidence for this iteration is the visualization directory's `faithful-hardware`
folder, including eight BMP/PNG exports, current browser screenshot, classic
fresh/offline pair and `hardware-results.json`. Software exports are not LCD
photographs. Golden fixture provenance is documented in Napotim's
`backend/tests/display_reference/README.md`.

## Environment

- Real SmallTV-Ultra ESP8266 with ST7789 240×240, firmware 2.0.0.
- Separate local backend on port 8011, frontend on 5191, restricted LAN relay on 8012.
- Separate UTF-8 PostgreSQL development and test databases on the existing local
  PostgreSQL service. Only synthetic tasks and a synthetic device key were used.
- ESP remains paired to the local API. Its previous source configuration is saved
  on the device for an explicit future restore; production fallback was never tested.

## Automated checks

| Check | Result |
| --- | --- |
| Complete backend pytest suite with real isolated PostgreSQL | 628 passed |
| Frontend suite, including catalog selection and save | 240 passed |
| Design tooling tests | 9 passed |
| Frontend ESLint and production build | Passed |
| Theme authoring validator: 16 themes × 4 scenarios | 64 packets passed |
| Same server packets through the firmware C++ decoder | 64 accepted |
| C++ corruption, truncation, bounds, RLE, blending and origin checks | Passed |
| Existing schedule, sleep, polling, response reader and status checks | Passed |
| Existing timestamp glyph test with actual U8g2 decoder | Passed |
| Public firmware build without private setup header | Passed |

Renderer scenarios cover normal, empty, one task and long Ukrainian titles.
Backend regressions cover stable content IDs, settings validation, status-area
overlap, device ownership, separate-account frame access and revoked keys.

## Real-device acceptance

- All 16 themes were transferred and compared byte for byte with the accepted
  server frame plus the ESP's actual locally painted timestamp.
- A sixteenth theme was added only on the server and accepted by the already
  installed receiver, with no OTA or restart for the theme addition.
- Unchanged successful polling retained the frame ID and download counter while
  advancing the successful-update time; no frame file rewrite occurred.
- An unavailable local server retained the previous frame and exact timestamp,
  with red digits. A subsequent successful request restored grey digits.
- The same freshness behavior worked with the theme's serif digit atlas.
- Corrupt and truncated transfers retained the old frame/time and recovered on a
  valid retry. Invalid manifest data was rejected before changing the frame/time.
- Rotation redrew the accepted scene. Sleep disabled the screen; a different
  theme downloaded during sleep without waking it. Disabling sleep restored it.
- Reboot while the server was unavailable did not display cached task pixels.
  A fresh authenticated request restored the scene.
- Revoking the synthetic key cleared the task frame and pairing. Explicit local
  re-pairing restored the scene.
- A local v2 404 exercised the existing v1 display path through the same local
  source. A manual refresh returned to v2 after the local endpoint recovered.

Fault testing found and fixed an ESP HTTPClient stream ownership issue and partial
writes during diagnostic BMP export. The final successful theme-switch run keeps
the boot counter unchanged. Free heap after requests remains around 32–34 KB.
The compiled program uses about 547 KB flash and 39 KB static RAM.

## Evidence and limits

The local settings page is `http://127.0.0.1:5191/settings`: expand Integrations,
open the test screen, choose a theme and save. Normal device synchronization is
once a minute. The demonstration leaves the Typographic theme selected, sleep
disabled and gateway fault mode reset to normal.

Artifacts are in the Codex visualization directory's `hardware-demo` folder:
`verified-themes.png`, `local-theme-settings.png`, `freshness-demo.png`, individual
BMP exports and `hardware-results.json`. These are synthetic task data.
BMP export reconstructs the frame and painted status from firmware state; it is
not a camera photo or electrical readback of the LCD.

Production HTTPS was deliberately not exercised: hardware acceptance used the
local HTTP source. The existing verified TLS transport and trust anchor remain
in the HTTPS path. This release supports static themes; animation and partial
frame updates are outside the current protocol.
