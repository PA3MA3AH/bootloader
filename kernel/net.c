#include "net.h"
#include "e1000.h"
#include "console.h"
#include "kheap.h"
#include "pit.h"

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 *  Small freestanding helpers
 * ============================================================ */

static void mem_zero(void *ptr, uint32_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void mem_copy(void *dst, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static int mem_eq(const void *a, const void *b, uint32_t size) {
    const uint8_t *pa = (const uint8_t*)a;
    const uint8_t *pb = (const uint8_t*)b;
    for (uint32_t i = 0; i < size; i++) {
        if (pa[i] != pb[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static uint16_t load_u16_be(const uint8_t *p) {
    return (uint16_t)((((uint16_t)p[0]) << 8) | (uint16_t)p[1]);
}

static void store_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

/* clang in freestanding mode still emits implicit calls to memset/memcpy
 * for things like `T x = {0};` on aggregates. Provide minimal fallbacks. */
void *memset(void *dst, int c, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* RFC 1071 internet checksum, returns network-byte-order value
 * stored in host order (caller writes via store_u16_be). */
static uint16_t inet_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t i;

    for (i = 0; i + 1 < len; i += 2) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1];
    }
    if (i < len) {
        sum += ((uint32_t)data[i]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

/* ============================================================
 *  Time helpers
 * ============================================================ */

static void net_ensure_pit(void) {
    if (pit_hz() == 0) {
        pit_start(100);
    }
}

static uint32_t ticks_to_ms(uint64_t ticks) {
    uint32_t hz = pit_hz();
    if (hz == 0) {
        return 0;
    }
    return (uint32_t)((ticks * 1000ULL) / (uint64_t)hz);
}

static uint64_t ms_to_ticks(uint32_t ms) {
    uint32_t hz = pit_hz();
    if (hz == 0) {
        return 0;
    }
    return ((uint64_t)ms * (uint64_t)hz + 999ULL) / 1000ULL;
}

/* ============================================================
 *  Configuration / state
 * ============================================================ */

static NET_CONFIG g_cfg;
static E1000_INFO *g_nic = 0;
static int g_initialized = 0;

/* Default to QEMU SLIRP layout */
static const uint8_t SLIRP_IP[4]      = { 10, 0, 2, 15 };
static const uint8_t SLIRP_NETMASK[4] = { 255, 255, 255, 0 };
static const uint8_t SLIRP_GATEWAY[4] = { 10, 0, 2, 2 };
static const uint8_t SLIRP_DNS[4]     = { 10, 0, 2, 3 };

static const uint8_t MAC_BROADCAST[6] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t MAC_ZERO[6] = { 0, 0, 0, 0, 0, 0 };

int net_init(E1000_INFO *nic) {
    if (!nic) {
        return 0;
    }

    if (!nic->present) {
        if (!e1000_probe(nic)) {
            return 0;
        }
    }
    if (!nic->rings_ready) {
        if (!e1000_init_rings(nic)) {
            return 0;
        }
    }

    if (!g_initialized) {
        mem_zero(&g_cfg, sizeof(g_cfg));
        mem_copy(g_cfg.ip,      SLIRP_IP,      4);
        mem_copy(g_cfg.netmask, SLIRP_NETMASK, 4);
        mem_copy(g_cfg.gateway, SLIRP_GATEWAY, 4);
        mem_copy(g_cfg.dns,     SLIRP_DNS,     4);
        g_cfg.configured = 1;
    }

    mem_copy(g_cfg.mac, nic->mac, 6);
    g_nic = nic;
    g_initialized = 1;

    net_ensure_pit();
    return 1;
}

int net_is_ready(void) {
    return g_initialized && g_nic != 0;
}

NET_CONFIG *net_get_config(void) {
    return &g_cfg;
}

void net_set_ip(const uint8_t ip[4], const uint8_t netmask[4], const uint8_t gw[4]) {
    if (ip)      mem_copy(g_cfg.ip, ip, 4);
    if (netmask) mem_copy(g_cfg.netmask, netmask, 4);
    if (gw)      mem_copy(g_cfg.gateway, gw, 4);
    g_cfg.configured = 1;
}

void net_set_dns(const uint8_t dns[4]) {
    if (dns) {
        mem_copy(g_cfg.dns, dns, 4);
    }
}

void net_print_config(CONSOLE *con) {
    if (!con) {
        return;
    }
    if (!g_initialized) {
        console_printf(con, "net: not initialized (run `e1000` first)\n");
        return;
    }

    console_printf(con, "net config:\n");
    console_printf(con, "  mac:     %x:%x:%x:%x:%x:%x\n",
                   (unsigned int)g_cfg.mac[0], (unsigned int)g_cfg.mac[1],
                   (unsigned int)g_cfg.mac[2], (unsigned int)g_cfg.mac[3],
                   (unsigned int)g_cfg.mac[4], (unsigned int)g_cfg.mac[5]);
    console_printf(con, "  ip:      %u.%u.%u.%u\n",
                   (unsigned int)g_cfg.ip[0], (unsigned int)g_cfg.ip[1],
                   (unsigned int)g_cfg.ip[2], (unsigned int)g_cfg.ip[3]);
    console_printf(con, "  netmask: %u.%u.%u.%u\n",
                   (unsigned int)g_cfg.netmask[0], (unsigned int)g_cfg.netmask[1],
                   (unsigned int)g_cfg.netmask[2], (unsigned int)g_cfg.netmask[3]);
    console_printf(con, "  gateway: %u.%u.%u.%u\n",
                   (unsigned int)g_cfg.gateway[0], (unsigned int)g_cfg.gateway[1],
                   (unsigned int)g_cfg.gateway[2], (unsigned int)g_cfg.gateway[3]);
    console_printf(con, "  dns:     %u.%u.%u.%u\n",
                   (unsigned int)g_cfg.dns[0], (unsigned int)g_cfg.dns[1],
                   (unsigned int)g_cfg.dns[2], (unsigned int)g_cfg.dns[3]);
}

int net_parse_ipv4(const char *s, uint8_t out[4]) {
    uint32_t v[4] = {0, 0, 0, 0};
    uint32_t idx = 0;
    uint32_t cur = 0;
    int has_digit = 0;

    if (!s || !out) {
        return 0;
    }

    for (uint32_t i = 0; ; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            cur = cur * 10U + (uint32_t)(c - '0');
            if (cur > 255) {
                return 0;
            }
            has_digit = 1;
        } else if (c == '.' || c == 0) {
            if (!has_digit) {
                return 0;
            }
            if (idx >= 4) {
                return 0;
            }
            v[idx++] = cur;
            cur = 0;
            has_digit = 0;
            if (c == 0) {
                break;
            }
        } else {
            return 0;
        }
    }

    if (idx != 4) {
        return 0;
    }

    out[0] = (uint8_t)v[0];
    out[1] = (uint8_t)v[1];
    out[2] = (uint8_t)v[2];
    out[3] = (uint8_t)v[3];
    return 1;
}

/* ============================================================
 *  ARP cache
 * ============================================================ */

#define ARP_CACHE_SIZE 8

typedef struct {
    int     valid;
    uint8_t ip[4];
    uint8_t mac[6];
} ARP_ENTRY;

static ARP_ENTRY g_arp[ARP_CACHE_SIZE];

static void arp_insert(const uint8_t ip[4], const uint8_t mac[6]) {
    /* Update if already there */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid && mem_eq(g_arp[i].ip, ip, 4)) {
            mem_copy(g_arp[i].mac, mac, 6);
            return;
        }
    }
    /* Use first empty slot, otherwise overwrite slot 0 (FIFO-ish) */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].valid) {
            g_arp[i].valid = 1;
            mem_copy(g_arp[i].ip, ip, 4);
            mem_copy(g_arp[i].mac, mac, 6);
            return;
        }
    }
    g_arp[0].valid = 1;
    mem_copy(g_arp[0].ip, ip, 4);
    mem_copy(g_arp[0].mac, mac, 6);
}

int net_arp_lookup(const uint8_t ip[4], uint8_t out_mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid && mem_eq(g_arp[i].ip, ip, 4)) {
            if (out_mac) {
                mem_copy(out_mac, g_arp[i].mac, 6);
            }
            return 1;
        }
    }
    return 0;
}

