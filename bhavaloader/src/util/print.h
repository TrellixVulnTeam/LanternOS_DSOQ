#pragma once
#include "../efi/efi_console.h"
#include "stdarg.h"

#define MAX_CHARS          1024
#define MAX_NUMERIC_LENGTH 24

/**
 * Reverses an array of CHAR16.
 * @param string: The buffer you wish to reverse.
 * @param lastPos: The last index that contains meaningful information in your buffer.
 */
void reverse_string(CHAR16* string, int lastPos) {
   int bufPos = 0;
   CHAR16 storageBuf[MAX_NUMERIC_LENGTH];

   for (int i = lastPos; i >= 0; i--) {
      storageBuf[bufPos] = string[i];
      bufPos++;
   }

   for (int i = 0; i <= lastPos; i++) { string[i] = storageBuf[i]; }
}

/**
 * Converts an integer value (up to 64 bits) into a string representation.
 * @param value: The value you wish to convert.
 * @param base: The base you wish the value to be represented in. Supports Base2 to Base16.
 * @param buffer: The buffer you wish to fill with the new string representation. Cannot exceed
 * MAX_NUMERIC_LENGTH.
 * @param length: a pointer to an integer that will be filled with the length of the string representation.
 */
void itoa(UINT64 value, int base, CHAR16* buffer, int* length) {
   int bufPos = 0;
   if (value == 0) {
      buffer[0] = L'0';
      *length   = 1;
      return;
   } else {
      int valcopy = __builtin_abs(value);
      while (valcopy > 0) {
         // Replace the value we were creating with a dumb error message because I can't think of anything
         // better to do.
         if (bufPos > MAX_NUMERIC_LENGTH) {
            buffer[0] = L'{';
            buffer[1] = L'E';
            buffer[2] = L'R';
            buffer[3] = L'R';
            buffer[4] = L'}';
            *length   = 5;
            return;
         }

         // 48 is the ASCII value for 0
         int code = 48 + (valcopy % base);
         // If our char has an ASCII code greater than 57, we need to offset by a small amount to reach
         // the letters A...F that will let us represent a value in a base greater than 10.
         if (code > 57) {
            code += 7;
         }
         buffer[bufPos] = code;
         bufPos++;
         valcopy = valcopy / base;
      }
   }

   // If we are converting a decimal value, and its negative, we need to end this string with a -
   if (base == 10) {
      if (value < 0) {
         buffer[bufPos] = L'-';
         bufPos++;
      }
   }

   // Now we have the characters backwards, we just need to reverse them.
   // We subtract 1 from bufPos because bufPos points to the index AFTER the last character weve written,
   // which in this case would be initialized garbage we don't want to copy.
   reverse_string(buffer, bufPos - 1);
   *length = bufPos;
}

/** A very simple print function. Just prints some basic variables, no extra fancy formatting like spacing.
 * @param fmt: The format string.
 * @param addEOL: Whether to add a newline sequence to the formatted string.
 * @param args: Additional n parameters you wish to be formatted into the string, in the form of a va_list.
 * @TODO: Currently no enforcement for if the formatted string exceeds MAX_CHARS and corrupts other data.
 */
void print_internal(const wchar_t* fmt, bool addEOL, va_list args) {
   wchar_t buffer[MAX_CHARS];
   int fmtPosition = 0;
   int bufferPos   = 0;

   while (true) {
      if (fmt[fmtPosition] == '%') {
         fmtPosition++;  // Ignore the escape character, we don't want to print it.
         switch (fmt[fmtPosition]) {
         case '%': {
            buffer[bufferPos] = L'%';
            fmtPosition++;
            bufferPos++;
            break;
         }
         case 'd': {
            int val = va_arg(args, int);
            CHAR16 intBuffer[MAX_NUMERIC_LENGTH];
            int length = 0;
            itoa(val, 10, intBuffer, &length);
            for (int i = 0; i < length; i++) {
               buffer[bufferPos] = intBuffer[i];
               bufferPos++;
            }
            fmtPosition++;
            break;
         }
         case 'x': {
            int val = va_arg(args, int);
            CHAR16 intBuffer[MAX_NUMERIC_LENGTH];
            int length = 0;
            itoa(val, 16, intBuffer, &length);
            for (int i = 0; i < length; i++) {
               buffer[bufferPos] = intBuffer[i];
               bufferPos++;
            }
            fmtPosition++;
            break;
         }
         }
      } else {
         buffer[bufferPos] = fmt[fmtPosition];
         if (fmt[fmtPosition] == 0) {
            // When printing to the UEFI console, we need both a newline char (this places the cursor on the
            // next row) and a carriage return (this resets the cursor back to the beginning of the row).
            // Finally we need the actual null terminator to finish off the array.
            if (addEOL) {
               buffer[bufferPos] = L'\n';
               bufferPos++;
               buffer[bufferPos] = L'\u000D';
               bufferPos++;
               buffer[bufferPos] = 0;
            }
            break;
         }
         fmtPosition++;
         bufferPos++;
      }
   }

   // Our buffer is now fully formatted and we can print to the UEFI console.
   ST->ConOut->OutputString(ST->ConOut, buffer);
}

/** Print a formatted string to the console, with no newline sequence.
 * @param fmt: The format string.
 * @param ...: variadic number of args to be formatted.
 */
void print(const CHAR16* fmt, ...) {
   va_list args;
   va_start(args, fmt);
   print_internal(fmt, false, args);
   va_end(args);
}

/** Print a formatted string to the console, with a newline sequence.
 * @param fmt: The format string.
 * @param ...: variadic number of args to be formatted.
 */
void println(const CHAR16* fmt, ...) {
   va_list args;
   va_start(args, fmt);
   print_internal(fmt, true, args);
   va_end(args);
}