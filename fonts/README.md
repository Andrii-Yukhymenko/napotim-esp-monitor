# Native title font

`7x14.bdf` is the native 7x14 size of Misc Fixed, the same family as the initial
6x13 title font. Its cap height is 10 pixels and x-height 7 pixels, matching the
enlarged design without resampling or irregularly doubled strokes. Glyph cells
are 7x14 with a 7-pixel advance. Ukrainian letters are included.

Source: https://raw.githubusercontent.com/olikraus/u8g2/master/tools/font/bdf/7x14.bdf

SHA256: `5086d6adcd78f57de8d768cdf7d4416eb669138fc0b1f4d75f5a505bbf7d5281`

The BDF declares: `Public domain font. Share and enjoy.` The vendored file makes
the export reproducible without downloading or running an external converter.

Run `python3 scripts/make-title-font.py` to regenerate `include/title_font.h`.
Only printable ASCII and Cyrillic glyphs are embedded, all in flash.
