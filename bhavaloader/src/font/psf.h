#pragma once

struct psf2_header {
   unsigned char magic[4];
   unsigned int version;
   unsigned int headerSize;
   unsigned int flags;
   unsigned int length;
   unsigned int charSize;
   unsigned int height, width;
};