void net_arp_clear(void) {
    mem_zero(g_arp, sizeof(g_arp));
}

void net_arp_print(CONSOLE *con) {
    if (!con) {
        return;
    }
    int any = 0;
    console_printf(con, "ARP cache:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].valid) {
            continue;
        }
        any = 1;
        console_printf(con,
                       "  %u.%u.%u.%u -> %x:%x:%x:%x:%x:%x\n",
                       (unsigned int)g_arp[i].ip[0],
                       (unsigned int)g_arp[i].ip[1],
                       (unsigned int)g_arp[i].ip[2],
                       (unsigned int)g_arp[i].ip[3],
                       (unsigned int)g_arp[i].mac[0],
                       (unsigned int)g_arp[i].mac[1],
                       (unsigned int)g_arp[i].mac[2],
                       (unsigned int)g_arp[i].mac[3],
                       (unsigned int)g_arp[i].mac[4],
                       (unsigned int)g_arp[i].mac[5]);
    }
    if (!any) {
        console_printf(con, "  (empty)\n");
    }
}

/* ============================================================
 *  Frame builders / dispatch
 * ============================================================ */

#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

#define IPPROTO_ICMP   1
#define IPPROTO_UDP    17

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

/* Build an Ethernet header into frame[0..13]. */
static void build_eth_header(uint8_t *frame,
                             const uint8_t dst_mac[6],
                             const uint8_t src_mac[6],
                             uint16_t ethertype) {
    mem_copy(frame + 0, dst_mac, 6);
    mem_copy(frame + 6, src_mac, 6);
    store_u16_be(frame + 12, ethertype);
}

