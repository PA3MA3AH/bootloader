#include "shell.h"
#include "io.h"
#include "keyboard.h"
#include "panic.h"
#include "interrupts.h"
#include "crashlog.h"
#include "kheap.h"
#include "pci.h"
#include "e1000.h"
#include "net.h"
#include "ahci.h"
#include "block.h"
#include "partition.h"
#include "fat32.h"

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
    console_printf(sh->con, "  alloc             - allocate and test one physical page\n");
    console_printf(sh->con, "  pf                - trigger a page fault for testing\n");
    console_printf(sh->con, "  kmem              - show kernel heap stats\n");
    console_printf(sh->con, "  ktest             - basic kmalloc/kfree test\n");
    console_printf(sh->con, "  pci               - scan PCI bus and print devices\n");
    console_printf(sh->con, "  pcidump b d f     - dump one PCI device config summary\n");
    console_printf(sh->con, "  fatinfo <part>   - show FAT32 info for a partition\n");
    console_printf(sh->con, "  ahci              - probe/init AHCI SATA controller\n");
    console_printf(sh->con, "  ahciports         - list AHCI ports and attached devices\n");
    console_printf(sh->con, "  ahciid <port>     - IDENTIFY DEVICE on one SATA port\n");
    console_printf(sh->con, "  readlba p lba n   - read 1..8 sectors from SATA disk\n");
    console_printf(sh->con, "  blk               - list generic block devices\n");
    console_printf(sh->con, "  blkid <dev>       - show block device info by index or name\n");
    console_printf(sh->con, "  blkread d lba n   - read 1..8 sectors via block layer\n");
    console_printf(sh->con, "  part              - list detected partitions\n");
    console_printf(sh->con, "  partscan          - rescan partitions on all disks\n");
    console_printf(sh->con, "  partinfo <part>   - show partition info by index or name\n");
    console_printf(sh->con, "  fatls <part> [p]  - list FAT32 directory (8.3 names)\n");
    console_printf(sh->con, "  fatcat <part> <p> - quick text preview for small files\n");
    console_printf(sh->con, "  fatwrite <part> <p> <text> - overwrite existing file\n");
    console_printf(sh->con, "  fatrename <part> <old> <new> - rename within same dir\n");
    console_printf(sh->con, "  fattouch <part> <p>  - create empty file\n");
    console_printf(sh->con, "  fatrm <part> <p>     - delete file\n");
    console_printf(sh->con, "  fatview <part> <p>- paged file viewer with line numbers\n");
    console_printf(sh->con, "  fatdump <part> <p>- hex dump first bytes of a file\n");
    console_printf(sh->con, "  fatstat <part> <p>- show FAT32 entry info\n");
    console_printf(sh->con, "  e1000             - probe and init Intel e1000/e1000e device\n");
    console_printf(sh->con, "  e1000dump         - print extended e1000 debug registers\n");
    console_printf(sh->con, "  e1000rings        - initialize e1000 RX/TX rings\n");
    console_printf(sh->con, "  e1000tx           - send one test broadcast Ethernet frame\n");
    console_printf(sh->con, "  arpwho a b c d    - send ARP request to target IPv4 (raw)\n");
    console_printf(sh->con, "  net               - bring up the IP stack on top of e1000\n");
    console_printf(sh->con, "  ipcfg             - print current IP configuration\n");
    console_printf(sh->con, "  ipset ip mask gw  - set local IPv4 / netmask / gateway\n");
    console_printf(sh->con, "  dnsset ip         - set DNS server IPv4 address\n");
    console_printf(sh->con, "  arp               - show ARP cache\n");
    console_printf(sh->con, "  arpclear          - empty the ARP cache\n");
    console_printf(sh->con, "  ping host         - ICMP echo to IPv4 or hostname (4 packets)\n");
    console_printf(sh->con, "  resolve name      - DNS A-record lookup\n");
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

/* ============================================================
 *  Network stack commands
 * ============================================================ */

