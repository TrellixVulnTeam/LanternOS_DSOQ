#include "efi/efi.h"
#include "efi/efi_console.h"
#include "efi/guid.h"
#include "efi/protocol_handler.h"
#include "util/print.h"

extern "C" {
EFI_STATUS
EFIAPI
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
   int versionMajor = 0;
   int versionMinor = 1;
   int versionPatch = 0;

   ST         = SystemTable;
   int colors = 0b00011111;
   SystemTable->ConOut->SetAttribute(SystemTable->ConOut, colors);
   SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

   // Get the base address where this image was loaded for gdb remote debugging purposes.
   EFI_GUID loadedImageProtocolGUID                = EFI_LOADED_IMAGE_PROTOCOL_GUID;
   EFI_LOADED_IMAGE_PROTOCOL *loadedImageInterface = NULL;
   SystemTable->BootServices->OpenProtocol(ImageHandle, &loadedImageProtocolGUID,
                                           (void **)&loadedImageInterface, ImageHandle, NULL,
                                           EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
   UINT64 ImageBaseAddress = (UINT64)loadedImageInterface->ImageBase;
   SystemTable->BootServices->CloseProtocol(ImageHandle, &loadedImageProtocolGUID, ImageHandle, NULL);

   println(L"Welcome to BhavaLoader v%d.%d.%d.", versionMajor, versionMinor, versionPatch);
   println(L"Copyright (©) 2021. Licensed under the MIT License.");
   println(L"This UEFI Image has been loaded at memory address: 0x%x", ImageBaseAddress);

   // For debugging.
   while (1)
      ;
   return EFI_SUCCESS;
}
}