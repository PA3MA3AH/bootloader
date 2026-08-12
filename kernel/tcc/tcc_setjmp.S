; x86_64 setjmp / longjmp for TCC freestanding
; jmp_buf layout: rbx, rbp, r12-r15, rsp, rip  (8 * 8 = 64 bytes)

bits 64
default rel

global tcc_setjmp
global tcc_longjmp

section .text

; int tcc_setjmp(jmp_buf env)
;   env in rdi, returns 0 on save, non-zero on restore
tcc_setjmp:
    mov [rdi],       rbx
    mov [rdi + 8],   rbp
    mov [rdi + 16],  r12
    mov [rdi + 24],  r13
    mov [rdi + 32],  r14
    mov [rdi + 40],  r15
    lea rax, [rsp + 8]       ; save rsp (points to return address)
    mov [rdi + 48],  rax
    mov rax, [rsp]            ; save rip (return address)
    mov [rdi + 56],  rax
    xor eax, eax
    ret

; void tcc_longjmp(jmp_buf env, int val)
;   env in rdi, val in esi
tcc_longjmp:
    mov rbx, [rdi]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    mov rax, [rdi + 56]
    mov eax, esi
    test esi, esi
    jnz .ret
    mov eax, 1          ; longjmp never returns 0
.ret:
    jmp rax