/* Build IPv4 header into ip[0..19] (no options).
 * total_length = full IP datagram length (header + payload).
 * Computes the header checksum. */
static void build_ipv4_header(uint8_t *ip,
                              uint8_t protocol,
                              uint16_t total_length,
                              uint16_t ident,
                              const uint8_t src_ip[4],
                              const uint8_t dst_ip[4]) {
    ip[0]  = (4U << 4) | 5U;            /* version 4, IHL 5 */
    ip[1]  = 0;                          /* DSCP/ECN */
    store_u16_be(ip + 2, total_length);
    store_u16_be(ip + 4, ident);
    store_u16_be(ip + 6, 0);             /* flags + fragment offset */
    ip[8]  = 64;                         /* TTL */
    ip[9]  = protocol;
    store_u16_be(ip + 10, 0);            /* checksum placeholder */
    mem_copy(ip + 12, src_ip, 4);
    mem_copy(ip + 16, dst_ip, 4);
    uint16_t cs = inet_checksum(ip, 20);
    store_u16_be(ip + 10, cs);
}

/* Pick destination MAC for an outgoing IPv4 packet using the routing rule:
 * if ip is on our subnet, ARP for it directly; otherwise ARP for gateway. */
static int resolve_next_hop(const uint8_t ip[4],
                            uint8_t out_mac[6],
                            uint32_t timeout_ms) {
    uint8_t target[4];
    int on_link = 1;
    for (int i = 0; i < 4; i++) {
        if ((g_cfg.ip[i] & g_cfg.netmask[i]) != (ip[i] & g_cfg.netmask[i])) {
            on_link = 0;
            break;
        }
    }
    if (on_link) {
        mem_copy(target, ip, 4);
    } else {
        mem_copy(target, g_cfg.gateway, 4);
    }
    return net_arp_resolve(target, out_mac, timeout_ms);
}

/* ============================================================
 *  Outgoing ARP
 * ============================================================ */

static int send_arp_request(const uint8_t target_ip[4]) {
    uint8_t frame[60];
    if (!g_nic) return 0;

    mem_zero(frame, sizeof(frame));
    build_eth_header(frame, MAC_BROADCAST, g_cfg.mac, ETHERTYPE_ARP);

    /* ARP body @ frame+14 */
    store_u16_be(frame + 14, 0x0001);        /* HTYPE Ethernet */
    store_u16_be(frame + 16, ETHERTYPE_IPV4);/* PTYPE IPv4 */
    frame[18] = 6;                            /* HLEN */
    frame[19] = 4;                            /* PLEN */
    store_u16_be(frame + 20, 0x0001);        /* OPER request */
    mem_copy(frame + 22, g_cfg.mac, 6);
    mem_copy(frame + 28, g_cfg.ip, 4);
    mem_copy(frame + 32, MAC_ZERO, 6);
    mem_copy(frame + 38, target_ip, 4);

    return e1000_send_packet(g_nic, frame, (uint16_t)sizeof(frame));
}

static int send_arp_reply(const uint8_t target_ip[4],
                          const uint8_t target_mac[6]) {
    uint8_t frame[60];
    if (!g_nic) return 0;

    mem_zero(frame, sizeof(frame));
    build_eth_header(frame, target_mac, g_cfg.mac, ETHERTYPE_ARP);

    store_u16_be(frame + 14, 0x0001);
    store_u16_be(frame + 16, ETHERTYPE_IPV4);
    frame[18] = 6;
    frame[19] = 4;
    store_u16_be(frame + 20, 0x0002);        /* OPER reply */
    mem_copy(frame + 22, g_cfg.mac, 6);
    mem_copy(frame + 28, g_cfg.ip, 4);
    mem_copy(frame + 32, target_mac, 6);
    mem_copy(frame + 38, target_ip, 4);

    return e1000_send_packet(g_nic, frame, (uint16_t)sizeof(frame));
}

