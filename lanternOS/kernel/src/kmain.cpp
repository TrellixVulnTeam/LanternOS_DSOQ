#include "libk/string.h"
#include "stdint.h"
#include "tty/tty.h"

struct GlobalInitializers {
   uint64_t *ctorAddresses;
   int ctorCount;
   uint64_t *dtorAddresses;
   int dtorCount;
};

typedef void (*global_ctor)(void);
void CallGlobalConstructors(GlobalInitializers initializers) {
   for (int i = 0; i < initializers.ctorCount; i++) {
      global_ctor constructor = (global_ctor)initializers.ctorAddresses[i];
      constructor();
   }
}

extern "C" {
int kmain(Framebuffer framebuffer, FontFormat fontFormat, GlobalInitializers initializers) {
   int stackMarker = 0;
   CallGlobalConstructors(initializers);

   TTY term(framebuffer, fontFormat);
   term.SetBackgroundColor(0x1A1A1A);
   term.SetForegroundColor(0xFFCC00);

   term.kprintf("Welcome to LanternOS!\n");
   term.kprintf("Copyright (c) 2021. Licensed under the MIT License.\n");
   term.kprintf("GOP Framebuffer is located at address: %#.16x.\n", framebuffer.frameBufferAddress);
   term.kprintf("Approximate location of the stack pointer is: %#.16x.\n", &stackMarker);
   term.kprintf("Test octal formatting: %#12.9o \n", 365788);

   while (true)
      ;
   return 0;
}
}