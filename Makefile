CC              := clang
LD_EFI          := lld-link
LD_KERNEL       := ld.lld
NASM            := nasm

BOOT_CFLAGS     := -target x86_64-pc-windows-msvc -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra
KERNEL_CFLAGS   := -target x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -Wall -Wextra
NASMFLAGS       := -f elf64

EFI_LDFLAGS     := /subsystem:efi_application /entry:efi_main /nodefaultlib /out:image/EFI/BOOT/BOOTX64.EFI
KERNEL_LDFLAGS  := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld -o image/EFI/BOOT/KERNEL.ELF

OVMF_CODE       := /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS_SRC   := /usr/share/edk2/x64/OVMF_VARS.4m.fd
OVMF_VARS_DST   := build/OVMF_VARS.4m.fd

BOOT_OBJS := \
	build/main.obj \
	build/config.obj \
	build/menu.obj \
	build/elf.obj

KERNEL_OBJS := \
	kernel/kernel.o \
	kernel/framebuffer.o \
	kernel/console.o \
	kernel/memory_map.o \
	kernel/pmm.o \
	kernel/vmm.o \
	kernel/idt.o \
	kernel/interrupts.o \
	kernel/keyboard.o \
	kernel/shell.o \
	kernel/panic.o \
	kernel/crashlog.o \
	kernel/kheap.o \
	kernel/pci.o \
	kernel/e1000.o \
	kernel/pic.o \
	kernel/pit.o \
	kernel/exceptions.o \
	kernel/isr_stubs.o

.PHONY: all build bootloader kernel run clean rebuild

all: build bootloader kernel

build:
	mkdir -p build image/EFI/BOOT

bootloader: image/EFI/BOOT/BOOTX64.EFI

kernel: image/EFI/BOOT/KERNEL.ELF

$(OVMF_VARS_DST): | build
	cp $(OVMF_VARS_SRC) $(OVMF_VARS_DST)

build/main.obj: bootloader/main.c bootloader/efi.h bootloader/config.h bootloader/menu.h bootloader/elf.h common/bootinfo.h | build
	$(CC) $(BOOT_CFLAGS) -c bootloader/main.c -o $@

build/config.obj: bootloader/config.c bootloader/config.h bootloader/efi.h | build
	$(CC) $(BOOT_CFLAGS) -c bootloader/config.c -o $@

build/menu.obj: bootloader/menu.c bootloader/menu.h bootloader/config.h bootloader/efi.h | build
	$(CC) $(BOOT_CFLAGS) -c bootloader/menu.c -o $@

build/elf.obj: bootloader/elf.c bootloader/elf.h bootloader/efi.h | build
	$(CC) $(BOOT_CFLAGS) -c bootloader/elf.c -o $@

image/EFI/BOOT/BOOTX64.EFI: $(BOOT_OBJS)
	$(LD_EFI) $(EFI_LDFLAGS) $(BOOT_OBJS)

kernel/kernel.o: kernel/kernel.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/kernel.c -o $@

kernel/framebuffer.o: kernel/framebuffer.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/framebuffer.c -o $@

kernel/console.o: kernel/console.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/console.c -o $@

kernel/memory_map.o: kernel/memory_map.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/memory_map.c -o $@

kernel/pmm.o: kernel/pmm.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/pmm.c -o $@

kernel/vmm.o: kernel/vmm.c kernel/vmm.h | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/vmm.c -o $@

kernel/idt.o: kernel/idt.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/idt.c -o $@

kernel/interrupts.o: kernel/interrupts.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/interrupts.c -o $@

kernel/keyboard.o: kernel/keyboard.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/keyboard.c -o $@

kernel/shell.o: kernel/shell.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/shell.c -o $@

kernel/panic.o: kernel/panic.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/panic.c -o $@

kernel/crashlog.o: kernel/crashlog.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/crashlog.c -o $@

kernel/kheap.o: kernel/kheap.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/kheap.c -o $@

kernel/pci.o: kernel/pci.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/pci.c -o $@

kernel/e1000.o: kernel/e1000.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/e1000.c -o $@

kernel/pic.o: kernel/pic.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/pic.c -o $@

kernel/pit.o: kernel/pit.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/pit.c -o $@

kernel/exceptions.o: kernel/exceptions.c | build
	$(CC) $(KERNEL_CFLAGS) -c kernel/exceptions.c -o $@

kernel/isr_stubs.o: kernel/isr_stubs.asm | build
	$(NASM) $(NASMFLAGS) kernel/isr_stubs.asm -o $@

image/EFI/BOOT/KERNEL.ELF: $(KERNEL_OBJS)
	$(LD_KERNEL) $(KERNEL_LDFLAGS) $(KERNEL_OBJS)

run: all $(OVMF_VARS_DST)
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS_DST) \
		-drive format=raw,file=fat:rw:image \
		-no-reboot -no-shutdown \
		-d int,cpu_reset,guest_errors \
		-D qemu.log

rebuild: clean all

clean:
	rm -rf build qemu.log image/EFI/BOOT/BOOTX64.EFI image/EFI/BOOT/KERNEL.ELF kernel/*.o
