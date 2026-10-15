# Server-rendered themes: implementation plan

Status: implemented and verified locally on 2026-10-14. Napotim branch `codex/server-rendered-display-themes`; firmware branch `codex/universal-display-receiver`. See [verification results](server-themes-verification.md). Production deployment remains outside this task.

Current visual acceptance: the seven original user-selected prototypes
02/03/04/08/10/11/12 and exact pre-theme Classic. Generic draft compositions are
not the acceptance target. Fixed RGB565 originals and independent old C++ output
protect their appearance; firmware 2.0.1 supports their declared status positions.

## Ownership

Napotim owns the theme catalog, composition, typography, task selection and previews. ESP owns transport, validation, successful-update time, stale indication, rotation, brightness, sleep and provisioning. No theme-specific firmware or downloaded executable code.

## First release

1. Versioned binary 240×240 RGB565 frames, adaptive raw/RLE encoding, bounded sizes and CRC32. Pixel data and a small alpha atlas for `0123456789:` share one validated packet. A declared flat-background status region allows ESP to draw the actual successful-update timestamp using the theme's custom font, grey/red locally.
2. Separate authenticated v2 manifest and frame endpoints. Content IDs exclude polling timestamps; unchanged content does not trigger frame download. Every response still confirms fresh data and settings. Existing v1 endpoint remains compatible.
3. Complete frame download into a temporary LittleFS file; validate structure, CRC, run lengths and exact decoded pixel count before replacing the active file or touching the LCD. Only changed content is written. Retain last valid frame/status on failures; no task cache displayed across power cycles without fresh authorization.
4. Server catalog of approved layouts, synthetic previews, selection saved in existing device JSON settings (no database column/migration). Strict theme validation, tenant-scoped frame access and bounded rendering/cache costs.
5. Firmware with production v1 fallback when v2 returns 404, authenticated alternate API origin for isolated local testing, local diagnostics and screenshot export of committed frame plus status. Clear cache on re-pair/revocation. Keep OTA recovery and sleep behavior.
6. Tests: packet decoder corruption/truncation/overflow, long Ukrainian titles/empty lists/hidden counts, cross-tenant and revoked key access, stable frame IDs, settings/UI selection, unchanged polling, OTA and real device fault/recovery checks.
7. Demonstrate real application selection driving the physical ESP using synthetic development data; retain evidence. User clarified that production must not be touched: leave the ESP on the isolated local source and perform no production fallback test. Preserve original configuration for a later explicit restore.

## Release boundaries

Static themes first. Animation and partial-image updates are deferred until the complete-frame path is measured on hardware. No commit/push/PR or production application deployment is implied by local implementation. One reusable server renderer powers previews and device frames. Protocol limits and authoring rules are documented and enforced by a validation command.

## Hardware acceptance

Firmware builds within existing flash/RAM budgets. Every theme downloads and displays with matching content ID. Successful unchanged polls update the local grey timestamp. Failed poll keeps timestamp and paints it red. Truncated/corrupt frames never replace a committed frame. Subsequent valid poll recovers. Brightness/rotation/sleep continue to work. Reboot, re-pair and key revocation do not revive unauthorized cached tasks.
