#include "efi.h"
#include "efiprot.h"

EFI_SYSTEM_TABLE *ST;

#include "elf/elf_header.h"
#include "font/psf.h"
#include "util/memcpy.h"
#include "util/print.h"

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

/**
 * @brief Pauses execution until input is recieved from the user.
 * @param msg: A message you wish to display to the user.
 */
void WaitForKey(const wchar_t *msg) {
   // Clear input queue.
   ST->ConIn->Reset(ST->ConIn, false);
   println(msg);
   // Listen for a keystroke.
   EFI_INPUT_KEY key {0};
   while (key.UnicodeChar == 0 && key.ScanCode == 0) { ST->ConIn->ReadKeyStroke(ST->ConIn, &key); }
}

/** @brief Gets the size of a file specified by a given file handle, in bytes.
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
   void *buffer         = nullptr;
   ST->BootServices->AllocatePool(type, bufferSize, &buffer);
   EFI_GUID fileInfoGUID = EFI_FILE_INFO_ID;
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

/** @brief Gets the number of pages needed to store data of a given size. Will round up to the nearest
 * whole number of pages.
 * @param dataSize: The size of the data, in bytes.
 *
 * @return The number of 4KiB UEFI pages that need to be allocated to store this data.
 */
UINTN GetDataPageSize(UINTN dataSize) {
   if (dataSize / (1024 * 4) == 0) {
      return 1;
   } else {
      return (UINTN(dataSize / (1024 * 4))) + 1;
   }
}

/**
 * @brief Allocates the necessary number of pages at any address as EfiLoaderData.
 * @param dataSize: The size of the data, in bytes.
 *
 * @return: The address of the beginning of the newly allocated pages, or nullptr on failure.
 */
void *AllocatePagesForData(int dataSize) {
   EFI_PHYSICAL_ADDRESS addr;
   UINTN numPages    = GetDataPageSize(dataSize);
   EFI_STATUS status = ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, numPages, &addr);

   if (status != 0) {
      ST->BootServices->FreePages(addr, numPages);
      return nullptr;
   } else {
      return (void *)addr;
   }
}

/**
 * @brief Translates a virtual kernel address in the kernel file to an actual address in UEFI memory.
 *
 * The kernel ELF file expects to be loaded at 0x400000, but we can not expect UEFI to have any specific
 * address free for us. Instead we must calculate the actual offset in memory where the requested data is
 * stored.
 *
 * @param kernelAddr: The address where UEFI chose to load the kernel into memory.
 * @param untranslatedAddr: The Address the ELF binary expects some data to be loaded at.
 * @param vaddr: The base address the ELF binary expects to be loaded at (usually 0x400000).
 *
 * @return: The translated memory address where the data actually resides in memory on the system.
 */
EFI_PHYSICAL_ADDRESS TranslateKernelAddress(EFI_PHYSICAL_ADDRESS kernelAddr,
                                            EFI_PHYSICAL_ADDRESS untranslatedAddr,
                                            EFI_PHYSICAL_ADDRESS vaddr) {
   return kernelAddr + untranslatedAddr - vaddr;
}

/**
 * @brief Loads a file from the ROOT of the Boot partition only. Cannot (yet) load files in subdirectories.
 * @param deviceHandle: The device handle that loaded this EFI image.
 * @param ImageHandle: The handle representing this EFI image.
 * @param fileName: The full name of the file to open.
 *
 * @return A pointer to EFI_FILE_PROTOCOL for the requested file, or null if file could not be opened.
 */
