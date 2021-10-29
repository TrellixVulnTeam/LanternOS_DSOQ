#pragma once
#define EI_NIDENT 16
#include "stdint.h"

#define PT_LOAD 1

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint64_t Elf64_Xword;

struct Elf64_Ehdr {
   unsigned char e_ident[EI_NIDENT];
   Elf64_Half e_type;
   Elf64_Half e_machine;
   Elf64_Word e_version;
   Elf64_Addr e_entry;
   Elf64_Off e_phoff;
   Elf64_Off e_shoff;
   Elf64_Word e_flags;
   Elf64_Half e_ehsize;
   Elf64_Half e_phentsize;
   Elf64_Half e_phnum;
   Elf64_Half e_shentsize;
   Elf64_Half e_shnum;
   Elf64_Half e_shstrndx;
};

struct Elf64_Phdr {
   Elf64_Word p_type;
   Elf64_Word p_flags;
   Elf64_Off p_offset;
   Elf64_Addr p_vaddr;
   Elf64_Addr p_paddr;
   Elf64_Addr p_filesz;
   Elf64_Addr p_memsz;
   Elf64_Addr p_align;
};

typedef struct {
   Elf64_Word sh_name;
   Elf64_Word sh_type;
   Elf64_Xword sh_flags;
   Elf64_Addr sh_addr;
   Elf64_Off sh_offset;
   Elf64_Xword sh_size;
   Elf64_Word sh_link;
   Elf64_Word sh_info;
   Elf64_Xword sh_addralign;
   Elf64_Xword sh_entsize;
} Elf64_Shdr;

#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
