#include "tty/tty.h"

#include <stdarg.h>

#include "libk/ctype.h"
#include "libk/stdlib.h"
#include "libk/string.h"

TTY::TTY(Framebuffer fb, FontFormat font) {
   m_framebuf   = fb;
   m_loadedFont = font;

   m_numCharCols = m_framebuf.horizontalResolution / m_loadedFont.glyphWidth;
   m_numCharRows = m_framebuf.verticalResolution / m_loadedFont.glyphHeight;
}

uint32_t TTY::GetPixelColor(uint32_t posX, uint32_t posY) {
   uint32_t yMemOffset = posY * m_framebuf.pixelsPerScanLine;
   return m_framebuf.frameBufferAddress[yMemOffset + posX];
}

void TTY::NewLine() {
   m_currentCharPosX = 0;
   m_currentCharPosY++;
}

void TTY::PlotPixel(uint32_t x, uint32_t y, uint32_t pixelColor) {
   uint32_t yMemOffset = y * m_framebuf.pixelsPerScanLine;
   // Note that we do not need to worry about the size of each pixel in memory (4 bytes), because we are
   // indexing the framebuffer as an array of 4 byte unsigned integers.
   m_framebuf.frameBufferAddress[yMemOffset + x] = pixelColor;
}

void TTY::SetBackgroundColor(uint32_t pixelColor) {
   if (m_bgColor == m_fgColor) {
      for (uint32_t x = 0; x < m_framebuf.horizontalResolution; x++) {
         for (uint32_t y = 0; y < m_framebuf.verticalResolution; y++) { PlotPixel(x, y, pixelColor); }
      }
   } else {
      for (uint32_t x = 0; x < m_framebuf.horizontalResolution; x++) {
         for (uint32_t y = 0; y < m_framebuf.verticalResolution; y++) {
            if (GetPixelColor(x, y) != m_fgColor) {
               PlotPixel(x, y, pixelColor);
            }
         }
      }
   }

   m_bgColor = pixelColor;
}

void TTY::SetForegroundColor(uint32_t pixelColor) {
   for (uint32_t x = 0; x < m_framebuf.horizontalResolution; x++) {
      for (uint32_t y = 0; y < m_framebuf.verticalResolution; y++) {
         if (GetPixelColor(x, y) == m_fgColor) {
            PlotPixel(x, y, pixelColor);
         }
      }
   }
   m_fgColor = pixelColor;
}

void TTY::ClearScreen() {
   for (uint32_t x = 0; x < m_framebuf.horizontalResolution; x++) {
      for (uint32_t y = 0; y < m_framebuf.verticalResolution; y++) { PlotPixel(x, y, m_bgColor); }
   }
}

void TTY::PutChar(uint8_t charToPrint, uint32_t foreground, uint32_t background) {
   // Handle control characters.
   if (charToPrint == '\n') {
      NewLine();
      return;
   }

   // Gets a pointer to the offset in memory where the glyph we want to print is stored.
   uint8_t *fontPtr = (uint8_t *)m_loadedFont.FontBufferAddress + charToPrint * m_loadedFont.glyphSizeInBytes;

   if (m_currentCharPosX > m_numCharCols - 1) {
      NewLine();
   }
   unsigned long pixelXOffset = m_currentCharPosX * m_loadedFont.glyphWidth;
   unsigned long pixelYOffset = m_currentCharPosY * m_loadedFont.glyphHeight;

   /**
    * This nested forloop checks the glyph in memory and determines, pixel by pixel,
    * whether a foreground or background pixel should be drawn.
    * Since our font is 10 pixels wide, each row of the glyph is encoded in 10 bits, which means we must
    * process two bytes of data for every row.
    * A bit of 0 means that corresponding pixel should be drawn as background color, while a bit of 1 means it
    * should be drawn as foreground color.
    */
   for (unsigned long y = pixelYOffset; y < pixelYOffset + m_loadedFont.glyphHeight; y++) {
      // We process the first byte by simply iterating over every bit inside it, and checking if it is set
      // to 1 or 0.
      for (unsigned long x = pixelXOffset; x < pixelXOffset + 8; x++) {
         if ((*fontPtr & (0b10000000 >> (x - pixelXOffset))) != 0) {
            PlotPixel(x, y, foreground);
         } else {
            PlotPixel(x, y, background);
         }
      }
      // We then increment the pointer to the next byte of data representing this row.
      fontPtr++;
      // This time, we only test the first two bits. The other 6 bits of the second byte are always 0.
      // We must be sure to add 8 to the x pixel coordinate, so that we are writing to pixels exactly where we
      // "left off" from the previous loop.
      for (unsigned long x = pixelXOffset; x < pixelXOffset + 2; x++) {
         if ((*fontPtr & (0b10000000 >> (x - pixelXOffset))) != 0) {
            PlotPixel(x + 8, y, foreground);
         } else {
            PlotPixel(x + 8, y, background);
         }
      }

      // Now that we have completed an entire row of the glyph, we increment to the pointer to the
      // first byte of the next row, and repeat the loop.
      fontPtr++;
   }

   m_currentCharPosX++;
}

void TTY::PutChar(uint8_t charToPrint) {
   PutChar(charToPrint, m_fgColor, m_bgColor);
}

void TTY::Puts(const char *array) {
   Puts(array, m_fgColor, m_bgColor);
}

