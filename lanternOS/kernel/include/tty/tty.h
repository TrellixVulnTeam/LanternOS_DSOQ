#pragma once
#include <stdint.h>

struct Framebuffer {
   uint32_t *frameBufferAddress;
   uint32_t pixelsPerScanLine;
   uint32_t horizontalResolution;
   uint32_t verticalResolution;
};

struct FontFormat {
   void *FontBufferAddress;
   uint32_t numGlyphs;
   uint32_t glyphSizeInBytes;
   uint32_t glyphHeight;
   uint32_t glyphWidth;
};

class TTY {
   Framebuffer fb;
   FontFormat loadedFont;
};