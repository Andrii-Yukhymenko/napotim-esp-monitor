#pragma once

// U8g2_for_Adafruit_GFX 1.8.0 defaults to RAM and direct byte reads on ESP8266.
// Store fonts in flash and use aligned flash-safe byte reads instead.
#if defined(ESP8266) && !defined(__ASSEMBLER__)
#include <pgmspace.h>
#define U8X8_FONT_SECTION(name) __attribute__((section(".irom.text." name), aligned(4)))
#define u8x8_pgm_read(address) pgm_read_byte(address)
#endif