static int shell_ensure_pit_running(SHELL *sh) {
    if (timer_hz() != 0) {
        return 1;
    }
    crashlog_set_stage("shell runtime");
    crashlog_set_action("net auto-piton", "before timer_start");
    interrupts_disable();
    timer_start(SHELL_PIT_HZ);
    interrupts_enable();
    g_pit_enabled = 1;
    crashlog_mark_stable();
    if (sh) {
        console_printf(sh->con, "(auto) PIT timer enabled at %u Hz for network stack\n",
                       (unsigned int)SHELL_PIT_HZ);
    }
    return 1;
}

static int shell_ensure_net(SHELL *sh, const char *cmd) {
    E1000_INFO *nic = e1000_get_state();

    if (!nic->present) {
        if (!e1000_probe(nic)) {
            console_printf(sh->con, "%s: e1000 device not found\n", cmd);
            return 0;
        }
    }
    if (!nic->rings_ready) {
        if (!e1000_init_rings(nic)) {
            console_printf(sh->con, "%s: e1000 init failed\n", cmd);
            return 0;
        }
    }

    shell_ensure_pit_running(sh);

    if (!net_init(nic)) {
        console_printf(sh->con, "%s: net_init failed\n", cmd);
        return 0;
    }
    return 1;
}

static void shell_run_net(SHELL *sh) {
    if (!shell_ensure_net(sh, "net")) {
        return;
    }
    console_printf(sh->con, "net: stack ready\n");
    net_print_config(sh->con);
}

static void shell_run_ipcfg(SHELL *sh) {
    net_print_config(sh->con);
}

static void shell_run_ipset(SHELL *sh, const char *args) {
    /* Usage: ipset <a.b.c.d> <netmask a.b.c.d> <gateway a.b.c.d> */
    char ip_s[24];
    char mask_s[24];
    char gw_s[24];
    uint32_t i = 0;
    uint32_t j = 0;

    if (!args) {
        console_printf(sh->con, "Usage: ipset <ip> <netmask> <gateway>\n");
        return;
    }

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(ip_s)) ip_s[j++] = args[i++];
    ip_s[j] = '\0';
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(mask_s)) mask_s[j++] = args[i++];
    mask_s[j] = '\0';
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(gw_s)) gw_s[j++] = args[i++];
    gw_s[j] = '\0';

    uint8_t ip[4], mask[4], gw[4];
    if (!net_parse_ipv4(ip_s, ip) ||
        !net_parse_ipv4(mask_s, mask) ||
        !net_parse_ipv4(gw_s, gw)) {
        console_printf(sh->con, "Usage: ipset <ip> <netmask> <gateway>\n");
        return;
    }

    if (!shell_ensure_net(sh, "ipset")) return;
    net_set_ip(ip, mask, gw);
    net_arp_clear();
    net_print_config(sh->con);
}

static void shell_run_dnsset(SHELL *sh, const char *args) {
    char buf[24];
    uint32_t i = 0;
    uint32_t j = 0;

    if (!args) {
        console_printf(sh->con, "Usage: dnsset <ip>\n");
        return;
    }
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(buf)) buf[j++] = args[i++];
    buf[j] = '\0';

    uint8_t ip[4];
    if (!net_parse_ipv4(buf, ip)) {
        console_printf(sh->con, "Usage: dnsset <ip>\n");
        return;
    }
    if (!shell_ensure_net(sh, "dnsset")) return;
    net_set_dns(ip);
    net_print_config(sh->con);
}

static void shell_run_arp(SHELL *sh) {
    net_arp_print(sh->con);
}

static void shell_run_arpclear(SHELL *sh) {
    net_arp_clear();
    console_printf(sh->con, "ARP cache cleared\n");
}

