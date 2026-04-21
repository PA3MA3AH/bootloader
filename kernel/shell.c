#include "shell.h"
#include "io.h"
#include "keyboard.h"
#include "panic.h"
#include "interrupts.h"
#include "crashlog.h"
#include "kheap.h"
#include "pci.h"
#include "e1000.h"

#define SHELL_PIT_HZ 100
#define SHELL_HISTORY_MAX 16

static int g_pit_enabled = 0;

static char g_history[SHELL_HISTORY_MAX][SHELL_INPUT_MAX];
static uint32_t g_history_count = 0;

static int g_history_browsing = 0;
static int g_history_index = -1;
static char g_history_saved_input[SHELL_INPUT_MAX];
static uint32_t g_history_saved_length = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static int str_to_u32(const char *s, uint32_t *out_value) {
    uint32_t value = 0;
    int has_digits = 0;

    if (!s || !*s || !out_value) {
        return 0;
    }

    while (*s) {
        if (*s < '0' || *s > '9') {
            return 0;
        }

        has_digits = 1;
        value = value * 10U + (uint32_t)(*s - '0');
        s++;
    }

    if (!has_digits) {
        return 0;
    }

    *out_value = value;
    return 1;
}

static int parse_three_u32_args(const char *args,
                                uint32_t *a,
                                uint32_t *b,
                                uint32_t *c) {
    char part0[16];
    char part1[16];
    char part2[16];
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t k = 0;

    if (!args || !a || !b || !c) {
        return 0;
    }

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(part0)) {
        part0[i++] = *args++;
    }
    part0[i] = '\0';

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && j + 1 < sizeof(part1)) {
        part1[j++] = *args++;
    }
    part1[j] = '\0';

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && k + 1 < sizeof(part2)) {
        part2[k++] = *args++;
    }
    part2[k] = '\0';

    while (*args == ' ') {
        args++;
    }

    if (*args != '\0') {
        return 0;
    }

    if (!str_to_u32(part0, a)) {
        return 0;
    }
    if (!str_to_u32(part1, b)) {
        return 0;
    }
    if (!str_to_u32(part2, c)) {
        return 0;
    }

    return 1;
}

static int parse_four_u32_args(const char *args,
                               uint32_t *a,
                               uint32_t *b,
                               uint32_t *c,
                               uint32_t *d) {
    char part0[16];
    char part1[16];
    char part2[16];
    char part3[16];
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t k = 0;
    uint32_t l = 0;

    if (!args || !a || !b || !c || !d) {
        return 0;
    }

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(part0)) {
        part0[i++] = *args++;
    }
    part0[i] = '\0';

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && j + 1 < sizeof(part1)) {
        part1[j++] = *args++;
    }
    part1[j] = '\0';

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && k + 1 < sizeof(part2)) {
        part2[k++] = *args++;
    }
    part2[k] = '\0';

    while (*args == ' ') {
        args++;
    }

    while (*args && *args != ' ' && l + 1 < sizeof(part3)) {
        part3[l++] = *args++;
    }
    part3[l] = '\0';

    while (*args == ' ') {
        args++;
    }

    if (*args != '\0') {
        return 0;
    }

    if (!str_to_u32(part0, a)) {
        return 0;
    }
    if (!str_to_u32(part1, b)) {
        return 0;
    }
    if (!str_to_u32(part2, c)) {
        return 0;
    }
    if (!str_to_u32(part3, d)) {
        return 0;
    }

    return 1;
}

