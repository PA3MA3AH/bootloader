CC_BOOT = clang
LD_BOOT = lld-link
CC_KERNEL = clang
LD_KERNEL = ld.lld
NASM = nasm

BOOT_CFLAGS = \
	-target x86_64-pc-windows-msvc \
	-ffreestanding \
	-fno-stack-protector \
	-fshort-wchar \
	-mno-red-zone \
	-Wall -Wextra

BOOT_LDFLAGS = \
	/subsystem:efi_application \
	/entry:efi_main \
	/nodefaultlib \
	/out:image/EFI/BOOT/BOOTX64.EFI

KERNEL_CFLAGS = \
	-target x86_64-unknown-none-elf \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-mno-red-zone \
	-Wall -Wextra

KERNEL_LDFLAGS = \
	-nostdlib -static -z max-page-size=0x1000 \
	-T kernel/linker.ld

OVMF_CODE = /usr/share/edk2/x64/OVMF_CODE.4m.fd
OVMF_VARS_TEMPLATE = /usr/share/edk2/x64/OVMF_VARS.4m.fd
OVMF_VARS_LOCAL = build/OVMF_VARS.4m.fd

BOOT_OBJS = \
	build/main.obj \
	build/config.obj \
	build/menu.obj \
	build/elf.obj

KERNEL_OBJS = \
	kernel/kernel.o \
	kernel/framebuffer.o \
	kernel/console.o \
	kernel/memory_map.o \
	kernel/pmm.o \
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

$(OVMF_VARS_LOCAL): | build
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS_LOCAL)

build/main.obj: bootloader/main.c bootloader/efi.h bootloader/config.h bootloader/menu.h bootloader/elf.h common/bootinfo.h | build
	$(CC_BOOT) $(BOOT_CFLAGS) -c bootloader/main.c -o $@

build/config.obj: bootloader/config.c bootloader/config.h bootloader/efi.h | build
	$(CC_BOOT) $(BOOT_CFLAGS) -c bootloader/config.c -o $@

build/menu.obj: bootloader/menu.c bootloader/menu.h bootloader/config.h bootloader/efi.h | build
	$(CC_BOOT) $(BOOT_CFLAGS) -c bootloader/menu.c -o $@

build/elf.obj: bootloader/elf.c bootloader/elf.h bootloader/efi.h | build
	$(CC_BOOT) $(BOOT_CFLAGS) -c bootloader/elf.c -o $@

image/EFI/BOOT/BOOTX64.EFI: $(BOOT_OBJS)
	$(LD_BOOT) $(BOOT_LDFLAGS) $(BOOT_OBJS)

kernel/%.o: kernel/%.c
	$(CC_KERNEL) $(KERNEL_CFLAGS) -c $< -o $@

kernel/isr_stubs.o: kernel/isr_stubs.asm
	$(NASM) -f elf64 $< -o $@

image/EFI/BOOT/KERNEL.ELF: $(KERNEL_OBJS)
	$(LD_KERNEL) $(KERNEL_LDFLAGS) -o $@ $(KERNEL_OBJS)

run: all $(OVMF_VARS_LOCAL)
	qemu-system-x86_64 \
		-machine q35 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS_LOCAL) \
		-drive format=raw,file=fat:rw:image \
		-no-reboot -no-shutdown \
		-d int,cpu_reset,guest_errors \
		-D qemu.log

rebuild: clean all

clean:
	rm -rf build qemu.log image/EFI/BOOT/BOOTX64.EFI image/EFI/BOOT/KERNEL.ELF kernel/*.o
