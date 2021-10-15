#include "stdint.h"

struct Framebuffer {
   uint32_t *frameBufferAddress;
   uint32_t pixelsPerScanLine;
};

void PlotPixel(Framebuffer framebuffer, int x, int y, int pixelColor) {
   uint32_t yMemOffset                            = y * framebuffer.pixelsPerScanLine;
   framebuffer.frameBufferAddress[x + yMemOffset] = pixelColor;
}

extern "C" {
int kmain(Framebuffer framebuffer) {
   for (int x = 0; x < 300; x++) {
      for (int y = 0; y < 300; y++) { PlotPixel(framebuffer, x, y, 200); }
   }

   while (true)
      ;
}
}