static void str_copy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (max_len == 0) {
        return;
    }

    while (i + 1 < max_len && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void shell_reset_input(SHELL *sh) {
    sh->length = 0;
    sh->input[0] = '\0';
}

static void shell_history_reset_navigation(void) {
    g_history_browsing = 0;
    g_history_index = -1;
    g_history_saved_input[0] = '\0';
    g_history_saved_length = 0;
}

static void shell_history_add(const char *line) {
    if (!line || !line[0]) {
        return;
    }

    if (g_history_count > 0 && str_eq(g_history[g_history_count - 1], line)) {
        return;
    }

    if (g_history_count < SHELL_HISTORY_MAX) {
        str_copy(g_history[g_history_count], line, SHELL_INPUT_MAX);
        g_history_count++;
        return;
    }

    for (uint32_t i = 1; i < SHELL_HISTORY_MAX; i++) {
        str_copy(g_history[i - 1], g_history[i], SHELL_INPUT_MAX);
    }

    str_copy(g_history[SHELL_HISTORY_MAX - 1], line, SHELL_INPUT_MAX);
}

static void shell_replace_input_line(SHELL *sh, const char *text) {
    while (sh->length > 0) {
        sh->length--;
        sh->input[sh->length] = '\0';
        console_backspace(sh->con);
    }

    if (!text) {
        return;
    }

    while (*text && sh->length + 1 < SHELL_INPUT_MAX) {
        sh->input[sh->length++] = *text;
        sh->input[sh->length] = '\0';
        console_putchar(sh->con, *text);
        text++;
    }
}

static void shell_history_up(SHELL *sh) {
    if (g_history_count == 0) {
        return;
    }

    if (!g_history_browsing) {
        str_copy(g_history_saved_input, sh->input, SHELL_INPUT_MAX);
        g_history_saved_length = sh->length;
        g_history_browsing = 1;
        g_history_index = (int)g_history_count;
    }

    if (g_history_index > 0) {
        g_history_index--;
    }

    shell_replace_input_line(sh, g_history[g_history_index]);
}

static void shell_history_down(SHELL *sh) {
    if (!g_history_browsing) {
        return;
    }

    if (g_history_index < (int)g_history_count - 1) {
        g_history_index++;
        shell_replace_input_line(sh, g_history[g_history_index]);
        return;
    }

    shell_replace_input_line(sh, g_history_saved_input);
    sh->length = g_history_saved_length;
    shell_history_reset_navigation();
}

static void shell_print_help(SHELL *sh) {
    console_printf(sh->con, "Commands:\n");
    console_printf(sh->con, "  help              - show this help\n");
    console_printf(sh->con, "  clear             - clear screen\n");
    console_printf(sh->con, "  mem               - show boot memory info\n");
    console_printf(sh->con, "  pmm               - show PMM info\n");
    console_printf(sh->con, "  kmem              - show kernel heap stats\n");
    console_printf(sh->con, "  ktest             - basic kmalloc/kfree test\n");
    console_printf(sh->con, "  pci               - scan PCI bus and print devices\n");
    console_printf(sh->con, "  pcidump b d f     - dump one PCI device config summary\n");
    console_printf(sh->con, "  e1000             - probe and init Intel e1000/e1000e device\n");
    console_printf(sh->con, "  e1000dump         - print extended e1000 debug registers\n");
    console_printf(sh->con, "  e1000rings        - initialize e1000 RX/TX rings\n");
    console_printf(sh->con, "  e1000tx           - send one test broadcast Ethernet frame\n");
    console_printf(sh->con, "  arpwho a b c d    - send ARP request to target IPv4\n");
    console_printf(sh->con, "  ticks             - show PIT status and tick counter\n");
    console_printf(sh->con, "  uptime            - show uptime based on PIT ticks\n");
    console_printf(sh->con, "  piton             - enable PIT IRQ0 timer\n");
    console_printf(sh->con, "  pitoff            - disable PIT IRQ0 timer\n");
    console_printf(sh->con, "  echo ...          - print text\n");
    console_printf(sh->con, "  panic             - trigger kernel panic\n");
    console_printf(sh->con, "  halt              - stop CPU\n");
    console_printf(sh->con, "  reboot            - reboot machine\n");
    console_printf(sh->con, "History:\n");
    console_printf(sh->con, "  Arrow Up          - previous command\n");
    console_printf(sh->con, "  Arrow Down        - next command\n");
}

static void shell_print_mem(SHELL *sh) {
    BOOT_INFO *b = sh->boot_info;

    console_printf(sh->con, "Boot memory info:\n");
    console_printf(sh->con, "  usable memory bytes: %u\n", (unsigned int)b->usable_memory_bytes);
    console_printf(sh->con, "  kernel phys base:    %p\n", (void*)(uintptr_t)b->kernel_phys_base);
    console_printf(sh->con, "  kernel reserved:     %u\n", (unsigned int)b->kernel_reserved_size);
    console_printf(sh->con, "  bootinfo phys:       %p\n", (void*)(uintptr_t)b->bootinfo_phys);
    console_printf(sh->con, "  scratch phys:        %p\n", (void*)(uintptr_t)b->scratch_phys);
    console_printf(sh->con, "  stack bottom:        %p\n", (void*)(uintptr_t)b->kernel_stack_bottom);
    console_printf(sh->con, "  stack top:           %p\n", (void*)(uintptr_t)b->kernel_stack_top);
    console_printf(sh->con, "  heap base:           %p\n", (void*)(uintptr_t)b->heap_base);
    console_printf(sh->con, "  heap size:           %u\n", (unsigned int)b->heap_size);
    console_printf(sh->con, "  crash info phys:     %p\n", (void*)(uintptr_t)b->crash_info_phys);
    console_printf(sh->con, "  crash info size:     %u\n", (unsigned int)b->crash_info_size);
    console_printf(sh->con, "  memory map:          %p\n", (void*)(uintptr_t)b->memory_map);
    console_printf(sh->con, "  memory map size:     %u\n", (unsigned int)b->memory_map_size);
    console_printf(sh->con, "  descriptor size:     %u\n", (unsigned int)b->memory_descriptor_size);
}

static void shell_print_pmm(SHELL *sh) {

    console_printf(sh->con, "PMM state:\n");
    console_printf(sh->con, "  region count: %u\n", (unsigned int)pmm_region_count());
    console_printf(sh->con, "  managed pages:%u\n", (unsigned int)pmm_managed_pages());
    console_printf(sh->con, "  bitmap bytes: %u\n", (unsigned int)pmm_bitmap_bytes());
    console_printf(sh->con, "  total pages:  %u\n", (unsigned int)pmm_total_pages());
    console_printf(sh->con, "  free pages:   %u\n", (unsigned int)pmm_free_pages());
    console_printf(sh->con, "  used pages:   %u\n", (unsigned int)pmm_used_pages());
}

static void shell_print_kmem(SHELL *sh) {
    KHEAP_STATS stats;

    kheap_get_stats(&stats);

    console_printf(sh->con, "Kernel heap stats:\n");
    console_printf(sh->con, "  heap base:         %p\n", (void*)(uintptr_t)stats.heap_base);
    console_printf(sh->con, "  heap size:         %u\n", (unsigned int)stats.heap_size);
    console_printf(sh->con, "  used bytes:        %u\n", (unsigned int)stats.used_bytes);
    console_printf(sh->con, "  free bytes:        %u\n", (unsigned int)stats.free_bytes);
    console_printf(sh->con, "  allocations:       %u\n", (unsigned int)stats.allocation_count);
    console_printf(sh->con, "  free blocks:       %u\n", (unsigned int)stats.free_block_count);
}

static void shell_run_ktest(SHELL *sh) {
    void *a;
    void *b;
    void *c;

    console_printf(sh->con, "Running kheap test...\n");

    a = kmalloc(64);
    b = kmalloc(128);
    c = kcalloc(16, 8);

    console_printf(sh->con, "  kmalloc(64)   -> %p\n", a);
    console_printf(sh->con, "  kmalloc(128)  -> %p\n", b);
    console_printf(sh->con, "  kcalloc(...)  -> %p\n", c);

    if (!a || !b || !c) {
        console_printf(sh->con, "  allocation failed\n");
        return;
    }

    b = krealloc(b, 256);
    console_printf(sh->con, "  krealloc(...) -> %p\n", b);

    kfree(a);
    kfree(b);
    kfree(c);

    console_printf(sh->con, "  freed all test allocations\n");
}

static void shell_run_pci_scan(SHELL *sh) {
    pci_scan_and_print(sh->con);
}

static void shell_run_pcidump(SHELL *sh, const char *args) {
    uint32_t bus;
    uint32_t device;
    uint32_t function;

    if (!parse_three_u32_args(args, &bus, &device, &function)) {
        console_printf(sh->con, "Usage: pcidump <bus> <device> <function>\n");
        return;
    }

    if (bus > 255 || device > 31 || function > 7) {
        console_printf(sh->con, "pcidump: values out of range (bus 0..255, device 0..31, function 0..7)\n");
        return;
    }

    pci_dump_device(sh->con, (uint8_t)bus, (uint8_t)device, (uint8_t)function);
}

static void shell_run_e1000(SHELL *sh) {
    E1000_INFO info;

    if (!e1000_probe(&info)) {
        console_printf(sh->con, "e1000: supported Intel device not found\n");
        return;
    }

    console_printf(sh->con, "e1000: device found at %u:%u.%u, initializing...\n",
                   (unsigned int)info.bus,
                   (unsigned int)info.device,
                   (unsigned int)info.function);

    if (!e1000_init(&info)) {
        console_printf(sh->con, "e1000: init failed\n");
        return;
    }

    e1000_print_info(sh->con, &info);
}

static void shell_run_e1000dump(SHELL *sh) {
    E1000_INFO info;

    if (!e1000_probe(&info)) {
        console_printf(sh->con, "e1000dump: supported Intel device not found\n");
        return;
    }

    e1000_refresh(&info);
    e1000_print_info(sh->con, &info);
    e1000_dump_registers(sh->con, &info);
}

static void shell_run_e1000rings(SHELL *sh) {
    E1000_INFO info;

    if (!e1000_probe(&info)) {
        console_printf(sh->con, "e1000rings: supported Intel device not found\n");
        return;
    }

    console_printf(sh->con, "e1000rings: initializing RX/TX descriptor rings...\n");

    if (!e1000_init_rings(&info)) {
        console_printf(sh->con, "e1000rings: ring init failed\n");
        return;
    }

    e1000_print_info(sh->con, &info);
    e1000_dump_registers(sh->con, &info);
}

static void shell_run_e1000tx(SHELL *sh) {
    E1000_INFO info;

    if (!e1000_probe(&info)) {
        console_printf(sh->con, "e1000tx: supported Intel device not found\n");
        return;
    }

    if (!e1000_init_rings(&info)) {
        console_printf(sh->con, "e1000tx: ring init failed\n");
        return;
    }

    console_printf(sh->con, "e1000tx: sending test broadcast frame...\n");

    if (!e1000_send_test_packet(&info)) {
        console_printf(sh->con, "e1000tx: send failed or timed out\n");
        e1000_dump_registers(sh->con, &info);
        return;
    }

    console_printf(sh->con, "e1000tx: packet queued/sent successfully\n");
    e1000_dump_registers(sh->con, &info);
}

static void shell_run_arpwho(SHELL *sh, const char *args) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint8_t target_ip[4];
    E1000_INFO info;

    if (!parse_four_u32_args(args, &a, &b, &c, &d)) {
        console_printf(sh->con, "Usage: arpwho <a> <b> <c> <d>\n");
        return;
    }

    if (a > 255 || b > 255 || c > 255 || d > 255) {
        console_printf(sh->con, "arpwho: each octet must be in range 0..255\n");
        return;
    }

    target_ip[0] = (uint8_t)a;
    target_ip[1] = (uint8_t)b;
    target_ip[2] = (uint8_t)c;
    target_ip[3] = (uint8_t)d;

    if (!e1000_probe(&info)) {
        console_printf(sh->con, "arpwho: supported Intel device not found\n");
        return;
    }

    if (!e1000_init_rings(&info)) {
        console_printf(sh->con, "arpwho: e1000 ring init failed\n");
        return;
    }

    console_printf(sh->con,
                   "arpwho: sending ARP request from 10.0.2.15 to %u.%u.%u.%u...\n",
                   (unsigned int)target_ip[0],
                   (unsigned int)target_ip[1],
                   (unsigned int)target_ip[2],
                   (unsigned int)target_ip[3]);

    if (!e1000_send_arp_request(&info, target_ip)) {
        console_printf(sh->con, "arpwho: send failed or timed out\n");
        e1000_dump_registers(sh->con, &info);
        return;
    }

    console_printf(sh->con, "arpwho: ARP request queued/sent successfully\n");
    e1000_dump_registers(sh->con, &info);
}

