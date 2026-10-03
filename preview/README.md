# Bitmap previews

`fonts.json` contains glyph metrics and row bitmaps exported from the actual
U8g2 Cyrillic fonts used by the display. Regenerate after a firmware build:

```bash
python3 scripts/make-preview-fonts.py
```

Each glyph is `[width, height, x_offset, y_offset, advance, rows]`.
Rows use the least significant bit for the leftmost pixel. The atlas includes
6×12 metadata and 6×13 header fonts; 7×13 is retained for comparison.
Current task titles use the native 7×14 masks in `include/title_font.h`, generated
from the vendored BDF under `fonts/`.

`native-title-check.png` illustrates native title glyphs with synthetic tasks.
The current screen layout is illustrated in `docs/screen-preview.png`.
These are software previews; physical LCD brightness and pixel density differ.
Fonts are public-domain Misc Fixed. Third-party notices are in `LICENSE-fonts.txt`.
