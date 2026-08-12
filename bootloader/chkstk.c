/*
 * Stack probe implementation for Windows ABI compatibility
 * Required when compiling with MSVC target
 */

void __chkstk(void) {
    __asm__ volatile(
        "pop %%rcx\n"
        "xor %%rax, %%rax\n"
        "1:\n"
        "sub $0x1000, %%rax\n"
        "test %%rax, (%%rsp, %%rax)\n"
        "cmp %%rcx, %%rax\n"
        "jg 1b\n"
        "sub %%rcx, %%rsp\n"
        "test %%rax, (%%rsp)\n"
        "add %%rcx, %%rsp\n"
        "jmp *%%rcx"
        :
        :
        : "rax", "rcx", "memory"
    );
}