static void shell_print_ticks(SHELL *sh) {
    console_printf(sh->con, "PIT status: %s\n", g_pit_enabled ? "enabled" : "disabled");
    console_printf(sh->con, "PIT hz:     %u\n", (unsigned int)timer_hz());
    console_printf(sh->con, "PIT ticks:  %u\n", (unsigned int)timer_ticks());
}

static void shell_print_uptime(SHELL *sh) {
    uint32_t hz = timer_hz();
    uint64_t ticks = timer_ticks();

    if (hz == 0) {
        console_printf(sh->con, "Uptime unavailable: PIT timer is disabled.\n");
        return;
    }

    uint64_t total_seconds = ticks / hz;
    uint64_t milliseconds = (ticks % hz) * 1000ULL / hz;
    uint64_t hours = total_seconds / 3600ULL;
    uint64_t minutes = (total_seconds % 3600ULL) / 60ULL;
    uint64_t seconds = total_seconds % 60ULL;

    console_printf(sh->con, "Uptime: %u:%u:%u.%u\n",
                   (unsigned int)hours,
                   (unsigned int)minutes,
                   (unsigned int)seconds,
                   (unsigned int)milliseconds);
    console_printf(sh->con, "Source: %u ticks at %u Hz\n",
                   (unsigned int)ticks,
                   (unsigned int)hz);
}