static void shell_run_ping(SHELL *sh, const char *args) {
    char host[64];
    uint32_t i = 0;
    uint32_t j = 0;
    uint8_t ip[4];

    if (!args) {
        console_printf(sh->con, "Usage: ping <host-or-ip>\n");
        return;
    }
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(host)) host[j++] = args[i++];
    host[j] = '\0';

    if (host[0] == '\0') {
        console_printf(sh->con, "Usage: ping <host-or-ip>\n");
        return;
    }

    if (!shell_ensure_net(sh, "ping")) return;

    if (!net_parse_ipv4(host, ip)) {
        console_printf(sh->con, "ping: resolving %s ...\n", host);
        if (!net_dns_resolve(sh->con, host, ip, 2000)) {
            console_printf(sh->con, "ping: cannot resolve '%s'\n", host);
            return;
        }
        console_printf(sh->con, "ping: %s -> %u.%u.%u.%u\n",
                       host,
                       (unsigned int)ip[0], (unsigned int)ip[1],
                       (unsigned int)ip[2], (unsigned int)ip[3]);
    }

    net_ping(sh->con, ip, 4, 1000, 0);
}

static void shell_run_resolve(SHELL *sh, const char *args) {
    char host[64];
    uint32_t i = 0;
    uint32_t j = 0;
    uint8_t ip[4];

    if (!args) {
        console_printf(sh->con, "Usage: resolve <hostname>\n");
        return;
    }
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(host)) host[j++] = args[i++];
    host[j] = '\0';

    if (host[0] == '\0') {
        console_printf(sh->con, "Usage: resolve <hostname>\n");
        return;
    }

    if (!shell_ensure_net(sh, "resolve")) return;

    if (net_dns_resolve(sh->con, host, ip, 2000)) {
        console_printf(sh->con, "%s -> %u.%u.%u.%u\n",
                       host,
                       (unsigned int)ip[0], (unsigned int)ip[1],
                       (unsigned int)ip[2], (unsigned int)ip[3]);
    } else {
        console_printf(sh->con, "resolve: lookup failed for '%s'\n", host);
    }
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

static void shell_hex_dump(SHELL *sh, const uint8_t *buf, uint32_t bytes) {
    uint32_t i, j;

    for (i = 0; i < bytes; i += 16) {
        console_printf(sh->con, "%x: ", (unsigned int)i);

        for (j = 0; j < 16; j++) {
            if (i + j < bytes) {
                uint8_t b = buf[i + j];
                console_printf(sh->con, "%c%c ",
                    "0123456789ABCDEF"[b >> 4],
                    "0123456789ABCDEF"[b & 0x0F]);
            } else {
                console_printf(sh->con, "   ");
            }
        }

        console_printf(sh->con, " |");

        for (j = 0; j < 16 && (i + j) < bytes; j++) {
            uint8_t c = buf[i + j];
            if (c >= 32 && c <= 126) {
                console_printf(sh->con, "%c", c);
            } else {
                console_printf(sh->con, ".");
            }
        }

        console_printf(sh->con, "|\n");
    }
}

static void shell_run_ahci(SHELL *sh) {
    AHCI_INFO info;

    if (!ahci_probe(&info)) {
        console_printf(sh->con, "ahci: controller not found\n");
        return;
    }

    console_printf(sh->con, "ahci: controller found at %u:%u.%u, initializing...\n",
                   (unsigned int)info.bus,
                   (unsigned int)info.device,
                   (unsigned int)info.function);

    if (!ahci_init(&info)) {
        console_printf(sh->con, "ahci: init failed\n");
        return;
    }

    ahci_print_info(sh->con, &info);
}

static void shell_run_ahciports(SHELL *sh) {
    AHCI_INFO *info = ahci_get_state();

    if (!info->initialized) {
        if (!ahci_init(info)) {
            console_printf(sh->con, "ahciports: init failed or controller not found\n");
            return;
        }
    }

    ahci_print_info(sh->con, info);
    ahci_print_ports(sh->con);
}