void TTY::Puts(const char *array, uint32_t fg, uint32_t bg) {
   int i            = 0;
   char charToPrint = *array;

   while (charToPrint != 0) {
      PutChar(charToPrint, fg, bg);
      array       = array + 1;
      charToPrint = *array;
      i++;
   }
}

void TTY::PrintFormattedWithModifiers(const char *str, long paddingAmount, long precisionAmount,
                                      bool leftAdjusted, const char *alternateFormStr) {
   int numSpaces                 = 0;
   int numZeroes                 = 0;
   bool printLeadingZeroForOctal = false;
   bool printLeadingHexSign      = false;

   if (precisionAmount >= strlen(str)) {
      numZeroes = precisionAmount - strlen(str);
   }

   if (paddingAmount > strlen(str) + numZeroes) {
      numSpaces = paddingAmount - (strlen(str) + numZeroes);
   }

   if (str[0] == '-') {
      numZeroes--;
      numSpaces--;
   }

   if (alternateFormStr != nullptr) {
      // TODO replace with strcmp
      if (alternateFormStr[0] == '0' && strlen(alternateFormStr) == 1) {
         if (str[0] != '0' && numZeroes == 0) {
            printLeadingZeroForOctal = true;
         }
      }
      if (alternateFormStr[0] == '0' && alternateFormStr[1] == 'x') {
         printLeadingHexSign = true;
      }
   }

   if (!leftAdjusted) {
      for (int i = 0; i < numSpaces; i++) { PutChar(' '); }
      if (printLeadingHexSign) {
         Puts("0x");
      }
      for (int i = 0; i < numZeroes; i++) { PutChar('0'); }
      if (printLeadingZeroForOctal) {
         PutChar('0');
      }
      Puts(str);
   } else {
      if (printLeadingHexSign) {
         Puts("0x");
      }
      for (int i = 0; i < numZeroes; i++) { PutChar('0'); }
      if (printLeadingZeroForOctal) {
         PutChar('0');
      }
      Puts(str);
      for (int i = 0; i < numSpaces; i++) { PutChar(' '); }
   }
}

// TODO: Implement
void TTY::kprintf(const char *format, ...) {
   va_list args;
   va_start(args, format);

   long totalPadding   = 0;
   long totalPrecision = 0;
   bool leftAdjusted   = false;

   size_t bufPos = 0;
   while (format[bufPos] != '\0') {
      if (format[bufPos] == '%') {
         bufPos++;

         // Handle potential flags.

         while (format[bufPos] == '-' || format[bufPos] == '#' || format[bufPos] == '0' ||
                format[bufPos] == ' ' || format[bufPos] == '+') {
            if (format[bufPos] == '-') {
               leftAdjusted = true;
            }

            bufPos++;
         }

         // Determine requested padding, if any.
         if (isdigit(format[bufPos]) && format[bufPos] != '0') {
            char *end    = NULL;
            totalPadding = strtol(&(format[bufPos]), &end, 10);
            bufPos       = bufPos + (end - &(format[bufPos]));
         }

         // Determine requested precision, if any.
         if (format[bufPos] == '.') {
            bufPos++;
            char *end      = NULL;
            totalPrecision = strtol(&(format[bufPos]), &end, 10);
            bufPos         = bufPos + (end - &(format[bufPos]));
         }

         switch (format[bufPos]) {
         case 'c': {
            unsigned char val = va_arg(args, int);
            PutChar(val, m_fgColor, m_bgColor);
            break;
         }
         case 'i':
         case 'd': {
            int val = va_arg(args, int);
            char stringRepresentation[MAXNUMERALREPRESENTATION];
            itoa(val, stringRepresentation, 10);
            PrintFormattedWithModifiers(stringRepresentation, totalPadding, totalPrecision, leftAdjusted,
                                        nullptr);
            break;
         }
         case 'o': {
            unsigned int val = va_arg(args, unsigned int);
            char stringRepresentation[MAXNUMERALREPRESENTATION];
            itoa(val, stringRepresentation, 8);
            PrintFormattedWithModifiers(stringRepresentation, totalPadding, totalPrecision, leftAdjusted,
                                        "0");
            break;
         }
         case 'u': {
            unsigned int val = va_arg(args, unsigned int);
            char stringRepresentation[MAXNUMERALREPRESENTATION];
            itoa(val, stringRepresentation, 10);
            PrintFormattedWithModifiers(stringRepresentation, totalPadding, totalPrecision, leftAdjusted,
                                        nullptr);
            break;
         }
         case 'p': {
            char stringRepresentation[MAXNUMERALREPRESENTATION];
            uint64_t val = (uint64_t)va_arg(args, void *);
            itoa(val, stringRepresentation, 16);
            PrintFormattedWithModifiers(stringRepresentation, totalPadding, totalPrecision, leftAdjusted,
                                        nullptr);
            break;
         }
         case 'x':
         case 'X': {
            char stringRepresentation[MAXNUMERALREPRESENTATION];
            unsigned int val = va_arg(args, unsigned int);
            itoa(val, stringRepresentation, 16);
            PrintFormattedWithModifiers(stringRepresentation, totalPadding, totalPrecision, leftAdjusted,
                                        "0x");
            break;
         }
         case 's': {
            const char *str = va_arg(args, const char *);
            Puts(str);
            break;
         }
         case '%': PutChar('%', m_fgColor, m_bgColor);
         }

         // After we are done parsing the conversion char, increment.
         bufPos++;
      } else {
         PutChar(format[bufPos]);
         bufPos++;
      }
   }
   va_end(args);
}