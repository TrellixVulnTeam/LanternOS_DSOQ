#pragma once
#include <stddef.h>

void *memcpy(void *dest, void *src, size_t n) {
   for (int i = 0; i < n; i++) { ((char *)dest)[i] = ((char *)src)[i]; }

   return dest;
}