static void shell_run_ahciid(SHELL *sh, const char *args) {
    uint32_t port_no;
    AHCI_PORT_INFO pi;

    if (!args || !str_to_u32(args, &port_no)) {
        console_printf(sh->con, "Usage: ahciid <port>\n");
        return;
    }

    if (!ahci_get_state()->initialized) {
        if (!ahci_init(ahci_get_state())) {
            console_printf(sh->con, "ahciid: init failed or controller not found\n");
            return;
        }
    }

    if (!ahci_identify(port_no, &pi)) {
        console_printf(sh->con, "ahciid: identify failed on port %u\n",
                       (unsigned int)port_no);
        return;
    }

    console_printf(sh->con, "port %u:\n", (unsigned int)port_no);
    console_printf(sh->con, "  implemented:    %u\n", (unsigned int)pi.implemented);
    console_printf(sh->con, "  present:        %u\n", (unsigned int)pi.device_present);
    console_printf(sh->con, "  SATA:           %u\n", (unsigned int)pi.sata);
    console_printf(sh->con, "  ATAPI:          %u\n", (unsigned int)pi.atapi);
    console_printf(sh->con, "  signature:      %x\n", (unsigned int)pi.sig);
    console_printf(sh->con, "  ssts:           %x\n", (unsigned int)pi.ssts);
    console_printf(sh->con, "  tfd:            %x\n", (unsigned int)pi.tfd);

    if (pi.model[0]) {
        console_printf(sh->con, "  model:          %s\n", pi.model);
    }
    if (pi.serial[0]) {
        console_printf(sh->con, "  serial:         %s\n", pi.serial);
    }
    if (pi.firmware[0]) {
        console_printf(sh->con, "  firmware:       %s\n", pi.firmware);
    }

    console_printf(sh->con, "  lba28 sectors:  %u\n", (unsigned int)pi.sectors_28);
    console_printf(sh->con, "  lba48 sectors:  %u\n", (unsigned int)(pi.sectors_48 & 0xFFFFFFFFULL));
}

static void shell_run_readlba(SHELL *sh, const char *args) {
    uint32_t port_no;
    uint32_t lba;
    uint32_t count;
    uint8_t buf[AHCI_MAX_READ_SECTORS * 512];

    if (!parse_three_u32_args(args, &port_no, &lba, &count)) {
        console_printf(sh->con, "Usage: readlba <port> <lba> <count>\n");
        console_printf(sh->con, "count must be 1..8\n");
        return;
    }

    if (count == 0 || count > AHCI_MAX_READ_SECTORS) {
        console_printf(sh->con, "readlba: count must be 1..8\n");
        return;
    }

    if (!ahci_get_state()->initialized) {
        if (!ahci_init(ahci_get_state())) {
            console_printf(sh->con, "readlba: init failed or controller not found\n");
            return;
        }
    }

    if (!ahci_read(port_no, (uint64_t)lba, count, buf)) {
        console_printf(sh->con, "readlba: read failed (port=%u lba=%u count=%u)\n",
                       (unsigned int)port_no,
                       (unsigned int)lba,
                       (unsigned int)count);
        return;
    }

    console_printf(sh->con, "readlba: ok (port=%u lba=%u count=%u)\n",
                   (unsigned int)port_no,
                   (unsigned int)lba,
                   (unsigned int)count);

    shell_hex_dump(sh, buf, count * 512U);
}


static int parse_u32_or_name_arg(const char *args, uint32_t *index, char *name_out, uint32_t name_out_size) {
    uint32_t i = 0;
    uint32_t j = 0;

    if (!args) {
        return 0;
    }

    while (args[i] == ' ') {
        i++;
    }

    if (args[i] == '\0') {
        return 0;
    }

    while (args[i] && args[i] != ' ' && j + 1 < name_out_size) {
        name_out[j++] = args[i++];
    }
    name_out[j] = '\0';

    while (args[i] == ' ') {
        i++;
    }

    if (args[i] != '\0') {
        return 0;
    }

    if (str_to_u32(name_out, index)) {
        return 1;
    }

    return 2;
}

static BLOCK_DEVICE *shell_find_block_device(SHELL *sh, const char *arg) {
    uint32_t index = 0;
    char name[BLOCK_NAME_MAX];
    int mode;

    mode = parse_u32_or_name_arg(arg, &index, name, sizeof(name));
    if (mode == 0) {
        console_printf(sh->con, "Usage: blkid <dev>  or  blkread <dev> <lba> <count>\n");
        return 0;
    }

    if (mode == 1) {
        BLOCK_DEVICE *dev = block_get_device(index);
        if (!dev) {
            console_printf(sh->con, "block: device index %u not found\n", (unsigned int)index);
        }
        return dev;
    }

    {
        BLOCK_DEVICE *dev = block_find_by_name(name);
        if (!dev) {
            console_printf(sh->con, "block: device '%s' not found\n", name);
        }
        return dev;
    }
}

