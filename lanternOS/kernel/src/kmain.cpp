#include "libk/string.h"
#include "stdarg.h"
#include "stddef.h"
#include "stdint.h"
#include "tty/tty.h"

struct GlobalInitializers {
   uint64_t *ctorAddresses;
   int ctorCount;
   uint64_t *dtorAddresses;
   int dtorCount;
};

/**
 * @brief Draw a single pixel to the screen based on (x,y) coordinates.
 *
 * The (0,0) coordinate is the top-left pixel of the screen.
 *
 * @param fb: The framebuffer object that represents the linear buffer of pixels that we draw to.
 * @param x: The x coordinate of the pixel. Valid from 0 - framebuffer.horizontalResolution.
 * @param y: The y coordinate of the pixel. Valid from 0 - framebuffer.verticalResolution.
 * @param pixelColor: The color to fill the pixel with. Valid from 0x00000000 - 0xFFFFFFFF.
 *                    0xFF000000 represents blue value, 0x00FF0000 represents green value, 0x0000FF00
 *                    represents red.
 */
void PlotPixel(Framebuffer fb, int x, int y, int pixelColor) {
   // In order to write to a linear framebuffer of memory values as if it were a 2d array of pixels,
   // we need to multiply our desired y coordinate by the number of pixels per scanline. This will
   // "jump" us down y rows of pixels, positioning us at the 0th column of the yth row.
   uint32_t yMemOffset = y * fb.pixelsPerScanLine;
   // Once we are positioned at the right row in memory, we can just add x columns to the beginning
   // of that row to reach the desired pixel.
   // Note that we do not need to worry about the size of each pixel in memory (4 bytes), because we are
   // indexing the framebuffer as an array of 4 byte unsigned integers.
   fb.frameBufferAddress[yMemOffset + x] = pixelColor;
}

void SetTTYBGColor(Framebuffer fb, int pixelColor) {
   for (uint32_t x = 0; x < fb.horizontalResolution; x++) {
      for (uint32_t y = 0; y < fb.verticalResolution; y++) { PlotPixel(fb, x, y, pixelColor); }
   }
}

/**
 * @brief Places a single ASCII character at a specific cursor position on the screen.
 *
 * @param fb: The framebuffer object that represents the linear buffer of pixels that we draw to.
 * @param font: The font object that contains information on how to draw our loaded font.
 * @param charToPrint: The ASCII character to print to the screen.
 * @param charPosX: The column to place the character in. 0 is the left-most column.
 * @param charPosY: The row to place the character in. 0 is the top-most row.
 * @param foreground: The RGB foreground color for the character. Valid from 0x00000000 - 0xFFFFFFFF.
 * @param background: The RGB background color for the character. Valid from 0x00000000 - 0xFFFFFFFF.
 *
 * @todo: What is the valid range for charPosX and charPosY? IE, can I implement automatic text wrapping?
 */
void PutChar(Framebuffer fb, FontFormat font, unsigned short int charToPrint, int charPosX, int charPosY,
             uint32_t foreground, uint32_t background) {
   // Gets a pointer to the offset in memory where the glyph we want to print is stored.
   uint8_t *fontPtr = (uint8_t *)font.FontBufferAddress + charToPrint * font.glyphSizeInBytes;

   // This function is given the cursor position where the character should be printed. We need to tanslate
   // this into the actual pixel offset where the top-left corner of the glyph should begin on the screen.
   unsigned long pixelXOffset = charPosX * font.glyphWidth;
   unsigned long pixelYOffset = charPosY * font.glyphHeight;

   /**
    * This nested forloop checks the glyph in memory and determines, pixel by pixel,
    * whether a foreground or background pixel should be drawn.
    * Since our font is 10 pixels wide, each row of the glyph is encoded in 10 bits, which means we must
    * process two bytes of data for every row.
    * A bit of 0 means that corresponding pixel should be drawn as background color, while a bit of 1 means it
    * should be drawn as foreground color.
    */
   for (unsigned long y = pixelYOffset; y < pixelYOffset + font.glyphHeight; y++) {
      // We process the first byte by simply iterating over every bit inside it, and checking if it is set
      // to 1 or 0.
      for (unsigned long x = pixelXOffset; x < pixelXOffset + 8; x++) {
         if ((*fontPtr & (0b10000000 >> (x - pixelXOffset))) != 0) {
            PlotPixel(fb, x, y, foreground);
         } else {
            PlotPixel(fb, x, y, background);
         }
      }
      // We then increment the pointer to the next byte of data representing this row.
      fontPtr++;
      // This time, we only test the first two bits. The other 6 bits of the second byte are always 0.
      // We must be sure to add 8 to the x pixel coordinate, so that we are writing to pixels exactly where we
      // "left off" from the previous loop.
      for (unsigned long x = pixelXOffset; x < pixelXOffset + 2; x++) {
         if ((*fontPtr & (0b10000000 >> (x - pixelXOffset))) != 0) {
            PlotPixel(fb, x + 8, y, foreground);
         } else {
            PlotPixel(fb, x + 8, y, background);
         }
      }

      // Now that we have completed an entire row of the glyph, we increment to the pointer to the
      // first byte of the next row, and repeat the loop.
      fontPtr++;
   }
}

void Puts(const char *array, Framebuffer fb, FontFormat font, int posX, int posY, uint32_t fg, uint32_t bg) {
   int i            = 0;
   char charToPrint = *array;

   while (charToPrint != 0) {
      PutChar(fb, font, charToPrint, i + posX, posY, fg, bg);
      array       = array + 1;
      charToPrint = *array;
      i++;
   }
}

void kprintf(const char *format, ...) {
   va_list args;
   va_start(args, format);

   for (size_t i = 0; i < strlen(format); i++) {
      char c = format[i];

      if (c == '%') {
         switch (c) {
         case '%': break;
         }
      }
   }

   va_end(args);
}

typedef void (*global_ctor)(void);
void CallGlobalConstructors(GlobalInitializers initializers) {
   for (int i = 0; i < initializers.ctorCount; i++) {
      global_ctor constructor = (global_ctor)initializers.ctorAddresses[i];
      constructor();
   }
}

extern "C" {
int kmain(Framebuffer framebuffer, FontFormat fontFormat, GlobalInitializers initializers) {
   CallGlobalConstructors(initializers);
   SetTTYBGColor(framebuffer, 0x1a1a1a);
   Puts("Welcome to LanternOS.", framebuffer, fontFormat, 0, 0, 0xFFCC00, 0x1a1a1a);
   Puts("Copyright (c) 2021. Licensed under the MIT License.", framebuffer, fontFormat, 0, 1, 0xFFCC00,
        0x1a1a1a);

   while (true)
      ;
   return 0;
}
}