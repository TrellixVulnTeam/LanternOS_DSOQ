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
   CallGlobalConstructors(initializers);

   TTY term(framebuffer, fontFormat);
   term.SetBackgroundColor(0x1A1A1A);
   term.SetForegroundColor(0xFFCC00);

   term.Puts("Welcome to LanternOS!");
   term.Puts("Copyright (c) 2021. Licensed under the MIT License. ");

   while (true)
      ;
   return 0;
}
}