static void shell_run_blk(SHELL *sh) {
    block_print_devices(sh->con);
}

static void shell_run_blkid(SHELL *sh, const char *args) {
    BLOCK_DEVICE *dev = shell_find_block_device(sh, args);
    if (!dev) {
        return;
    }

    block_print_device_info(sh->con, dev);
}

static void shell_run_blkread(SHELL *sh, const char *args) {
    char dev_part[32];
    char lba_part[32];
    char count_part[32];
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t lba32 = 0;
    uint32_t count = 0;
    BLOCK_DEVICE *dev;
    uint8_t buf[AHCI_MAX_READ_SECTORS * AHCI_SECTOR_SIZE];

    if (!args) {
        console_printf(sh->con, "Usage: blkread <dev> <lba> <count>\n");
        return;
    }

    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(dev_part)) dev_part[j++] = args[i++];
    dev_part[j] = '\0';

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(lba_part)) lba_part[j++] = args[i++];
    lba_part[j] = '\0';

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(count_part)) count_part[j++] = args[i++];
    count_part[j] = '\0';

    while (args[i] == ' ') i++;
    if (args[i] != '\0') {
        console_printf(sh->con, "Usage: blkread <dev> <lba> <count>\n");
        return;
    }

    dev = shell_find_block_device(sh, dev_part);
    if (!dev) {
        return;
    }

    if (!str_to_u32(lba_part, &lba32) || !str_to_u32(count_part, &count)) {
        console_printf(sh->con, "Usage: blkread <dev> <lba> <count>\n");
        return;
    }

    if (count == 0 || count > AHCI_MAX_READ_SECTORS) {
        console_printf(sh->con, "blkread: count must be 1..8\n");
        return;
    }

    if (dev->sector_size != AHCI_SECTOR_SIZE) {
        console_printf(sh->con, "blkread: only 512-byte sectors are supported right now\n");
        return;
    }

    if (!block_read(dev, (uint64_t)lba32, count, buf)) {
        console_printf(sh->con, "blkread: read failed (dev=%s lba=%u count=%u)\n",
                       dev->name,
                       (unsigned int)lba32,
                       (unsigned int)count);
        return;
    }

    console_printf(sh->con, "blkread: ok (dev=%s lba=%u count=%u)\n",
                   dev->name,
                   (unsigned int)lba32,
                   (unsigned int)count);

    shell_hex_dump(sh, buf, count * AHCI_SECTOR_SIZE);
}

static PARTITION_INFO *shell_find_partition(SHELL *sh, const char *arg) {
    uint32_t index = 0;
    char name[PARTITION_NAME_MAX];
    int mode;

    mode = parse_u32_or_name_arg(arg, &index, name, sizeof(name));
    if (mode == 0) {
        console_printf(sh->con, "Usage: partinfo <part>\n");
        return 0;
    }

    if (mode == 1) {
        PARTITION_INFO *part = partition_get(index);
        if (!part) {
            console_printf(sh->con, "partition: index %u not found\n", (unsigned int)index);
        }
        return part;
    }

    {
        PARTITION_INFO *part = partition_find_by_name(name);
        if (!part) {
            console_printf(sh->con, "partition: '%s' not found\n", name);
        }
        return part;
    }
}

static void shell_run_part(SHELL *sh) {
    partition_print_all(sh->con);
}

static void shell_run_partscan(SHELL *sh) {
    uint32_t count = partition_scan_all();
    console_printf(sh->con, "partscan: found %u partition(s)\n", (unsigned int)count);
    partition_print_all(sh->con);
}

static void shell_run_partinfo(SHELL *sh, const char *args) {
    PARTITION_INFO *part = shell_find_partition(sh, args);
    if (!part) {
        return;
    }

    partition_print_info(sh->con, part);
}