EFI_FILE_PROTOCOL *LoadRootDirFile(EFI_HANDLE deviceHandle, EFI_HANDLE ImageHandle, const wchar_t *fileName) {
   // Get SimpleFileSystem Protocol.
   EFI_GUID simpleFileSystemProtocolGUID         = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SFSInterface = nullptr;
   EFI_STATUS status                             = ST->BootServices->OpenProtocol(
      deviceHandle, &simpleFileSystemProtocolGUID, (void **)&SFSInterface, ImageHandle, nullptr, 0x00000001);
   if (status != EFI_SUCCESS) {
      println(L"Error! SimpleFileSystemProtocol not supported!");
      return nullptr;
   }

   // Open Volume root file handle.
   EFI_FILE_PROTOCOL *rootHandle = nullptr;
   SFSInterface->OpenVolume(SFSInterface, &rootHandle);

   // Open file handle for desired file.
   EFI_FILE_PROTOCOL *fileHandle = nullptr;
   status = rootHandle->Open(rootHandle, &fileHandle, (CHAR16 *)fileName, EFI_FILE_MODE_READ, 0);
   if (status != EFI_SUCCESS) {
      println(L"Error! Could not get a file handle to the kernel.");
      return nullptr;
   }
   rootHandle->Close(rootHandle);
   fileHandle->SetPosition(fileHandle, 0);
   ST->BootServices->CloseProtocol(deviceHandle, &simpleFileSystemProtocolGUID, ImageHandle, nullptr);
   return fileHandle;
}

/**
 * @brief Verify that a loaded file is a proper ELF64 executable file.
 * @param The ELF header for the file you are verifying.
 *
 * @return True if the file header is correct, false otherwise.
 */
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

/**
 * @brief Read in the header for the given kernel file. Seek position of the file is set to zero after
 * function call.
 * @param kernelHandle: Handle to the opened kernel file.
 *
 * @return A struct containing the ELF header information.
 */
Elf64_Ehdr ParseELFHeader(EFI_FILE_PROTOCOL *kernelHandle) {
   Elf64_Ehdr headerData {};
   UINTN size = sizeof(Elf64_Ehdr);
   kernelHandle->Read(kernelHandle, &size, &headerData);

   if (!VerifyELFFile(headerData)) {
      WaitForKey(L"");
   }

   kernelHandle->SetPosition(kernelHandle, 0);
   return headerData;
}

/**
 * @brief Read in the program headers for the given kernel file. Seek position of the file is set to zero
 * after function call.
 * @param elfHeader: The header struct for the given kernel file.
 * @param kernelHandle: Handle to the opened kernel file.
 *
 * @return A pointer to the first element of an array of Program Headers.
 */
Elf64_Phdr *ParseELFPHeader(Elf64_Ehdr elfHeader, EFI_FILE_PROTOCOL *kernelHandle) {
   Elf64_Phdr *programHeaders;
   kernelHandle->SetPosition(kernelHandle, elfHeader.e_phoff);
   UINTN phsize = elfHeader.e_phentsize * elfHeader.e_phnum;
   kernelHandle->Read(kernelHandle, &phsize, (void **)programHeaders);

   kernelHandle->SetPosition(kernelHandle, 0);
   return programHeaders;
}

/**
 * @brief Read in the section headers, if they exist, for the given kernel file. Seek position of the file is
 * set to zero after function call.
 * @param elfHeader: The header struct for the given kernel file.
 * @param kernelHandle: Handle to the opened kernel file.
 *
 * @return A pointer to the first element of an array of Program Headers, or null if there are no section
 * headers.
 */
Elf64_Shdr *ParseELFSHeader(Elf64_Ehdr elfHeader, EFI_FILE_PROTOCOL *kernelHandle) {
   if (elfHeader.e_shoff == 0) {
      return nullptr;
   }

   Elf64_Shdr *sectionHeaders;
   kernelHandle->SetPosition(kernelHandle, elfHeader.e_shoff);
   UINTN shsize = elfHeader.e_shentsize * elfHeader.e_shnum;
   kernelHandle->Read(kernelHandle, &shsize, (void **)sectionHeaders);
   kernelHandle->SetPosition(kernelHandle, 0);
   return sectionHeaders;
}

/**
 * @brief Read in the header for the given PSF2 file. Seek position of the file is set to zero after function
 * call.
 * @param fontHandle: Handle to the opened font file.
 *
 * @return A struct containing the PSF2 Header information..
 */