/* ============================================================
 *  Incoming dispatch state — simple "expectations" tracked while
 *  blocking calls (ping / dns) are spinning.
 * ============================================================ */

typedef struct {
    int      active;
    uint16_t ident;
    uint16_t seq;
    int      received;
    uint64_t reply_ticks;
} PING_WAITER;

typedef struct {
    int      active;
    uint16_t txid;
    int      received;
    int      ok;
    uint8_t  result_ip[4];
} DNS_WAITER;

static PING_WAITER g_ping_waiter;
static DNS_WAITER  g_dns_waiter;

/* ---- ARP handler ---- */
static void handle_arp_frame(const uint8_t *frame, uint32_t len) {
    if (len < 42) return;
    const uint8_t *arp = frame + 14;

    /* Basic sanity */
    if (load_u16_be(arp + 0) != 0x0001) return;        /* HTYPE Eth */
    if (load_u16_be(arp + 2) != ETHERTYPE_IPV4) return;
    if (arp[4] != 6 || arp[5] != 4) return;

    uint16_t op = load_u16_be(arp + 6);
    const uint8_t *sha = arp + 8;
    const uint8_t *spa = arp + 14;
    const uint8_t *tpa = arp + 24;

    /* Always learn from the sender */
    arp_insert(spa, sha);

    if (op == 0x0001 && mem_eq(tpa, g_cfg.ip, 4)) {
        /* ARP request for us — reply */
        send_arp_reply(spa, sha);
    }
    /* op == 0x0002 (reply): nothing to do, already cached */
}

/* ---- ICMP handler (operates on IP payload) ---- */
static void handle_icmp(const uint8_t *frame,
                        uint32_t frame_len,
                        const uint8_t *ip_hdr,
                        uint32_t ip_total_len,
                        uint32_t ihl_bytes) {
    (void)frame; (void)frame_len;

    if (ip_total_len < ihl_bytes + 8) return;
    const uint8_t *icmp = ip_hdr + ihl_bytes;
    uint32_t icmp_len = ip_total_len - ihl_bytes;

    uint8_t  type = icmp[0];
    uint16_t ident = load_u16_be(icmp + 4);
    uint16_t seq   = load_u16_be(icmp + 6);

    if (type == ICMP_ECHO_REPLY) {
        if (g_ping_waiter.active &&
            !g_ping_waiter.received &&
            g_ping_waiter.ident == ident &&
            g_ping_waiter.seq == seq) {
            g_ping_waiter.reply_ticks = pit_ticks();
            g_ping_waiter.received = 1;
        }
    } else if (type == ICMP_ECHO_REQUEST) {
        /* Build an Echo Reply mirroring payload back to the sender */
        uint8_t reply[1500];
        if (icmp_len > sizeof(reply) - 14 - ihl_bytes) {
            return;
        }

        const uint8_t *src_mac = frame + 6;
        const uint8_t *src_ip  = ip_hdr + 12;

        mem_zero(reply, sizeof(reply));
        build_eth_header(reply, src_mac, g_cfg.mac, ETHERTYPE_IPV4);

        /* IP header */
        uint16_t total = (uint16_t)(20 + icmp_len);
        build_ipv4_header(reply + 14, IPPROTO_ICMP, total, 0x4242, g_cfg.ip, src_ip);

        /* ICMP: copy then change type to 0 and recompute checksum */
        uint8_t *out_icmp = reply + 14 + 20;
        mem_copy(out_icmp, icmp, icmp_len);
        out_icmp[0] = ICMP_ECHO_REPLY;
        store_u16_be(out_icmp + 2, 0);
        uint16_t cs = inet_checksum(out_icmp, icmp_len);
        store_u16_be(out_icmp + 2, cs);

        uint16_t total_frame = (uint16_t)(14 + total);
        if (total_frame < 60) total_frame = 60;
        e1000_send_packet(g_nic, reply, total_frame);
    }
}

/* ---- DNS response parser (only A records) ---- */

/* Read a DNS name starting at offset `off`, following pointers.
 * Stores the dot-form into out (truncated to out_size).
 * Returns the number of bytes consumed at offset `off` (NOT including
 * bytes traversed via pointer), or 0 on failure. */
