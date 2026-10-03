"""Exercise the firmware renderer with the real U8g2 decoder and font.

Run after a PlatformIO build, which installs the pinned library dependencies.
The host graphics adapter records pixels instead of sending them over SPI.
"""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
library = root / '.pio/libdeps/ultra/U8g2_for_Adafruit_GFX/src'
source = (root / 'src/main.cpp').read_text()
renderer = source[source.index('constexpr int UPDATE_GLYPH_WIDTH'):source.index('void drawUpdateStatus(')]
# Make the old uninitialized background failure deterministic. Correct code
# must ignore or replace a stale background after setFont() selects opaque mode.
renderer = renderer.replace('glyphFont.begin(canvas);',
                            'glyphFont.begin(canvas); glyphFont.setBackgroundColor(0xF924);')
font_source = (library / 'u8g2_fonts.c').read_text(encoding='latin1')
font = re.search(r'const uint8_t u8g2_font_6x12_t_cyrillic\[\d+\][^=]+='
                 r'\s*(?:"(?:[^"\\]|\\.)*"\s*)+;', font_source)[0] + '\n'

adapter = r'''
#pragma once
#include <cstddef>
#include <cstdint>
class Print {
 public:
  virtual size_t write(uint8_t) = 0;
};
class Adafruit_GFX : public Print {
 public:
  Adafruit_GFX(int16_t w, int16_t h) : width(w), height(h) {}
  virtual void drawPixel(int16_t, int16_t, uint16_t) = 0;
  size_t write(uint8_t) override { return 1; }
  void fillScreen(uint16_t color) {
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) drawPixel(x, y, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t n, uint16_t color) {
    for (int i = 0; i < n; ++i) drawPixel(x + i, y, color);
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t n, uint16_t color) {
    for (int i = 0; i < n; ++i) drawPixel(x, y + i, color);
  }
 private:
  int16_t width, height;
};
'''
prefix = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <U8g2_for_Adafruit_GFX.h>
constexpr uint16_t BG = 0x0841;
struct Screen {
  std::array<uint16_t, 72> pixels;
  void drawRGBBitmap(int x, int y, uint16_t* data, int w, int h) {
    assert(x == 205 && y == 5 && w == 6 && h == 12);
    std::copy(data, data + 72, pixels.begin());
  }
} tft;
'''
checks = r'''
int main() {
  for (uint16_t color : {uint16_t(0x9CF3), uint16_t(0xF924)}) {
    for (char glyph : "0123456789: ") {
      if (!glyph) continue;
      drawUpdateGlyph(205, glyph, color);
      UpdateGlyphCanvas expected;
      expected.fillScreen(BG);
      U8G2_FOR_ADAFRUIT_GFX reference;
      reference.begin(expected);
      reference.setFont(u8g2_font_6x12_t_cyrillic);
      reference.setBackgroundColor(BG);
      reference.setForegroundColor(color);
      reference.drawGlyph(0, 10, glyph);
      int foreground = 0, background = 0;
      for (int i = 0; i < 72; ++i) {
        assert(tft.pixels[i] == expected.pixels[i]);
        assert(tft.pixels[i] == BG || tft.pixels[i] == color);
        foreground += tft.pixels[i] == color;
        background += tft.pixels[i] == BG;
      }
      assert(background > 0);
      assert(glyph == ' ' ? foreground == 0 : foreground > 0);
    }
  }
  puts("Real U8g2 glyphs: normal/error colors, background pixels and blank cells passed.");
}
'''
with tempfile.TemporaryDirectory(prefix='napotim-update-glyph-') as directory:
    temporary = Path(directory)
    (temporary / 'Adafruit_GFX.h').write_text(adapter)
    cpp = temporary / 'test.cpp'
    cpp.write_text(prefix + font + renderer + checks)
    binary = temporary / 'test'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(temporary), '-I', str(library), str(cpp),
                    str(library / 'U8g2_for_Adafruit_GFX.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