static int shell_parse_part_and_optional_path(SHELL *sh,
                                              const char *args,
                                              PARTITION_INFO **out_part,
                                              char *path_out,
                                              uint32_t path_out_size) {
    uint32_t i = 0;
    uint32_t j = 0;
    char part_arg[PARTITION_NAME_MAX];

    if (!args || !out_part || !path_out || path_out_size == 0) {
        return 0;
    }

    while (args[i] == ' ') {
        i++;
    }

    while (args[i] && args[i] != ' ' && j + 1 < sizeof(part_arg)) {
        part_arg[j++] = args[i++];
    }
    part_arg[j] = '\0';

    if (part_arg[0] == '\0') {
        return 0;
    }

    *out_part = shell_find_partition(sh, part_arg);
    if (!*out_part) {
        return 0;
    }

    while (args[i] == ' ') {
        i++;
    }

    j = 0;
    while (args[i] && j + 1 < path_out_size) {
        path_out[j++] = args[i++];
    }
    path_out[j] = '\0';
    return 1;
}

static int shell_parse_part_and_required_path(SHELL *sh,
                                              const char *args,
                                              PARTITION_INFO **out_part,
                                              char *path_out,
                                              uint32_t path_out_size) {
    if (!shell_parse_part_and_optional_path(sh, args, out_part, path_out, path_out_size)) {
        return 0;
    }

    if (path_out[0] == '\0') {
        return 0;
    }

    return 1;
}

static void shell_run_fatinfo(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    FAT32_FS fs;

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatinfo <part>\n");
        return;
    }

    part = shell_find_partition(sh, args);
    if (!part) {
        return;
    }

    if (!fat32_mount(part, &fs)) {
        console_printf(sh->con, "fatinfo: failed to mount FAT32 on %s\n", part->name);
        return;
    }

    fat32_print_info(sh->con, &fs);
}

static void shell_run_fatls(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatls <part> [path]\n");
        return;
    }

    if (!shell_parse_part_and_optional_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatls <part> [path]\n");
        return;
    }

    if (!fat32_list_directory(sh->con, part, path[0] ? path : "/")) {
        console_printf(sh->con, "fatls: cannot list '%s' on %s\n",
                       path[0] ? path : "/",
                       part->name);
    }
}

static void shell_run_fatcat(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatcat <part> <path>\n");
        return;
    }

    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatcat <part> <path>\n");
        return;
    }

    if (!fat32_cat_file(sh->con, part, path)) {
        console_printf(sh->con, "fatcat: cannot read '%s' on %s\n", path, part->name);
    }
}

static void shell_run_fatwrite(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char part_name[PARTITION_NAME_MAX];
    char path[128];
    const char *text;
    uint32_t i = 0;
    uint32_t j;
    uint32_t text_len;

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatwrite <part> <path> <text...>\n");
        return;
    }

    /* skip leading spaces */
    while (args[i] == ' ') i++;

    /* token 1: <part> */
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(part_name)) {
        part_name[j++] = args[i++];
    }
    part_name[j] = '\0';
    if (j == 0) {
        console_printf(sh->con, "Usage: fatwrite <part> <path> <text...>\n");
        return;
    }

    while (args[i] == ' ') i++;

    /* token 2: <path> (no spaces in FAT32 8.3 paths) */
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(path)) {
        path[j++] = args[i++];
    }
    path[j] = '\0';
    if (j == 0) {
        console_printf(sh->con, "Usage: fatwrite <part> <path> <text...>\n");
        return;
    }

    while (args[i] == ' ') i++;

    /* rest: text payload (kept verbatim, including inner spaces) */
    text = &args[i];
    if (!*text) {
        console_printf(sh->con,
            "fatwrite: empty payload (try 'fatwrite %s %s hello world')\n",
            part_name, path);
        return;
    }

    part = shell_find_partition(sh, part_name);
    if (!part) {
        console_printf(sh->con, "fatwrite: unknown partition '%s'\n", part_name);
        return;
    }

    text_len = 0;
    while (text[text_len]) text_len++;

    if (!fat32_write_file(sh->con, part, path, text, text_len)) {
        console_printf(sh->con, "fatwrite: failed on '%s'\n", path);
    }
}

