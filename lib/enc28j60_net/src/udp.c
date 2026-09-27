// udp.c — implementasi UDP.

#include <string.h>
#include "udp.h"
#include "enc28j60_hw.h"
#include "ip_icmp.h"
#include "net_offsets.h"
#include "net_config.h"

typedef struct {
    uint16_t port;
    udp_callback_t callback;
    uint8_t active;
} udp_listener_t;

static udp_listener_t s_listeners[NET_MAX_UDP_LISTENERS];

void udp_init(void) {
    memset(s_listeners, 0, sizeof s_listeners);
}

uint8_t udp_listen(uint16_t port, udp_callback_t callback) {
    for (uint8_t i = 0; i < NET_MAX_UDP_LISTENERS; i++) {
        if (!s_listeners[i].active) {
            s_listeners[i].port = port;
            s_listeners[i].callback = callback;
            s_listeners[i].active = 1;
            return 1;
        }
    }
    return 0; // slot penuh - naikkan NET_MAX_UDP_LISTENERS di net_config.h
}

// Bagian umum: header UDP + checksum + serah ke IP, dengan asumsi payload
// SUDAH ada di net_buf+UDP_DATA_P sepanjang 'len' byte (caller yang isi).
static uint8_t udp_finalize(uint16_t src_port, const uint8_t dst_ip[4],
                            uint16_t dst_port, uint16_t len) {
    net_buf[UDP_SRC_PORT_H_P] = (uint8_t) (src_port >> 8);
    net_buf[UDP_SRC_PORT_L_P] = (uint8_t) src_port;
    net_buf[UDP_DST_PORT_H_P] = (uint8_t) (dst_port >> 8);
    net_buf[UDP_DST_PORT_L_P] = (uint8_t) dst_port;
    uint16_t udp_len = (uint16_t) (UDP_HEADER_LEN + len);
    net_buf[UDP_LEN_H_P] = (uint8_t) (udp_len >> 8);
    net_buf[UDP_LEN_L_P] = (uint8_t) udp_len;
    net_buf[UDP_CHECKSUM_H_P] = 0;
    net_buf[UDP_CHECKSUM_L_P] = 0;

    // ip_prepare_send nulis src/dst IP dulu - checksum UDP butuh itu buat
    // pseudo-header (lihat komentar net_fill_checksum di ip_icmp.h).
    ip_prepare_send(dst_ip, IP_PROTO_UDP, (uint16_t) (IP_HEADER_LEN + udp_len));

    // pseudo_sum = protocol + panjang segmen UDP (trik aljabar RFC768,
    // sama dengan yang dipakai net_fill_checksum untuk TCP nanti).
    net_fill_checksum(UDP_CHECKSUM_H_P, IP_SRC_P, (uint16_t) (8 + udp_len),
                      (uint16_t) (IP_PROTO_UDP + udp_len));

    return ip_send_finalize((uint16_t) (IP_HEADER_LEN + udp_len));
}

uint8_t udp_send(uint16_t src_port, const uint8_t dst_ip[4], uint16_t dst_port,
                 const uint8_t *data, uint16_t len) {
    uint16_t max_payload = NET_BUF_SIZE - UDP_DATA_P;
    if (len > max_payload)
        len = max_payload;
    if (len > 0)
        memcpy(net_buf + UDP_DATA_P, data, len);
    return udp_finalize(src_port, dst_ip, dst_port, len);
}

uint8_t udp_send_inplace(uint16_t src_port, const uint8_t dst_ip[4],
                         uint16_t dst_port, uint16_t len) {
    // Payload dianggap sudah ditulis caller langsung ke net_buf+UDP_DATA_P
    // (dipakai DHCP/NTP, biar gak dobel-copy dan gak butuh buffer terpisah).
    uint16_t max_payload = NET_BUF_SIZE - UDP_DATA_P;
    if (len > max_payload)
        len = max_payload;
    return udp_finalize(src_port, dst_ip, dst_port, len);
}

void udp_input(uint16_t len) {
    if (len < ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN)
        return;

    uint16_t dst_port = ((uint16_t) net_buf[UDP_DST_PORT_H_P] << 8) |
                        net_buf[UDP_DST_PORT_L_P];
    uint16_t src_port = ((uint16_t) net_buf[UDP_SRC_PORT_H_P] << 8) |
                        net_buf[UDP_SRC_PORT_L_P];
    uint16_t udp_len = ((uint16_t) net_buf[UDP_LEN_H_P] << 8) |
                       net_buf[UDP_LEN_L_P];
    if (udp_len < UDP_HEADER_LEN)
        return;

    // udp_len itu field DI DALAM paket UDP sendiri, independen dari
    // ip_total_len yang sudah dicek ip_input() - gak otomatis konsisten
    // (bisa beda kalau paketnya korup/sengaja dipalsukan). Pastikan
    // dulu byte yang diklaim udp_len itu BENERAN ada di dalam frame yang
    // sudah kita terima (len), sebelum dipakai buat batas baca checksum
    // ATAU diteruskan ke listener sebagai data_len.
    if ((uint32_t) ETH_HEADER_LEN + IP_HEADER_LEN + udp_len > len)
        return;

    // Checksum UDP itu OPSIONAL (RFC 768): pengirim yang sengaja kirim
    // 0x0000 artinya "gak dihitung", BUKAN "checksum-nya kosong makanya
    // invalid" - kita wajib terima itu apa adanya kalau memang begitu.
    uint8_t checksum_present = !(net_buf[UDP_CHECKSUM_H_P] == 0 &&
                                 net_buf[UDP_CHECKSUM_L_P] == 0);
    if (checksum_present &&
        !net_verify_checksum(IP_SRC_P, (uint16_t) (8 + udp_len),
                             (uint16_t) (IP_PROTO_UDP + udp_len))) {
        return; // checksum salah - buang diam-diam (UDP gak ada balasan error)
    }

    uint16_t data_len = udp_len - UDP_HEADER_LEN;

    for (uint8_t i = 0; i < NET_MAX_UDP_LISTENERS; i++) {
        if (s_listeners[i].active && s_listeners[i].port == dst_port) {
            uint8_t src_ip[4];
            memcpy(src_ip, net_buf + IP_SRC_P, 4);
            s_listeners[i].callback(src_ip, src_port, dst_port,
                                    net_buf + UDP_DATA_P, data_len);
        }
    }
}