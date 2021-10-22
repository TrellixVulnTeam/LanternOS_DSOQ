#include "efi/efi.h"
#include "efi/efi_console.h"
#include "efi/graphics.h"
#include "efi/guid.h"
#include "efi/protocol_handler.h"
#include "efi/simple_file_system.h"
#include "elf/elf_header.h"
#include "font/psf.h"
#include "util/memcpy.h"
#include "util/print.h"

#define KERNEL_LOC_MAX_ADDR 0x100000
#define FONT_LOC_MAX_ADDR   0x80000

struct GlobalInitializers {
   EFI_PHYSICAL_ADDRESS *ctorAddresses;
   int ctorCount;
   EFI_PHYSICAL_ADDRESS *dtorAddresses;
   int dtorCount;
};

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

struct MemoryMap {
   EFI_MEMORY_DESCRIPTOR *memoryDescriptorArray;
   UINTN memoryDescriptorArraySize;
   UINTN descriptorSize;
};

typedef int(__attribute__((sysv_abi)) * KernelEntry)(Framebuffer, FontFormat, GlobalInitializers);

/** Gets the size of a file specified by a given file handle, in bytes.
 * @param fileHandle: The file to query.
 *
 * @return The size of the file in bytes. Returns 0 if the filesize could not be determined.
 */
UINTN GetFileSize(EFI_FILE_PROTOCOL *fileHandle) {
   UINTN fileSize = 0;
   // We first allocate a pool of 1 byte so that GetInfo() can return us the correct size in bytes the buffer
   // needs to be.
   UINTN bufferSize     = 1;
   EFI_MEMORY_TYPE type = EFI_MEMORY_TYPE::EfiLoaderData;
   void *buffer         = NULL;
   ST->BootServices->AllocatePool(type, bufferSize, &buffer);
   EFI_GUID fileInfoGUID = EFI_FILE_INFO_GUID;
   fileHandle->GetInfo(fileHandle, &fileInfoGUID, &bufferSize, (void **)buffer);

   // Now that we know the correct number of bytes we have to allocate, we can get the file information.
   ST->BootServices->FreePool(buffer);
   ST->BootServices->AllocatePool(type, bufferSize, &buffer);
   EFI_STATUS st = fileHandle->GetInfo(fileHandle, &fileInfoGUID, &bufferSize, (void **)buffer);
   if (st != EFI_SUCCESS) {
      println(L"Error! Could not allocate memory for GetInfo()!");
      ST->BootServices->FreePool(buffer);
      return 0;
   }
   fileSize = ((EFI_FILE_INFO *)buffer)->FileSize;
   ST->BootServices->FreePool(buffer);
   return fileSize;
}

/** Gets the number of pages needed to store a file of a given size.
 * @param fileSize: The size of the file, in bytes.
 *
 * @return The number of 4KiB UEFI pages that need to be allocated to store this file.
 */
UINTN GetFilePageSize(UINTN fileSize) {
   if (fileSize / (1024 * 4) == 0) {
      return 1;
   } else {
      return fileSize / (1024 * 4);
   }
}

/** Helper function to allocates pages within a certain memory range. It will attempt to allocate pages
 * between the beginning of the address range and maxAddr. All pages must be allocated within this range. If
 * there is not enough available space in this range, allocation will fail.
 * @param numPages: The number of 4KiB UEFI pages you need to allocate.
 * @param maxAddr: The maximum address to consider for allocation.
 *
 * @return a pointer to the beginning of the newly allocated pages, or NULL if allocation failed.
 */
void *AllocatePagesBelowMaxAddr(int numPages, EFI_PHYSICAL_ADDRESS maxAddr) {
   EFI_PHYSICAL_ADDRESS memory = maxAddr;
   EFI_STATUS status = ST->BootServices->AllocatePages(AllocateMaxAddress, EfiLoaderData, numPages, &memory);

   if (status != 0) {
      ST->BootServices->FreePages(memory, numPages);
      return NULL;
   }
   void *buffer = (void *)memory;
   return buffer;
}

EFI_PHYSICAL_ADDRESS TranslateKernelAddress(EFI_PHYSICAL_ADDRESS kernelAddr,
                                            EFI_PHYSICAL_ADDRESS untranslatedAddr,
                                            EFI_PHYSICAL_ADDRESS vaddr) {
   // We are "supposed to" offset the load the program data into virtual memory with an offset of vaddr.
   // Instead we can just subtract it from the e_entry address for the entry point.
   // Eg if e_entry is 0x400080, and we are supposed to load the program data to address 0x400000, then
   // we can just subtract the 0x400000 and add the resulting 0x80 to the location of our kernel in memory,
   // Without having to set it to a specific virtual memory address the ELF file requests.
   return kernelAddr + untranslatedAddr - vaddr;
}