static void shell_run_fatrename(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char part_name[PARTITION_NAME_MAX];
    char old_path[128];
    char new_path[128];
    uint32_t i = 0;
    uint32_t j;

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatrename <part> <old> <new>\n");
        return;
    }

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(part_name)) part_name[j++] = args[i++];
    part_name[j] = '\0';
    if (j == 0) { console_printf(sh->con, "Usage: fatrename <part> <old> <new>\n"); return; }

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(old_path)) old_path[j++] = args[i++];
    old_path[j] = '\0';
    if (j == 0) { console_printf(sh->con, "Usage: fatrename <part> <old> <new>\n"); return; }

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j + 1 < sizeof(new_path)) new_path[j++] = args[i++];
    new_path[j] = '\0';
    if (j == 0) { console_printf(sh->con, "Usage: fatrename <part> <old> <new>\n"); return; }

    part = shell_find_partition(sh, part_name);
    if (!part) {
        console_printf(sh->con, "fatrename: unknown partition '%s'\n", part_name);
        return;
    }

    if (!fat32_rename(sh->con, part, old_path, new_path)) {
        console_printf(sh->con, "fatrename: failed\n");
    }
}

static void shell_run_fattouch(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fattouch <part> <path>\n");
        return;
    }
    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fattouch <part> <path>\n");
        return;
    }
    if (!fat32_create_file(sh->con, part, path)) {
        console_printf(sh->con, "fattouch: failed on '%s'\n", path);
    }
}

static void shell_run_fatrm(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatrm <part> <path>\n");
        return;
    }
    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatrm <part> <path>\n");
        return;
    }
    if (!fat32_delete_file(sh->con, part, path)) {
        console_printf(sh->con, "fatrm: failed on '%s'\n", path);
    }
}

static void shell_run_fatview(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];
    uint32_t page_lines = 0;

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatview [-N] <part> <path>\n");
        return;
    }

    while (*args == ' ') {
        args++;
    }

    if (*args == '-') {
        uint32_t n = 0;
        args++;

        if (*args < '0' || *args > '9') {
            console_printf(sh->con, "Usage: fatview [-N] <part> <path>\n");
            return;
        }

        while (*args >= '0' && *args <= '9') {
            n = n * 10u + (uint32_t)(*args - '0');
            args++;
        }

        while (*args == ' ') {
            args++;
        }

        if (n < 10) {
            n = 10;
        }
        if (n > 30) {
            n = 30;
        }

        page_lines = n;
    }

    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatview [-N] <part> <path>\n");
        return;
    }

    if (!fat32_view_file(sh->con, part, path, page_lines)) {
        console_printf(sh->con, "fatview: cannot view '%s' on %s\n", path, part->name);
    }
}

static void shell_run_fatdump(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatdump <part> <path>\n");
        return;
    }

    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatdump <part> <path>\n");
        return;
    }

    if (!fat32_dump_file(sh->con, part, path, 256U)) {
        console_printf(sh->con, "fatdump: cannot dump '%s' on %s\n", path, part->name);
    }
}

