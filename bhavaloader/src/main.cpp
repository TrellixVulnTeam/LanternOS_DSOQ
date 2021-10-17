#include "efi/efi.h"
#include "efi/efi_console.h"
#include "efi/graphics.h"
#include "efi/guid.h"
#include "efi/protocol_handler.h"
#include "efi/simple_file_system.h"
#include "elf/elf_header.h"
#include "font/psf.h"
#include "util/print.h"

struct Framebuffer {
   void *frameBufferAddress;
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

typedef int(__attribute__((sysv_abi)) * KernelEntry)(Framebuffer, FontFormat);

UINTN GetFileSize(EFI_FILE_PROTOCOL *fileHandle) {
   UINTN fileSize = 0;
   // Find filesize of kernel. We first allocate a pool of 1 byte so that GetInfo() can return
   // us the correct size in bytes the buffer needs to be.
   UINTN bufferSize     = 1;
   EFI_MEMORY_TYPE type = EFI_MEMORY_TYPE::EfiLoaderData;
   void *buffer         = NULL;
   ST->BootServices->AllocatePool(type, bufferSize, &buffer);
   EFI_GUID fileInfoGUID = EFI_FILE_INFO_GUID;
   fileHandle->GetInfo(fileHandle, &fileInfoGUID, &bufferSize, (void **)buffer);

   // Now that we know the correct number of bytes we have to allocate, we can get the data for real.
   ST->BootServices->FreePool(buffer);
   ST->BootServices->AllocatePool(type, bufferSize, &buffer);
   EFI_STATUS st = fileHandle->GetInfo(fileHandle, &fileInfoGUID, &bufferSize, (void **)buffer);
   if (st != EFI_SUCCESS) {
      println(L"Error! Could not allocate memory for GetInfo()!");
      ST->BootServices->FreePool(buffer);
      return fileSize;
   }
   fileSize = ((EFI_FILE_INFO *)buffer)->FileSize;
   ST->BootServices->FreePool(buffer);
   return fileSize;
}

UINT8 *LoadRootDirFile(EFI_HANDLE deviceHandle, EFI_HANDLE ImageHandle, UINTN *bufferSize,
                       const CHAR16 *fileName) {
   // Get SimpleFileSystem Protocol.
   EFI_GUID simpleFileSystemProtocolGUID         = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SFSInterface = NULL;
   EFI_STATUS status =
      ST->BootServices->OpenProtocol(deviceHandle, &simpleFileSystemProtocolGUID, (void **)&SFSInterface,
                                     ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
   if (status != EFI_SUCCESS) {
      println(L"Error! SimpleFileSystemProtocol not supported!");
      return NULL;
   }

   // Open Volume root file handle.
   EFI_FILE_PROTOCOL *rootHandle = NULL;
   SFSInterface->OpenVolume(SFSInterface, &rootHandle);

   // Open file handle for desired file.
   EFI_FILE_PROTOCOL *fileHandle = NULL;
   status = rootHandle->Open(rootHandle, &fileHandle, (CHAR16 *)fileName, EFI_FILE_MODE_READ, 0);
   if (status != EFI_SUCCESS) {
      println(L"Error! Could not get a file handle to the kernel.");
      return NULL;
   }
   rootHandle->Close(rootHandle);

   // Get the filesize of the file so we know how much memory to allocate for it.
   UINTN fileSize = GetFileSize(fileHandle);
   print(L"The filesize of ");
   print(fileName);
   println(L" is: %d bytes.", fileSize);

   // Seek to the beginning of the file just to be safe, then load the file into memory.
   fileHandle->SetPosition(fileHandle, 0);
   void *buffer = NULL;
   ST->BootServices->AllocatePool(EFI_MEMORY_TYPE::EfiLoaderData, fileSize, &buffer);
   fileHandle->Read(fileHandle, &fileSize, (void **)buffer);

   ST->BootServices->CloseProtocol(deviceHandle, &simpleFileSystemProtocolGUID, ImageHandle, NULL);
   *bufferSize = fileSize;
   return (UINT8 *)buffer;
}

Elf64_Ehdr ParseELFHeader(uint8_t *data) {
   Elf64_Ehdr header {0};

   for (int i = 0; i < sizeof(Elf64_Ehdr); i++) { ((uint8_t *)&header)[i] = data[i]; }

   return header;
}

Elf64_Phdr ParseELFPHeader(Elf64_Ehdr header, uint8_t *data) {
   Elf64_Phdr programHeader;

   for (int i = 0; i < header.e_phentsize * header.e_phnum; i++) {
      ((uint8_t *)&programHeader)[i] = data[i + header.e_phoff];
   }
   return programHeader;
}

psf2_header ParsePSF2Header(UINT8 *data) {
   psf2_header header;
   for (int i = 0; i < sizeof(psf2_header); i++) { ((uint8_t *)&header)[i] = data[i]; }

   return header;
}

bool VerifyPSF2File(psf2_header header) {
   if (header.magic[0] != 0x72 || header.magic[1] != 0xb5 || header.magic[2] != 0x4a ||
       header.magic[3] != 0x86) {
      println(
         L"Loaded font file does not appear to be in PSF VERSION 2 format! This loader only supports PSF "
         L"VERSION 2");
      return false;
   }

   return true;
}

bool VerifyELFFile(Elf64_Ehdr header) {
   if (header.e_ident[0] != 127 || header.e_ident[1] != 'E' || header.e_ident[2] != 'L' ||
       header.e_ident[3] != 'F') {
      println(
         L"Loaded kernel file does not appear to be in the ELF format! This loader only supports ELF file "
         L"format.");
      return false;
   }
   if (header.e_ident[4] != 2) {
      println(L"Loaded kernel file is not 64 bit. This loader only supports 64 bit kernels.");
      return false;
   }

   return true;
}

void ExitBootServices(EFI_HANDLE ImageHandle) {
   // First allocate a buffer of 1 byte to get the actual size of the memory map.
   void *buffer             = NULL;
   UINTN memoryMapSize      = 1;
   UINTN mapKey             = 0;
   UINTN descriptorSize     = 0;
   UINT32 descriptorVersion = 0;
   EFI_MEMORY_TYPE type     = EFI_MEMORY_TYPE::EfiLoaderData;
   ST->BootServices->AllocatePool(type, 1, &buffer);
   ST->BootServices->GetMemoryMap(&memoryMapSize, (EFI_MEMORY_DESCRIPTOR *)buffer, &mapKey, &descriptorSize,
                                  &descriptorVersion);

   // Now we can actually allocate the map.
   ST->BootServices->FreePool(buffer);
   ST->BootServices->AllocatePool(type, memoryMapSize, &buffer);
   ST->BootServices->GetMemoryMap(&memoryMapSize, (EFI_MEMORY_DESCRIPTOR *)buffer, &mapKey, &descriptorSize,
                                  &descriptorVersion);
   EFI_MEMORY_DESCRIPTOR *memoryMap = (EFI_MEMORY_DESCRIPTOR *)buffer;

   // Finally we can exit boot services.
   ST->BootServices->ExitBootServices(ImageHandle, mapKey);
}

UINT32 GetVideoMode() {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = NULL;
   ST->BootServices->LocateProtocol(&gopGUID, NULL, (void **)&gopInterface);
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
   UINTN size                                 = 0;

   // Get video mode with highest resolution.
   int favoredMode           = 0;
   int currentHighestHorzRes = 0;
   int currentHighestVertRes = 0;
   for (int i = 0; i < gopInterface->Mode->MaxMode; i++) {
      gopInterface->QueryMode(gopInterface, i, &size, &info);
      if (info->HorizontalResolution > currentHighestHorzRes) {
         if (info->VerticalResolution > currentHighestVertRes) {
            favoredMode = i;
         }
      }
   }
   gopInterface->QueryMode(gopInterface, favoredMode, &size, &info);
   println(L"Selected Kernel Video Mode Horz: %d px, Vert: %d px.", info->HorizontalResolution,
           info->VerticalResolution);

   return favoredMode;
}

Framebuffer SetUpFramebuffer(UINT32 videoMode) {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = NULL;
   ST->BootServices->LocateProtocol(&gopGUID, NULL, (void **)&gopInterface);

   gopInterface->SetMode(gopInterface, videoMode);

   return {(void *)gopInterface->Mode->FrameBufferBase, gopInterface->Mode->Info->PixelsPerScanLine,
           gopInterface->Mode->Info->HorizontalResolution, gopInterface->Mode->Info->VerticalResolution};
}

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

   // Get interface for Loaded Image Protocol
   EFI_GUID loadedImageProtocolGUID                = EFI_LOADED_IMAGE_PROTOCOL_GUID;
   EFI_LOADED_IMAGE_PROTOCOL *loadedImageInterface = NULL;
   SystemTable->BootServices->OpenProtocol(ImageHandle, &loadedImageProtocolGUID,
                                           (void **)&loadedImageInterface, ImageHandle, NULL,
                                           EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
   // Get the address this UEFI image was loaded at and print it for debugging purposes.
   UINT64 ImageBaseAddress = (UINT64)loadedImageInterface->ImageBase;

   println(L"Welcome to BhavaLoader v%d.%d.%d.", versionMajor, versionMinor, versionPatch);
   println(L"Copyright (©) 2021. Licensed under the MIT License.");
   println(L"This UEFI Image has been loaded at memory address: 0x%x", ImageBaseAddress);

   UINTN bufferSize = 0;
   UINT8 *kernelLocation =
      LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, &bufferSize, L"LanternOS");
   if (kernelLocation == NULL) {
      println(L"Could not load ELF kernel into memory.");
      return 1;
   }
   println(L"Kernel has been loaded into memory starting at address: 0x%x.", kernelLocation);

   Elf64_Ehdr headerData    = ParseELFHeader(kernelLocation);
   Elf64_Phdr programHeader = ParseELFPHeader(headerData, kernelLocation);
   UINTN vaddr              = programHeader.p_vaddr;

   // We are "supposed to" offset the load the program data into virtual memory with an offset of vaddr.
   // Instead we can just subtract it from the e_entry address for the entry point.
   // Eg if e_entry is 0x400080, and we are supposed to load the program data to address 0x400000, then
   // we can just subtract the 0x400000 and add the resulting 0x80 to the location of our kernel in memory,
   // Without having to set it to a specific virtual memory address the ELF file requests.
   UINTN adjustedEntry = (UINTN)kernelLocation + headerData.e_entry - vaddr;
   KernelEntry kmain   = (KernelEntry)adjustedEntry;

   if (!VerifyELFFile(headerData)) {
      return 1;
   } else {
      println(L"Kernel file successfully verified as 64-bit ELF executable.");
   }
   println(L"Entry point kmain for kernel is loaded in memory at address 0x%x", adjustedEntry);

   UINTN fontBufferSize = 0;
   UINT8 *fontLocation =
      LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, &fontBufferSize, L"font.psf");

   psf2_header psf2Header = ParsePSF2Header(fontLocation);
   if (!VerifyPSF2File(psf2Header)) {
      return 1;
   }
   FontFormat fontFormat = {fontLocation + psf2Header.headerSize, psf2Header.length, psf2Header.charSize,
                            psf2Header.height, psf2Header.width};

   UINT32 videoMode = GetVideoMode();

   // Clear input queue.
   ST->ConIn->Reset(ST->ConIn, false);
   println(L"Ready to transfer control to kernel. Press any key to continue...");
   // Listen for a keystroke.
   EFI_INPUT_KEY key {0};
   while (key.UnicodeChar == 0 && key.ScanCode == 0) {
      SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &key);
   }
   Framebuffer framebuffer = SetUpFramebuffer(videoMode);

   // Cleanup
   SystemTable->BootServices->CloseProtocol(ImageHandle, &loadedImageProtocolGUID, ImageHandle, NULL);
   ExitBootServices(ImageHandle);
   // Execute kernel
   int ret = kmain(framebuffer, fontFormat);

   return EFI_SUCCESS;
}
}