/**
 * Loads a file from the ROOT of the Boot partition only. Cannot (yet) load files in subdirectories.
 * @param deviceHandle: The device handle that loaded this EFI image.
 * @param ImageHandle: The handle representing this EFI image.
 * @param OUTbufferSize: The size of the returned buffer in bytes, or 0 if the file could not be loaded.
 * @param fileName: The full name of the file to open.
 * @param maxAddrLocation: The maximum memory address to consider when allocating pages of memory to load the
 * file into.
 *
 * @return A pointer to the beginning of the memory buffer that has been loaded with the contents of the file.
 *         Returns NULL if the file could not be loaded.
 */
UINT8 *LoadRootDirFile(EFI_HANDLE deviceHandle, EFI_HANDLE ImageHandle, UINTN *OUTbufferSize,
                       const CHAR16 *fileName, UINTN maxAddrLocation) {
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
   if (fileSize == 0) {
      return NULL;
   }
   print(L"The filesize of ");
   print(fileName);
   println(L" is: %d bytes.", fileSize);

   UINTN pageSize = GetFilePageSize(fileSize);

   // Seek to the beginning of the file just to be safe, then load the file into memory.
   fileHandle->SetPosition(fileHandle, 0);
   void *buffer = AllocatePagesBelowMaxAddr(pageSize, maxAddrLocation);
   fileHandle->Read(fileHandle, &fileSize, (void **)buffer);

   ST->BootServices->CloseProtocol(deviceHandle, &simpleFileSystemProtocolGUID, ImageHandle, NULL);
   *OUTbufferSize = fileSize;
   return (UINT8 *)buffer;
}

/**
 * Read in the header for an ELF file that has been loaded into memory.
 * @param data: Pointer to the beginning of the file in memory.
 *
 * @return A struct containing the ELF header information.
 */
Elf64_Ehdr ParseELFHeader(UINT8 *data) {
   Elf64_Ehdr header {0};
   memcpy(&header, data, sizeof(Elf64_Ehdr));
   return header;
}

/**
 * Read in the Program header for an ELF file that has been loaded into memory.
 * @param header: The header struct for the file you wish to parse.
 * @param data: Pointer to the beginning of the file in memory.
 *
 * @return A struct containing the ELF Program header information.
 */
Elf64_Phdr ParseELFPHeader(Elf64_Ehdr header, UINT8 *data) {
   Elf64_Phdr programHeader;
   void *programHeaderLocation = data + header.e_phoff;
   memcpy(&programHeader, programHeaderLocation, header.e_phentsize * header.e_phnum);
   return programHeader;
}

/**
 * Read in the header for a PC Screen Font file that has been loaded into memory.
 * @param data: Pointer to the beginning of the file in memory.
 *
 * @return A struct containing the PC Screen Font header information.
 */
psf2_header ParsePSF2Header(UINT8 *data) {
   psf2_header header;
   memcpy(&header, data, sizeof(psf2_header));
   return header;
}

/**
 * Takes in an array of 8 little-endian bytes stored in a file and converts it to a 64-bit memory address.
 * @param data: An array of 8 bytes to convert. Array is expected to be 8 bytes.
 *
 * @return A 64-bit address that was encoded in the bytes.
 */
EFI_PHYSICAL_ADDRESS ConvertLittleEndianBytesToAddr(UINT8 *data) {
   return ((UINTN)(data[0] << 0) + ((UINTN)data[1] << 8) + ((UINTN)data[2] << 16) + ((UINTN)data[3] << 24) +
           ((UINTN)data[4] << 32) + ((UINTN)data[5] << 40) + ((UINTN)data[6] << 48) + ((UINTN)data[7] << 56));
}

