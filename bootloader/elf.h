#ifndef ELF_H
#define ELF_H

#include "efi.h"

#define ELF64_MAGIC_0 0x7F
#define ELF64_MAGIC_1 'E'
#define ELF64_MAGIC_2 'L'
#define ELF64_MAGIC_3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_EXEC 2
#define ET_DYN  3

#define EM_X86_64 62

#define PT_NULL    0
#define PT_LOAD    1

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} ELF64_EHDR;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} ELF64_PHDR;

typedef struct {
    UINT64 image_base;
    UINT64 image_end;
    UINT64 entry_point;
    UINT64 loadable_segments;
} ELF_LOAD_RESULT;

EFI_STATUS load_kernel_elf_from_path(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *st,
    CHAR16 *kernel_path,
    EFI_PHYSICAL_ADDRESS reserved_base,
    UINT64 reserved_size,
    ELF_LOAD_RESULT *result
);

#endif
