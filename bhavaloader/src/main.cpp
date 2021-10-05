#include "efi/efi.h"
#include "efi/efi_console.h"

EFI_SYSTEM_TABLE *ST;

extern "C" {
EFI_STATUS
EFIAPI
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
   ST         = SystemTable;
   int colors = 0b00011111;
   SystemTable->ConOut->SetAttribute(SystemTable->ConOut, colors);
   SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
   SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Welcome to BhavaLoader v0.0.1\n\u000D");

   // For debugging.
   while (1)
      ;
   return EFI_SUCCESS;
}
}