GlobalInitializers ParseGlobalInitializers(Elf64_Shdr *headerArray, int headerCount, UINT8 *kernelLocation,
                                           Elf64_Addr vaddr) {
   EFI_PHYSICAL_ADDRESS *initArrayStart;
   EFI_PHYSICAL_ADDRESS *finiArrayStart = NULL;
   GlobalInitializers initializers {0};
   for (int i = 0; i < headerCount; i++) {
      if (headerArray[i].sh_type == SHT_INIT_ARRAY) {
         uint64_t offset        = headerArray[i].sh_offset;
         UINTN bufferSize       = headerArray[i].sh_size;
         initializers.ctorCount = bufferSize / 8;
         EFI_MEMORY_TYPE type   = EFI_MEMORY_TYPE::EfiLoaderData;
         ST->BootServices->AllocatePool(type, bufferSize, (void **)&initArrayStart);
         for (int i = 0; i < initializers.ctorCount; i++) {
            EFI_PHYSICAL_ADDRESS ctorAddr =
               ConvertLittleEndianBytesToAddr(kernelLocation + offset + sizeof(void *) * i);
            ctorAddr          = TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelLocation, ctorAddr, vaddr);
            initArrayStart[i] = ctorAddr;
         }
      }
      if (headerArray[i].sh_type == SHT_FINI_ARRAY) {
         uint64_t offset        = headerArray[i].sh_offset;
         UINTN bufferSize       = headerArray[i].sh_size;
         initializers.dtorCount = bufferSize / 8;
         EFI_MEMORY_TYPE type   = EFI_MEMORY_TYPE::EfiLoaderData;
         ST->BootServices->AllocatePool(type, bufferSize, (void **)&finiArrayStart);
         for (int i = 0; i < initializers.dtorCount; i++) {
            EFI_PHYSICAL_ADDRESS dtorAddr =
               ConvertLittleEndianBytesToAddr(kernelLocation + offset + sizeof(void *) * i);
            dtorAddr          = TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelLocation, dtorAddr, vaddr);
            finiArrayStart[i] = dtorAddr;
         }
      }
   }
   initializers.ctorAddresses = initArrayStart;
   initializers.dtorAddresses = finiArrayStart;
   return initializers;
}

Elf64_Shdr *ParseELFSHeader(Elf64_Ehdr header, UINT8 *data) {
   Elf64_Shdr *sectionHeaderArray = (Elf64_Shdr *)(data + header.e_shoff);
   return sectionHeaderArray;
}

/**
 * Verify that a loaded file is a PC Screen Font file of the right version.
 * @param The PSF2 Header for the file you are verifying.
 *
 * @return True if the file header is correct.
 */
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

/**
 * Verify that a loaded file is a proper ELF64 executable file.
 * @param The ELF header for the file you are verifying.
 *
 * @return True if the file header is correct.
 */
