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
   public:
   TTY(Framebuffer fb, FontFormat font);

   /**
    * @brief Sets the current background color.
    * The background color for the entire screen will be retroactively changed to the new color.
    *
    * @param pixelColor: The new background color.
    */
   void SetBackgroundColor(uint32_t pixelColor);

   /**
    * @brief Sets the current foreground (text) color.
    * The foreground color for the entire screen will be retroactively changed to the new color, including
    * all previously printed text being rendered.
    *
    * @param pixelColor: The new foreground color.
    */
   void SetForegroundColor(uint32_t pixelColor);

   /** @brief Clears the screen to the current background color. */
   void ClearScreen();

   /**
    * @brief Renders a string of text to the screen starting at the beginning of the most recent line.
    * Puts will implicitly terminate every string with a newline.
    *
    * @param array: A pointer to a null-terminated string to be printed.
    * @param fg: The text color to print with.
    * @param bg: The background color to print behind each character.
    */
   void Puts(const char *array, uint32_t fg, uint32_t bg);

   /**
    * @brief Renders a string of text to the screen starting at the beginning of the most recent line. Will
    * automatically use the currently selected foreground and background colors.
    *
    * @param array: A pointer to a null-terminated string to be printed.
    */
   void Puts(const char *array);

   /**
    * @brief Places a single ASCII character at the current cursor position.
    *
    * @param charToPrint: The ASCII character to print to the screen.
    * @param foreground: The RGB foreground color for the character. Valid from 0x00000000 - 0xFFFFFFFF.
    * @param background: The RGB background color for the character. Valid from 0x00000000 - 0xFFFFFFFF.
    *
    * @todo: Need to implement scrolling.
    */
   void PutChar(uint8_t charToPrint, uint32_t foreground, uint32_t background);

   /**
    * @brief Places a single ASCII character at the current cursor position. Will automatically use the
    * currently selected foreground and background colors.
    *
    * @param charToPrint: The ASCII character to print to the screen.
    *
    * @todo: Need to implement scrolling.
    */
   void PutChar(uint8_t charToPrint);

   /** @brief Creates a newline and carriage return by setting the current cursor position. */
   void NewLine();

   /** @brief Prints a formatted string to the screen.
    *
    * Currently, formatting roughly follows the Libc standard but is missing many features. Can do
    * basic formatted printing of integers, unsigned ints, hexadecimal values, characters, strings, and
    * pointers.
    *
    * @param format: A pointer to the first character of a null-terminated string, with or without
    *                conversion specifiers, that you wish to print.
    * @param ...: Variadic args for the variables you wish to format for printing.
    *
    * @todo: Finish implementing according to the C11 standard. Consider using variadic templates instead of
    *        va_args.
    */
   void kprintf(const char *format, ...);

   private:
   /** An abstraction of the the linear buffer of pixels that this TTY will draw to. */
   Framebuffer m_framebuf {};
   /** An abstraction of the currently loaded PC Screen Font data to be used to draw characters. */
   FontFormat m_loadedFont {};
   /** The column where the next character will be placed. */
   uint64_t m_currentCharPosX {0};
   /** The row where the next character will be placed. */
   uint64_t m_currentCharPosY {0};

   /** The number of rows that can fit on the screen, based on current resolution. */
   uint32_t m_numCharRows {0};
   /** The number of columns that can fit on the screen, based on current resolution. */
   uint32_t m_numCharCols {0};
   /** The current background color of the TTY. */
   uint32_t m_bgColor {0};
   /** The current foreground color of the TTY. */
   uint32_t m_fgColor {0};

   /** Any Non-floating-point value should only need this many characters maximum to be represented. */
   const uint8_t MAXNUMERALREPRESENTATION = 22;

   void PrintFormattedWithModifiers(const char *str, long paddingAmount, long precisionAmount,
                                    bool leftAdjusted, char *alternateFormStr);

   /**
    * @brief Gets the RGB value of a specific pixel on the screen.
    * The (0,0) coordinate is the top-left pixel of the screen.
    *
    * @param x: The x coordinate of the pixel.
    * @param y: The y coordinate of the pixel.
    *
    * @return The pixel color.
    */
   uint32_t GetPixelColor(uint32_t posX, uint32_t posY);

   /**
    * @brief Draw a single pixel to the screen based on (x,y) coordinates.
    * The (0,0) coordinate is the top-left pixel of the screen.
    *
    * @param x: The x coordinate of the pixel. Valid from 0 - framebuffer.horizontalResolution.
    * @param y: The y coordinate of the pixel. Valid from 0 - framebuffer.verticalResolution.
    * @param pixelColor: The color to fill the pixel with. Valid from 0x00000000 - 0xFFFFFFFF.
    *                    0xFF000000 represents blue value, 0x00FF0000 represents green value, 0x0000FF00
    *                    represents red.
    */
   void PlotPixel(uint32_t x, uint32_t y, uint32_t pixelColor);
};