static void shell_enable_pit(SHELL *sh) {
    if (g_pit_enabled) {
        console_printf(sh->con, "PIT IRQ0 timer is already enabled at %u Hz.\n",
                       (unsigned int)timer_hz());
        return;
    }

    crashlog_set_stage("shell runtime");
    crashlog_set_action("piton", "before timer_start");

    interrupts_disable();
    timer_start(SHELL_PIT_HZ);

    crashlog_set_action("piton", "before sti");
    interrupts_enable();

    crashlog_set_action("piton", "after sti");
    crashlog_mark_stable();

    g_pit_enabled = 1;

    console_printf(sh->con, "PIT IRQ0 timer enabled at %u Hz.\n",
                   (unsigned int)SHELL_PIT_HZ);
}

static void shell_disable_pit(SHELL *sh) {
    if (!g_pit_enabled) {
        console_printf(sh->con, "PIT IRQ0 timer is already disabled.\n");
        return;
    }

    timer_stop();
    crashlog_mark_stable();

    g_pit_enabled = 0;

    console_printf(sh->con, "PIT IRQ0 timer disabled.\n");
}

static void shell_do_reboot(void) {
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static void shell_execute(SHELL *sh) {
    if (sh->length == 0) {
        return;
    }

    shell_history_add(sh->input);
    shell_history_reset_navigation();

    if (str_eq(sh->input, "help")) {
        shell_print_help(sh);
        return;
    }

    if (str_eq(sh->input, "clear")) {
        console_clear(sh->con);
        return;
    }

    if (str_eq(sh->input, "mem")) {
        shell_print_mem(sh);
        return;
    }

    if (str_eq(sh->input, "pmm")) {
        shell_print_pmm(sh);
        return;
    }

    if (str_eq(sh->input, "kmem")) {
        shell_print_kmem(sh);
        return;
    }

    if (str_eq(sh->input, "ktest")) {
        shell_run_ktest(sh);
        return;
    }

    if (str_eq(sh->input, "pci")) {
        shell_run_pci_scan(sh);
        return;
    }

    if (str_starts_with(sh->input, "pcidump ")) {
        shell_run_pcidump(sh, sh->input + 8);
        return;
    }

    if (str_eq(sh->input, "e1000")) {
        shell_run_e1000(sh);
        return;
    }

    if (str_eq(sh->input, "e1000dump")) {
        shell_run_e1000dump(sh);
        return;
    }

    if (str_eq(sh->input, "e1000rings")) {
        shell_run_e1000rings(sh);
        return;
    }

    if (str_eq(sh->input, "e1000tx")) {
        shell_run_e1000tx(sh);
        return;
    }

    if (str_starts_with(sh->input, "arpwho ")) {
        shell_run_arpwho(sh, sh->input + 7);
        return;
    }

    if (str_eq(sh->input, "ticks")) {
        shell_print_ticks(sh);
        return;
    }

    if (str_eq(sh->input, "uptime")) {
        shell_print_uptime(sh);
        return;
    }

    if (str_eq(sh->input, "piton")) {
        shell_enable_pit(sh);
        return;
    }

    if (str_eq(sh->input, "pitoff")) {
        shell_disable_pit(sh);
        return;
    }

    if (str_eq(sh->input, "panic")) {
        panic("Manual panic triggered from shell");
    }

    if (str_eq(sh->input, "halt")) {
        console_printf(sh->con, "CPU halted.\n");
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    if (str_eq(sh->input, "reboot")) {
        crashlog_mark_stable();
        console_printf(sh->con, "Rebooting...\n");
        shell_do_reboot();
        return;
    }

    if (str_starts_with(sh->input, "echo ")) {
        console_printf(sh->con, "%s\n", sh->input + 5);
        return;
    }

    if (str_eq(sh->input, "echo")) {
        console_printf(sh->con, "\n");
        return;
    }

    console_printf(sh->con, "Unknown command: %s\n", sh->input);
    console_printf(sh->con, "Type 'help' for available commands.\n");
}

void shell_init(SHELL *sh, CONSOLE *con, BOOT_INFO *boot_info) {
    sh->con = con;
    sh->boot_info = boot_info;
    g_pit_enabled = 0;
    g_history_count = 0;
    shell_history_reset_navigation();
    shell_reset_input(sh);
}

void shell_prompt(SHELL *sh) {
    console_printf(sh->con, "\n> ");
}

void shell_handle_char(SHELL *sh, char ch) {
    if ((unsigned char)ch >= 0x80) {
        switch ((unsigned char)ch) {
            case KEY_F1:
                console_printf(sh->con, "\n[F1 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F2:
                console_printf(sh->con, "\n[F2 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F3:
                console_printf(sh->con, "\n[F3 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F4:
                console_printf(sh->con, "\n[F4 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F5:
                console_printf(sh->con, "\n[F5 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F6:
                console_printf(sh->con, "\n[F6 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F7:
                console_printf(sh->con, "\n[F7 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F8:
                console_printf(sh->con, "\n[F8 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F9:
                console_printf(sh->con, "\n[F9 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F10:
                console_printf(sh->con, "\n[F10 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F11:
                console_printf(sh->con, "\n[F11 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_F12:
                console_printf(sh->con, "\n[F12 pressed]\n");
                shell_prompt(sh);
                shell_replace_input_line(sh, sh->input);
                break;
            case KEY_ARROW_UP:
                shell_history_up(sh);
                break;
            case KEY_ARROW_DOWN:
                shell_history_down(sh);
                break;
            default:
                break;
        }

        return;
    }

    if (ch == '\r') {
        return;
    }

    if (ch == '\b') {
        if (sh->length > 0) {
            sh->length--;
            sh->input[sh->length] = '\0';
            console_backspace(sh->con);
        }
        return;
    }

    if (ch == '\n') {
        console_putchar(sh->con, '\n');
        shell_execute(sh);
        shell_reset_input(sh);
        shell_prompt(sh);
        return;
    }

    if (ch == '\t') {
        return;
    }

    if (sh->length + 1 >= SHELL_INPUT_MAX) {
        return;
    }

    shell_history_reset_navigation();

    sh->input[sh->length++] = ch;
    sh->input[sh->length] = '\0';
    console_putchar(sh->con, ch);
}