static uint32_t dns_read_name(const uint8_t *buf, uint32_t buf_len,
                              uint32_t off, char *out, uint32_t out_size) {
    uint32_t cur = off;
    uint32_t out_pos = 0;
    int jumped = 0;
    uint32_t consumed_at_start = 0;
    int label_started = 0;
    uint32_t safety = 0;

    while (safety++ < 256) {
        if (cur >= buf_len) return 0;
        uint8_t b = buf[cur];

        if (b == 0) {
            if (!jumped) consumed_at_start += 1;
            cur += 1;
            break;
        }
        if ((b & 0xC0) == 0xC0) {
            if (cur + 1 >= buf_len) return 0;
            uint16_t ptr = (uint16_t)(((b & 0x3FU) << 8) | buf[cur + 1]);
            if (!jumped) consumed_at_start += 2;
            cur = ptr;
            jumped = 1;
            continue;
        }
        if ((b & 0xC0) != 0) return 0;

        uint8_t llen = b;
        if (cur + 1 + llen > buf_len) return 0;
        if (label_started && out && out_pos + 1 < out_size) {
            out[out_pos++] = '.';
        }
        for (uint32_t i = 0; i < llen; i++) {
            if (out && out_pos + 1 < out_size) {
                out[out_pos++] = (char)buf[cur + 1 + i];
            }
        }
        label_started = 1;
        if (!jumped) consumed_at_start += 1U + (uint32_t)llen;
        cur += 1U + (uint32_t)llen;
    }

    if (out && out_pos < out_size) {
        out[out_pos] = '\0';
    } else if (out && out_size > 0) {
        out[out_size - 1] = '\0';
    }
    return consumed_at_start;
}

static void handle_dns(const uint8_t *udp_payload, uint32_t udp_len) {
    if (!g_dns_waiter.active || g_dns_waiter.received) return;
    if (udp_len < 12) return;

    uint16_t txid = load_u16_be(udp_payload + 0);
    if (txid != g_dns_waiter.txid) return;

    uint16_t flags = load_u16_be(udp_payload + 2);
    uint16_t qdcount = load_u16_be(udp_payload + 4);
    uint16_t ancount = load_u16_be(udp_payload + 6);

    g_dns_waiter.received = 1;
    g_dns_waiter.ok = 0;

    if ((flags & 0x000FU) != 0) {
        /* RCODE != 0: server returned an error */
        return;
    }

    uint32_t off = 12;
    /* Skip questions */
    for (uint16_t i = 0; i < qdcount; i++) {
        uint32_t consumed = dns_read_name(udp_payload, udp_len, off, 0, 0);
        if (consumed == 0) return;
        off += consumed;
        if (off + 4 > udp_len) return;
        off += 4; /* QTYPE + QCLASS */
    }

    /* Walk answers; pick first A record */
    for (uint16_t i = 0; i < ancount; i++) {
        uint32_t consumed = dns_read_name(udp_payload, udp_len, off, 0, 0);
        if (consumed == 0) return;
        off += consumed;
        if (off + 10 > udp_len) return;

        uint16_t atype  = load_u16_be(udp_payload + off + 0);
        /* uint16_t aclass = load_u16_be(udp_payload + off + 2); */
        /* uint32_t ttl   = load_u32_be(udp_payload + off + 4); */
        uint16_t rdlen  = load_u16_be(udp_payload + off + 8);
        off += 10;
        if (off + rdlen > udp_len) return;

        if (atype == 1 && rdlen == 4) {
            mem_copy(g_dns_waiter.result_ip, udp_payload + off, 4);
            g_dns_waiter.ok = 1;
            return;
        }
        off += rdlen;
    }
}

static void handle_udp(const uint8_t *ip_hdr,
                       uint32_t ip_total_len,
                       uint32_t ihl_bytes) {
    if (ip_total_len < ihl_bytes + 8) return;
    const uint8_t *udp = ip_hdr + ihl_bytes;
    uint16_t src_port = load_u16_be(udp + 0);
    /* uint16_t dst_port = load_u16_be(udp + 2); */
    uint16_t udp_len  = load_u16_be(udp + 4);
    if (udp_len < 8 || udp_len > ip_total_len - ihl_bytes) return;

    const uint8_t *payload = udp + 8;
    uint32_t payload_len = (uint32_t)(udp_len - 8);

    if (src_port == 53) {
        handle_dns(payload, payload_len);
    }
}