static void shell_run_fatstat(SHELL *sh, const char *args) {
    PARTITION_INFO *part;
    char path[128];

    if (!args || !*args) {
        console_printf(sh->con, "Usage: fatstat <part> <path>\n");
        return;
    }

    if (!shell_parse_part_and_required_path(sh, args, &part, path, sizeof(path))) {
        console_printf(sh->con, "Usage: fatstat <part> <path>\n");
        return;
    }

    if (!fat32_stat_path(sh->con, part, path)) {
        console_printf(sh->con, "fatstat: cannot stat '%s' on %s\n", path, part->name);
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

    if (str_eq(sh->input, "alloc")) {
        void *p = pmm_alloc_page();
    
        if (!p) {
            console_printf(sh->con, "alloc failed\n");
            return;
        }
    
        console_printf(sh->con, "allocated page: %p\n", p);
    
        uint64_t *x = (uint64_t*)p;
        *x = 0xDEADBEEFCAFEBABEULL;
    
        console_printf(sh->con, "write ok, value=%p\n", (void*)(uintptr_t)(*x));
        return;
    }

    if (str_eq(sh->input, "pf")) {
        console_printf(sh->con, "triggering page fault...\n");
    
        volatile uint64_t *bad = (uint64_t*)0xFFFFFFFFFFFFF000ULL;
        *bad = 0x1234ULL;
    
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

    if (str_starts_with(sh->input, "fatinfo ")) {
        shell_run_fatinfo(sh, sh->input + 8);
        return;
    }

    if (str_starts_with(sh->input, "fatls ")) {
        shell_run_fatls(sh, sh->input + 6);
        return;
    }

    if (str_starts_with(sh->input, "fatstat ")) {
        shell_run_fatstat(sh, sh->input + 8);
        return;
    }
    
    if (str_starts_with(sh->input, "fatview ")) {
        shell_run_fatview(sh, sh->input + 8);
        return;
    }
    
    if (str_starts_with(sh->input, "fatdump ")) {
        shell_run_fatdump(sh, sh->input + 8);
        return;
    }

    if (str_starts_with(sh->input, "fatcat ")) {
        shell_run_fatcat(sh, sh->input + 7);
        return;
    }

    if (str_starts_with(sh->input, "fatwrite ")) {
        shell_run_fatwrite(sh, sh->input + 9);
        return;
    }

    if (str_starts_with(sh->input, "fatrename ")) {
        shell_run_fatrename(sh, sh->input + 10);
        return;
    }

    if (str_starts_with(sh->input, "fattouch ")) {
        shell_run_fattouch(sh, sh->input + 9);
        return;
    }
    if (str_starts_with(sh->input, "fatrm ")) {
        shell_run_fatrm(sh, sh->input + 6);
        return;
    }

    if (str_eq(sh->input, "ahci")) {
        shell_run_ahci(sh);
        return;
    }

    if (str_eq(sh->input, "ahciports")) {
        shell_run_ahciports(sh);
        return;
    }

    if (str_starts_with(sh->input, "ahciid ")) {
        shell_run_ahciid(sh, sh->input + 7);
        return;
    }

    if (str_starts_with(sh->input, "readlba ")) {
        shell_run_readlba(sh, sh->input + 8);
        return;
    }

    if (str_eq(sh->input, "blk")) {
        shell_run_blk(sh);
        return;
    }

    if (str_starts_with(sh->input, "blkid ")) {
        shell_run_blkid(sh, sh->input + 6);
        return;
    }

    if (str_starts_with(sh->input, "blkread ")) {
        shell_run_blkread(sh, sh->input + 8);
        return;
    }

    if (str_eq(sh->input, "part")) {
        shell_run_part(sh);
        return;
    }

    if (str_eq(sh->input, "partscan")) {
        shell_run_partscan(sh);
        return;
    }

    if (str_starts_with(sh->input, "partinfo ")) {
        shell_run_partinfo(sh, sh->input + 9);
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
        shell_reset_input(sh);
        return;
    }

    if (str_eq(sh->input, "net")) {
        shell_run_net(sh);
        shell_reset_input(sh);
        return;
    }

    if (str_eq(sh->input, "ipcfg")) {
        shell_run_ipcfg(sh);
        shell_reset_input(sh);
        return;
    }

    if (str_starts_with(sh->input, "ipset ")) {
        shell_run_ipset(sh, sh->input + 6);
        shell_reset_input(sh);
        return;
    }

    if (str_starts_with(sh->input, "dnsset ")) {
        shell_run_dnsset(sh, sh->input + 7);
        shell_reset_input(sh);
        return;
    }

    if (str_eq(sh->input, "arp")) {
        shell_run_arp(sh);
        shell_reset_input(sh);
        return;
    }

    if (str_eq(sh->input, "arpclear")) {
        shell_run_arpclear(sh);
        shell_reset_input(sh);
        return;
    }

    if (str_starts_with(sh->input, "ping ")) {
        shell_run_ping(sh, sh->input + 5);
        shell_reset_input(sh);
        return;
    }

    if (str_starts_with(sh->input, "resolve ")) {
        shell_run_resolve(sh, sh->input + 8);
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