bool VerifyELFFile(Elf64_Ehdr header) {
   // When I link a static library to the ELF kernel file, the following code:
   //
   // if (header.e_ident[0] != 127 || header.e_ident[1] != 'E' || header.e_ident[2] != 'L' ||
   //    header.e_ident[3] != 'F') {
   //
   // does not work, despite me personally checking those values to confirm they are right.
   // Instead, I have to splitting into multiple if statements. Not sure if I'm missing something tremendously
   // obvious.
   if (header.e_ident[0] != 127) {
      println(
         L"Loaded kernel file does not appear to be in the ELF format! This loader only supports ELF file "
         L"format.");
      return false;
   }
   if (header.e_ident[1] != 'E') {
      println(
         L"Loaded kernel file does not appear to be in the ELF format! This loader only supports ELF file "
         L"format.");
      return false;
   }
   if (header.e_ident[2] != 'L') {
      println(
         L"Loaded kernel file does not appear to be in the ELF format! This loader only supports ELF file "
         L"format.");
      return false;
   }
   if (header.e_ident[3] != 'F') {
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

/** Exits UEFI Boot services and provides a pointer to the memory map at the time of exit.
 * @param ImageHandle: The handle representing this EFI image.
 * @param OUTmapSize: Provides the size of the memory map. [OPTIONAL]
 * @return The memory map at the time of exiting Boot Services.
 */
MemoryMap ExitBootServices(EFI_HANDLE ImageHandle) {
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
   // We need to allocate a little extra memory because by allocating new memory weve changed the size
   // of the memory map.
   memoryMapSize += (2 * descriptorSize);
   ST->BootServices->AllocatePool(type, memoryMapSize, &buffer);
   EFI_STATUS garb = ST->BootServices->GetMemoryMap(&memoryMapSize, (EFI_MEMORY_DESCRIPTOR *)buffer, &mapKey,
                                                    &descriptorSize, &descriptorVersion);
   EFI_MEMORY_DESCRIPTOR *memoryMap = (EFI_MEMORY_DESCRIPTOR *)buffer;

   // Finally we can exit boot services.
   ST->BootServices->ExitBootServices(ImageHandle, mapKey);

   MemoryMap map = {memoryMap, memoryMapSize, descriptorSize};

   return map;
}

/** Finds the highest resolution video mode this device supports.
 *
 * @return The UEFI mode number for the selected video mode, or -1 if an adequate mode could not be found.
 */
UINT32 GetVideoMode() {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = NULL;
   ST->BootServices->LocateProtocol(&gopGUID, NULL, (void **)&gopInterface);
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
   UINTN size                                 = 0;
   int favoredMode                            = -1;
#ifdef CUSTOM_RESOLUTION
   for (int i = 0; i < gopInterface->Mode->MaxMode; i++) {
      gopInterface->QueryMode(gopInterface, i, &size, &info);
      if (info->HorizontalResolution == CUSTOM_RESOLUTION_X &&
          info->VerticalResolution == CUSTOM_RESOLUTION_Y) {
         favoredMode = i;
      }
   }
#else
   // Get video mode with highest resolution.
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
#endif
   gopInterface->QueryMode(gopInterface, favoredMode, &size, &info);
   println(L"Selected Kernel Video Mode Horz: %d px, Vert: %d px.", info->HorizontalResolution,
           info->VerticalResolution);

   return favoredMode;
}

Framebuffer SetUpFramebuffer(UINTN videoMode) {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = NULL;
   ST->BootServices->LocateProtocol(&gopGUID, NULL, (void **)&gopInterface);

   gopInterface->SetMode(gopInterface, videoMode);

   return {(void *)gopInterface->Mode->FrameBufferBase, gopInterface->Mode->Info->PixelsPerScanLine,
           gopInterface->Mode->Info->HorizontalResolution, gopInterface->Mode->Info->VerticalResolution};
}

void WaitForKey(const CHAR16 *msg) {
   // Clear input queue.
   ST->ConIn->Reset(ST->ConIn, false);
   println(msg);
   // Listen for a keystroke.
   EFI_INPUT_KEY key {0};
   while (key.UnicodeChar == 0 && key.ScanCode == 0) { ST->ConIn->ReadKeyStroke(ST->ConIn, &key); }
}

void EnumerateMemoryMap() {
}

extern "C" {
EFI_STATUS
EFIAPI
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
   int versionMajor = 0;
   int versionMinor = 2;
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

   UINTN bufferSize      = 0;
   UINT8 *kernelLocation = LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, &bufferSize,
                                           L"LanternOS", KERNEL_LOC_MAX_ADDR);
   if (kernelLocation == NULL) {
      WaitForKey(L"Could not load ELF kernel into memory.");
      return 1;
   }
   println(L"Kernel has been loaded into memory starting at address: 0x%x.", kernelLocation);

   Elf64_Ehdr headerData          = ParseELFHeader(kernelLocation);
   Elf64_Phdr programHeader       = ParseELFPHeader(headerData, kernelLocation);
   Elf64_Shdr *sectionHeaderArray = ParseELFSHeader(headerData, kernelLocation);
   GlobalInitializers globalObjCtorDtor =
      ParseGlobalInitializers(sectionHeaderArray, headerData.e_shnum, kernelLocation, programHeader.p_vaddr);

   EFI_PHYSICAL_ADDRESS adjustedEntryPoint =
      TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelLocation, headerData.e_entry, programHeader.p_vaddr);

   KernelEntry kmain = (KernelEntry)adjustedEntryPoint;

   if (!VerifyELFFile(headerData)) {
      return 1;
   } else {
      println(L"Kernel file successfully verified as 64-bit ELF executable.");
   }
   println(L"Entry point kmain for kernel is loaded in memory at address 0x%x", adjustedEntryPoint);

   UINTN fontBufferSize = 0;
   UINT8 *fontLocation  = LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, &fontBufferSize,
                                         L"font.psf", FONT_LOC_MAX_ADDR);

   psf2_header psf2Header = ParsePSF2Header(fontLocation);
   if (!VerifyPSF2File(psf2Header)) {
      return 1;
   }
   FontFormat fontFormat = {fontLocation + psf2Header.headerSize, psf2Header.length, psf2Header.charSize,
                            psf2Header.height, psf2Header.width};

   UINT32 videoMode = GetVideoMode();
   if (videoMode == -1) {
      WaitForKey(L"Could not find suitable video mode.");
      return 1;
   }

   WaitForKey(L"Ready to transfer control to kernel. Press any key to continue...");
   Framebuffer framebuffer = SetUpFramebuffer(videoMode);

   // Cleanup
   SystemTable->BootServices->CloseProtocol(ImageHandle, &loadedImageProtocolGUID, ImageHandle, NULL);
   MemoryMap uefiMemoryMap = ExitBootServices(ImageHandle);
   // Execute kernel
   int ret = kmain(framebuffer, fontFormat, globalObjCtorDtor);

   return EFI_SUCCESS;
}
}