static void handle_ipv4_frame(const uint8_t *frame, uint32_t len) {
    if (len < 14 + 20) return;
    const uint8_t *ip = frame + 14;

    uint8_t version_ihl = ip[0];
    if ((version_ihl >> 4) != 4) return;
    uint32_t ihl_bytes = (uint32_t)(version_ihl & 0x0F) * 4U;
    if (ihl_bytes < 20) return;

    uint16_t total_len = load_u16_be(ip + 2);
    if (total_len < ihl_bytes) return;
    if ((uint32_t)total_len + 14U > len) return;

    /* Only accept frames addressed to us or to broadcast */
    int for_us = mem_eq(ip + 16, g_cfg.ip, 4);
    int bcast  = (ip[16] == 255 && ip[17] == 255 && ip[18] == 255 && ip[19] == 255);
    if (!for_us && !bcast) {
        /* Still learn route info if it’s broadcast traffic, but skip processing */
        return;
    }

    uint8_t proto = ip[9];
    if (proto == IPPROTO_ICMP) {
        handle_icmp(frame, len, ip, total_len, ihl_bytes);
    } else if (proto == IPPROTO_UDP) {
        handle_udp(ip, total_len, ihl_bytes);
    }
}

/* ============================================================
 *  Receive pump
 * ============================================================ */

void net_poll(void) {
    if (!g_nic) return;

    uint8_t buf[2048];
    uint32_t pkt_len = 0;

    while (e1000_recv_packet(g_nic, buf, sizeof(buf), &pkt_len)) {
        if (pkt_len < 14) continue;
        uint16_t ethertype = load_u16_be(buf + 12);
        if (ethertype == ETHERTYPE_ARP) {
            handle_arp_frame(buf, pkt_len);
        } else if (ethertype == ETHERTYPE_IPV4) {
            handle_ipv4_frame(buf, pkt_len);
        }
    }
}

/* ============================================================
 *  ARP resolve (blocking)
 * ============================================================ */

int net_arp_resolve(const uint8_t ip[4], uint8_t out_mac[6], uint32_t timeout_ms) {
    if (!g_initialized || !ip) return 0;

    if (net_arp_lookup(ip, out_mac)) {
        return 1;
    }

    net_ensure_pit();
    uint64_t deadline = pit_ticks() + ms_to_ticks(timeout_ms);
    uint64_t next_send = 0;
    int attempts = 0;

    while (pit_ticks() < deadline) {
        if (pit_ticks() >= next_send && attempts < 4) {
            send_arp_request(ip);
            attempts++;
            next_send = pit_ticks() + ms_to_ticks(timeout_ms / 4U + 1U);
        }
        net_poll();
        if (net_arp_lookup(ip, out_mac)) {
            return 1;
        }
        __asm__ __volatile__("pause");
    }
    return 0;
}

/* ============================================================
 *  ICMP echo (ping)
 * ============================================================ */

static int send_icmp_echo(const uint8_t dst_ip[4],
                          const uint8_t dst_mac[6],
                          uint16_t ident,
                          uint16_t seq) {
    uint8_t frame[14 + 20 + 8 + 32];
    mem_zero(frame, sizeof(frame));

    build_eth_header(frame, dst_mac, g_cfg.mac, ETHERTYPE_IPV4);

    uint16_t total = (uint16_t)(20 + 8 + 32);
    build_ipv4_header(frame + 14, IPPROTO_ICMP, total, ident, g_cfg.ip, dst_ip);

    uint8_t *icmp = frame + 14 + 20;
    icmp[0] = ICMP_ECHO_REQUEST;
    icmp[1] = 0;
    store_u16_be(icmp + 2, 0);
    store_u16_be(icmp + 4, ident);
    store_u16_be(icmp + 6, seq);
    /* 32 bytes of pattern */
    for (uint32_t i = 0; i < 32; i++) {
        icmp[8 + i] = (uint8_t)('a' + (i % 26));
    }
    uint16_t cs = inet_checksum(icmp, 8 + 32);
    store_u16_be(icmp + 2, cs);

    return e1000_send_packet(g_nic, frame, (uint16_t)sizeof(frame));
}