psf2_header ParsePSF2Header(EFI_FILE_PROTOCOL *fontHandle) {
   psf2_header psf2Header {};
   UINTN psf2size = sizeof(psf2_header);
   fontHandle->Read(fontHandle, &psf2size, &psf2Header);

   fontHandle->SetPosition(fontHandle, 0);
   return psf2Header;
}

/**
 * @brief Takes in an array of 8 little-endian bytes stored in a file and converts it to a 64-bit memory
 * address.
 * @param data: An array of 8 bytes to convert. Array is expected to be 8 bytes.
 *
 * @return A 64-bit address that was encoded in the bytes.
 */
EFI_PHYSICAL_ADDRESS ConvertLittleEndianBytesToAddr(UINT8 *data) {
   return ((UINTN)(data[0] << 0) + ((UINTN)data[1] << 8) + ((UINTN)data[2] << 16) + ((UINTN)data[3] << 24) +
           ((UINTN)data[4] << 32) + ((UINTN)data[5] << 40) + ((UINTN)data[6] << 48) + ((UINTN)data[7] << 56));
}

/**
 * @brief Parses the addresses of the global constructors and destructors needed for using global objects
 * in a c++ kernel.
 *
 * @param headerArray: A pointer to the first element of an array of section headers.
 * @param headerCount: The length of the header array.
 * @param kernelData: Pointer to the beginning of the loaded kernel in memory.
 * @param vaddr: The base Address that the Elf File expects to be loaded at, for address conversion.
 *
 * @return A GlobalInitializers Struct filled with addresses for constructors and destructors.
 */
GlobalInitializers ParseGlobalInitializers(Elf64_Shdr *headerArray, int headerCount, UINT8 *kernelData,
                                           Elf64_Addr vaddr) {
   EFI_PHYSICAL_ADDRESS *initArrayStart = nullptr;
   EFI_PHYSICAL_ADDRESS *finiArrayStart = nullptr;
   GlobalInitializers initializers {0};
   for (int i = 0; i < headerCount; i++) {
      if (headerArray[i].sh_type == SHT_INIT_ARRAY) {
         UINTN offset           = headerArray[i].sh_offset;
         UINTN bufferSize       = headerArray[i].sh_size;
         initializers.ctorCount = bufferSize / 8;
         EFI_MEMORY_TYPE type   = EFI_MEMORY_TYPE::EfiLoaderData;
         ST->BootServices->AllocatePool(type, bufferSize, (void **)&initArrayStart);
         for (int i = 0; i < initializers.ctorCount; i++) {
            EFI_PHYSICAL_ADDRESS ctorAddr =
               ConvertLittleEndianBytesToAddr(kernelData + offset + sizeof(void *) * i);
            ctorAddr          = TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelData, ctorAddr, vaddr);
            initArrayStart[i] = ctorAddr;
         }
      }
      if (headerArray[i].sh_type == SHT_FINI_ARRAY) {
         UINTN offset           = headerArray[i].sh_offset;
         UINTN bufferSize       = headerArray[i].sh_size;
         initializers.dtorCount = bufferSize / 8;
         EFI_MEMORY_TYPE type   = EFI_MEMORY_TYPE::EfiLoaderData;
         ST->BootServices->AllocatePool(type, bufferSize, (void **)&finiArrayStart);
         for (int i = 0; i < initializers.dtorCount; i++) {
            EFI_PHYSICAL_ADDRESS dtorAddr =
               ConvertLittleEndianBytesToAddr(kernelData + offset + sizeof(void *) * i);
            dtorAddr          = TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelData, dtorAddr, vaddr);
            finiArrayStart[i] = dtorAddr;
         }
      }
   }
   initializers.ctorAddresses = initArrayStart;
   initializers.dtorAddresses = finiArrayStart;
   return initializers;
}

/**
 * @brief Verify that a loaded file is a PC Screen Font file of the right version.
 * @param The PSF2 Header for the file you are verifying.
 *
 * @return True if the file header is correct.
 */
