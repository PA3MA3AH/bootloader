#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "e1000.h"

/* ---- Configuration ---- */

typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    int     configured;
} NET_CONFIG;

/* Bind the network stack to a probed/initialized e1000 NIC.
 * Loads MAC from the NIC, applies SLIRP defaults (10.0.2.15/24,
 * gw 10.0.2.2, dns 10.0.2.3) on first call. Safe to call multiple times. */
int  net_init(E1000_INFO *nic);
int  net_is_ready(void);
NET_CONFIG *net_get_config(void);

void net_set_ip(const uint8_t ip[4], const uint8_t netmask[4], const uint8_t gw[4]);
void net_set_dns(const uint8_t dns[4]);
void net_print_config(CONSOLE *con);

/* ---- Frame pump (polled). Drains the e1000 RX ring and dispatches. ---- */
void net_poll(void);

/* ---- ARP ---- */
int  net_arp_lookup(const uint8_t ip[4], uint8_t out_mac[6]);
int  net_arp_resolve(const uint8_t ip[4], uint8_t out_mac[6], uint32_t timeout_ms);
void net_arp_print(CONSOLE *con);
void net_arp_clear(void);

/* ---- ICMP echo (ping) ---- */
typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t rtt_min_ms;
    uint32_t rtt_max_ms;
    uint32_t rtt_total_ms;
} NET_PING_STATS;

int net_ping(CONSOLE *con,
             const uint8_t ip[4],
             uint32_t count,
             uint32_t timeout_ms,
             NET_PING_STATS *out_stats);

/* ---- DNS (A record only) ---- */
int net_dns_resolve(CONSOLE *con,
                    const char *name,
                    uint8_t out_ip[4],
                    uint32_t timeout_ms);

/* ---- Helpers exposed for the shell ---- */
int net_parse_ipv4(const char *s, uint8_t out[4]);

#endif