int net_ping(CONSOLE *con,
             const uint8_t ip[4],
             uint32_t count,
             uint32_t timeout_ms,
             NET_PING_STATS *out_stats) {
    NET_PING_STATS stats;
    mem_zero(&stats, sizeof(stats));
    stats.rtt_min_ms = 0xFFFFFFFFU;

    if (!g_initialized) {
        if (con) console_printf(con, "ping: net not initialized (run `e1000`)\n");
        return 0;
    }
    if (count == 0) count = 4;
    if (timeout_ms == 0) timeout_ms = 1000;

    uint8_t hop_mac[6];
    if (!resolve_next_hop(ip, hop_mac, timeout_ms)) {
        if (con) console_printf(con, "ping: ARP failed for next hop\n");
        return 0;
    }

    if (con) {
        console_printf(con, "PING %u.%u.%u.%u: %u packets, %u ms timeout\n",
                       (unsigned int)ip[0], (unsigned int)ip[1],
                       (unsigned int)ip[2], (unsigned int)ip[3],
                       (unsigned int)count, (unsigned int)timeout_ms);
    }

    uint16_t ident = (uint16_t)(pit_ticks() & 0xFFFFU) | 0x4000U;

    for (uint32_t i = 0; i < count; i++) {
        uint16_t seq = (uint16_t)(i + 1);

        mem_zero(&g_ping_waiter, sizeof(g_ping_waiter));
        g_ping_waiter.active = 1;
        g_ping_waiter.ident = ident;
        g_ping_waiter.seq = seq;

        uint64_t t_send = pit_ticks();
        if (!send_icmp_echo(ip, hop_mac, ident, seq)) {
            if (con) console_printf(con, "  seq=%u: send failed\n",
                                    (unsigned int)seq);
            stats.sent++;
            continue;
        }
        stats.sent++;

        uint64_t deadline = t_send + ms_to_ticks(timeout_ms);
        while (pit_ticks() < deadline && !g_ping_waiter.received) {
            net_poll();
            __asm__ __volatile__("pause");
        }

        if (g_ping_waiter.received) {
            uint64_t dt = g_ping_waiter.reply_ticks - t_send;
            uint32_t ms = ticks_to_ms(dt);
            stats.received++;
            stats.rtt_total_ms += ms;
            if (ms < stats.rtt_min_ms) stats.rtt_min_ms = ms;
            if (ms > stats.rtt_max_ms) stats.rtt_max_ms = ms;
            if (con) {
                console_printf(con,
                               "  reply seq=%u from %u.%u.%u.%u rtt=%u ms\n",
                               (unsigned int)seq,
                               (unsigned int)ip[0], (unsigned int)ip[1],
                               (unsigned int)ip[2], (unsigned int)ip[3],
                               (unsigned int)ms);
            }
        } else if (con) {
            console_printf(con, "  seq=%u: timeout\n", (unsigned int)seq);
        }

        g_ping_waiter.active = 0;
    }

    if (stats.received == 0) {
        stats.rtt_min_ms = 0;
    }

    if (con) {
        uint32_t loss_pct = stats.sent
            ? ((stats.sent - stats.received) * 100U) / stats.sent
            : 0;
        console_printf(con,
                       "--- ping statistics ---\n"
                       "  %u sent, %u received, %u%% loss\n",
                       (unsigned int)stats.sent,
                       (unsigned int)stats.received,
                       (unsigned int)loss_pct);
        if (stats.received > 0) {
            console_printf(con,
                           "  rtt min/avg/max = %u/%u/%u ms\n",
                           (unsigned int)stats.rtt_min_ms,
                           (unsigned int)(stats.rtt_total_ms / stats.received),
                           (unsigned int)stats.rtt_max_ms);
        }
    }

    if (out_stats) *out_stats = stats;
    return stats.received > 0;
}

/* ============================================================
 *  DNS query (A record)
 * ============================================================ */

/* Encode a DNS QNAME into out, returns number of bytes written or 0. */
static uint32_t dns_encode_qname(const char *name, uint8_t *out, uint32_t out_size) {
    uint32_t pos = 0;
    uint32_t i = 0;
    uint32_t name_len = str_len(name);

    while (i <= name_len) {
        /* Find end of label */
        uint32_t label_start = i;
        while (i < name_len && name[i] != '.') i++;
        uint32_t llen = i - label_start;
        if (llen == 0 && i < name_len) return 0; /* empty label like "a..b" */
        if (llen > 63) return 0;
        if (pos + 1 + llen >= out_size) return 0;
        out[pos++] = (uint8_t)llen;
        for (uint32_t j = 0; j < llen; j++) {
            out[pos++] = (uint8_t)name[label_start + j];
        }
        if (i >= name_len) break;
        i++; /* skip the '.' */
    }
    if (pos + 1 > out_size) return 0;
    out[pos++] = 0; /* terminator */
    return pos;
}