bool VerifyPSF2File(psf2_header header) {
   if (header.magic[0] != 0x72 || header.magic[1] != 0xb5 || header.magic[2] != 0x4a ||
       header.magic[3] != 0x86) {
      return false;
   }
   return true;
}

/** @brief Exits UEFI Boot services and provides a pointer to the memory map at the time of exit.
 * @param ImageHandle: The handle representing this EFI image.
 * @param OUTmapSize: Provides the size of the memory map. [OPTIONAL]
 * @return The memory map at the time of exiting Boot Services.
 */
MemoryMap ExitBootServices(EFI_HANDLE ImageHandle, UINTN *OUTmapSize) {
   // First allocate a buffer of 1 byte to get the actual size of the memory map.
   void *buffer             = nullptr;
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

   if (OUTmapSize != nullptr) {
      *OUTmapSize = memoryMapSize;
   }

   return map;
}

/** @brief Finds the highest resolution video mode this device supports.
 *
 * @return The UEFI mode number for the selected video mode, or -1 if an adequate mode could not be found.
 */
UINT32 GetVideoMode() {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = nullptr;
   ST->BootServices->LocateProtocol(&gopGUID, nullptr, (void **)&gopInterface);
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = nullptr;
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

/** @brief Initializes a framebuffer object for use by the kernel.
 * @param videoMode: The UEFI video mode to use for this framebuffer.
 *
 * @returns: A Framebuffer struct.
 */
Framebuffer SetUpFramebuffer(UINTN videoMode) {
   EFI_GUID gopGUID                           = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gopInterface = nullptr;
   ST->BootServices->LocateProtocol(&gopGUID, nullptr, (void **)&gopInterface);

   gopInterface->SetMode(gopInterface, videoMode);

   return {(void *)gopInterface->Mode->FrameBufferBase, gopInterface->Mode->Info->PixelsPerScanLine,
           gopInterface->Mode->Info->HorizontalResolution, gopInterface->Mode->Info->VerticalResolution};
}

extern "C" {
EFI_STATUS
EFIAPI
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
   int versionMajor = 0;
   int versionMinor = 3;
   int versionPatch = 0;

   ST = SystemTable;

   // Set initial screen state.
   int colors = 0b00011111;
   SystemTable->ConOut->SetAttribute(SystemTable->ConOut, colors);
   SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

   // Get interface for Loaded Image Protocol
   EFI_GUID loadedImageProtocolGUID                = EFI_LOADED_IMAGE_PROTOCOL_GUID;
   EFI_LOADED_IMAGE_PROTOCOL *loadedImageInterface = nullptr;
   SystemTable->BootServices->OpenProtocol(ImageHandle, &loadedImageProtocolGUID,
                                           (void **)&loadedImageInterface, ImageHandle, nullptr, 0x00000001);
   // Get the address this UEFI image was loaded at and print it for debugging purposes.
   UINT64 ImageBaseAddress = (UINT64)loadedImageInterface->ImageBase;
   println(L"Welcome to BhavaLoader v%d.%d.%d.", versionMajor, versionMinor, versionPatch);
   println(L"Copyright (©) 2021. Licensed under the MIT License.");
   println(L"This UEFI Image has been loaded at memory address: 0x%x", ImageBaseAddress);

   // Load kernel handle.
   EFI_FILE_PROTOCOL *kernelHandle =
      LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, L"LanternOS");
   if (!kernelHandle) {
      WaitForKey(L"Could not open ELF Kernel File..");
      return 1;
   }

   Elf64_Ehdr elfHeaderData     = ParseELFHeader(kernelHandle);
   Elf64_Phdr *elfProgramHeader = ParseELFPHeader(elfHeaderData, kernelHandle);

   KernelEntry kmain;
   GlobalInitializers globalObjCtorDtor;

   // TODO: Handle multiple PT_LOAD headers. The kernel doesn't have any now but it could in the future.
   for (int i = 0; i < elfHeaderData.e_phnum; i++) {
      if (elfProgramHeader[i].p_type == PT_LOAD) {
         void *kernelData = AllocatePagesForData(elfProgramHeader[i].p_memsz);
         if (!kernelData) {
            WaitForKey(L"Error: Could not allocate memory pages for kernel data!");
            return 1;
         }
         // Read in filesz bytes instead of memsz bytes because memsz includes .bss section which isnt
         // stored in the file on disk. If we used memsz we'd read in too much data (potentially).
         UINTN kernelDiskSize = elfProgramHeader[i].p_filesz;
         kernelHandle->SetPosition(kernelHandle, elfProgramHeader[i].p_offset);
         kernelHandle->Read(kernelHandle, &kernelDiskSize, (void *)kernelData);

         println(L"Kernel has been loaded into memory starting at address 0x%x.", kernelData);
         kmain = (KernelEntry)TranslateKernelAddress((EFI_PHYSICAL_ADDRESS)kernelData, elfHeaderData.e_entry,
                                                     elfProgramHeader[i].p_vaddr);
         println(L"Kernel is stored in %d 4KiB pages and its exact size in bytes is %d.",
                 GetDataPageSize(elfProgramHeader[i].p_memsz), elfProgramHeader[i].p_memsz);
         println(L"Entry point kmain for kernel is loaded in memory at address 0x%x", kmain);

         // Get section headers so we can retrieve our global constructors and destructors.
         Elf64_Shdr *sectionHeaders = ParseELFSHeader(elfHeaderData, kernelHandle);
         if (!sectionHeaders) {
            break;
         }
         globalObjCtorDtor = ParseGlobalInitializers(sectionHeaders, elfHeaderData.e_shnum,
                                                     (UINT8 *)kernelData, elfProgramHeader[i].p_vaddr);
      }
   }

   // Set up PC Screen Font.
   EFI_FILE_PROTOCOL *fontHandle =
      LoadRootDirFile(loadedImageInterface->DeviceHandle, ImageHandle, L"font.psf");
   psf2_header psf2Header = ParsePSF2Header(fontHandle);
   if (!VerifyPSF2File(psf2Header)) {
      WaitForKey(L"Error: PC Screen Font file not recognized as PSF Version 2!");
      return 1;
   }
   UINTN psf2fileSize = GetFileSize(fontHandle);
   fontHandle->SetPosition(fontHandle, psf2Header.headerSize);
   void *fontData = AllocatePagesForData(psf2Header.charSize * psf2Header.length);
   if (!fontData) {
      WaitForKey(L"Error: Could not allocate pages for PC Screen Font data!");
      return 1;
   }
   fontHandle->Read(fontHandle, &psf2fileSize, (void **)fontData);
   FontFormat fontFormat = {fontData, psf2Header.length, psf2Header.charSize, psf2Header.height,
                            psf2Header.width};

   println(L"font.psf has been loaded into memory starting at address 0x%x.", fontData);
   println(L"Font Data is stored in %d 4KiB pages and its exact size in bytes is %d.",
           GetDataPageSize(psf2Header.charSize * psf2Header.length), psf2Header.charSize * psf2Header.length);

   // Get a suitable videomode.
   UINT32 videoMode = GetVideoMode();
   if (videoMode == -1) {
      WaitForKey(L"Could not find suitable video mode.");
      return 1;
   }

   WaitForKey(L"Ready to transfer control to kernel. Press any key to continue...");
   Framebuffer framebuffer = SetUpFramebuffer(videoMode);

   // Cleanup
   SystemTable->BootServices->CloseProtocol(ImageHandle, &loadedImageProtocolGUID, ImageHandle, NULL);
   MemoryMap uefiMemoryMap = ExitBootServices(ImageHandle, nullptr);

   // Execute kernel
   int ret = kmain(framebuffer, fontFormat, globalObjCtorDtor);

   return EFI_SUCCESS;
}
}