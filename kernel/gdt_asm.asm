BITS 64
default rel

section .text

global gdt_flush
global tss_flush

; void gdt_flush(uint64_t gdt_ptr_addr);
gdt_flush:
    lgdt [rdi]
    
    ; Reload segment registers
    mov ax, 0x10      ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far return to reload CS
    pop rdi           ; Save return address
    mov rax, 0x08     ; Kernel code segment
    push rax
    push rdi
    retfq

; void tss_flush(uint16_t tss_selector);
tss_flush:
    ltr di
    ret