static int send_dns_query(const uint8_t dst_mac[6],
                          const uint8_t dst_ip[4],
                          uint16_t txid,
                          uint16_t src_port,
                          const char *name) {
    uint8_t frame[14 + 20 + 8 + 512];
    mem_zero(frame, sizeof(frame));
    build_eth_header(frame, dst_mac, g_cfg.mac, ETHERTYPE_IPV4);

    uint8_t *udp_payload = frame + 14 + 20 + 8;
    uint32_t avail = (uint32_t)sizeof(frame) - (14 + 20 + 8);

    /* DNS header */
    if (avail < 12) return 0;
    store_u16_be(udp_payload + 0, txid);
    store_u16_be(udp_payload + 2, 0x0100);  /* RD=1 */
    store_u16_be(udp_payload + 4, 1);       /* QDCOUNT */
    store_u16_be(udp_payload + 6, 0);       /* ANCOUNT */
    store_u16_be(udp_payload + 8, 0);       /* NSCOUNT */
    store_u16_be(udp_payload + 10, 0);      /* ARCOUNT */

    uint32_t qname_len = dns_encode_qname(name, udp_payload + 12, avail - 12);
    if (qname_len == 0) return 0;
    if (12 + qname_len + 4 > avail) return 0;

    store_u16_be(udp_payload + 12 + qname_len + 0, 1); /* QTYPE A */
    store_u16_be(udp_payload + 12 + qname_len + 2, 1); /* QCLASS IN */

    uint32_t dns_len = 12 + qname_len + 4;
    uint16_t udp_len = (uint16_t)(8 + dns_len);
    uint16_t ip_total = (uint16_t)(20 + udp_len);

    build_ipv4_header(frame + 14, IPPROTO_UDP, ip_total, 0xBEEF, g_cfg.ip, dst_ip);

    uint8_t *udp = frame + 14 + 20;
    store_u16_be(udp + 0, src_port);
    store_u16_be(udp + 2, 53);
    store_u16_be(udp + 4, udp_len);
    store_u16_be(udp + 6, 0);  /* checksum optional for IPv4 UDP — leave 0 */

    uint16_t total_frame = (uint16_t)(14 + ip_total);
    if (total_frame < 60) total_frame = 60;
    return e1000_send_packet(g_nic, frame, total_frame);
}

int net_dns_resolve(CONSOLE *con,
                    const char *name,
                    uint8_t out_ip[4],
                    uint32_t timeout_ms) {
    if (!g_initialized) {
        if (con) console_printf(con, "dns: net not initialized\n");
        return 0;
    }
    if (!name || !*name) return 0;
    if (timeout_ms == 0) timeout_ms = 2000;

    /* Already an IP literal? */
    if (net_parse_ipv4(name, out_ip)) {
        return 1;
    }

    uint8_t hop_mac[6];
    if (!resolve_next_hop(g_cfg.dns, hop_mac, timeout_ms)) {
        if (con) console_printf(con, "dns: ARP for DNS server failed\n");
        return 0;
    }

    uint16_t txid = (uint16_t)((pit_ticks() & 0xFFFFU) ^ 0x55AA);
    uint16_t src_port = (uint16_t)(40000U + (pit_ticks() & 0x1FFFU));

    mem_zero(&g_dns_waiter, sizeof(g_dns_waiter));
    g_dns_waiter.active = 1;
    g_dns_waiter.txid = txid;

    uint64_t t_send = pit_ticks();
    if (!send_dns_query(hop_mac, g_cfg.dns, txid, src_port, name)) {
        if (con) console_printf(con, "dns: send failed\n");
        g_dns_waiter.active = 0;
        return 0;
    }

    uint64_t deadline = t_send + ms_to_ticks(timeout_ms);
    uint64_t next_resend = t_send + ms_to_ticks(timeout_ms / 2U + 1U);
    int resends = 0;

    while (pit_ticks() < deadline && !g_dns_waiter.received) {
        net_poll();
        if (pit_ticks() >= next_resend && resends < 1) {
            send_dns_query(hop_mac, g_cfg.dns, txid, src_port, name);
            resends++;
            next_resend = deadline;
        }
        __asm__ __volatile__("pause");
    }

    int ok = g_dns_waiter.received && g_dns_waiter.ok;
    if (ok) {
        mem_copy(out_ip, g_dns_waiter.result_ip, 4);
    }
    g_dns_waiter.active = 0;
    return ok;
}
