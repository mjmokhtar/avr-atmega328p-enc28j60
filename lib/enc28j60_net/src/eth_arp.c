// eth_arp.c — implementasi frame Ethernet + ARP (2-slot cache: gateway
// + 1 host tujuan aktif). Desain 2-slot ini diambil dari pola EtherCard
// (bukan cache ARP penuh), sesuai keterbatasan RAM yang sudah disepakati
// di PRD.

#include <string.h>
#include "eth_arp.h"
#include "enc28j60_hw.h"
#include "net_state.h"
#include "net_offsets.h"
#include "millis.h"

static const uint8_t BCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define ARP_RETRY_MS  1000UL

// --- cache MAC gateway ---
static uint8_t  s_gw_mac[6];
static uint8_t  s_gw_mac_valid = 0;
static uint8_t  s_gw_pending = 0;
static uint32_t s_gw_last_request_ms;

// --- cache MAC 1 host tujuan aktif (bukan gateway) ---
static uint8_t  s_dest_ip[4];
static uint8_t  s_dest_mac[6];
static uint8_t  s_dest_mac_valid = 0;
static uint8_t  s_dest_pending = 0;
static uint32_t s_dest_last_request_ms;

static void arp_send_request(const uint8_t target_ip[4]) {
    memset(net_buf + ARP_P, 0, ARP_PACKET_LEN);
    net_buf[ARP_P + 0] = 0x00; net_buf[ARP_P + 1] = 0x01; // hw type = Ethernet
    net_buf[ARP_P + 2] = 0x08; net_buf[ARP_P + 3] = 0x00; // proto type = IPv4
    net_buf[ARP_P + 4] = 6;                               // hw addr len
    net_buf[ARP_P + 5] = 4;                               // proto addr len
    net_buf[ARP_OPCODE_H_P] = 0x00;
    net_buf[ARP_OPCODE_L_P] = ARP_OP_REQUEST;
    memcpy(net_buf + ARP_SRC_MAC_P, net_my_mac, 6);
    memcpy(net_buf + ARP_SRC_IP_P, net_my_ip, 4);
    memset(net_buf + ARP_DST_MAC_P, 0, 6); // target MAC belum diketahui
    memcpy(net_buf + ARP_DST_IP_P, target_ip, 4);

    eth_build_header(BCAST_MAC, (uint16_t) ((ETH_TYPE_ARP_H << 8) | ETH_TYPE_ARP_L));
    enc_packet_send(ETH_HEADER_LEN + ARP_PACKET_LEN);
}

static void arp_send_reply(void) {
    // Balikkan: pengirim request jadi tujuan reply.
    uint8_t requester_mac[6];
    uint8_t requester_ip[4];
    memcpy(requester_mac, net_buf + ARP_SRC_MAC_P, 6);
    memcpy(requester_ip, net_buf + ARP_SRC_IP_P, 4);

    net_buf[ARP_OPCODE_H_P] = 0x00;
    net_buf[ARP_OPCODE_L_P] = ARP_OP_REPLY;
    memcpy(net_buf + ARP_DST_MAC_P, requester_mac, 6);
    memcpy(net_buf + ARP_DST_IP_P, requester_ip, 4);
    memcpy(net_buf + ARP_SRC_MAC_P, net_my_mac, 6);
    memcpy(net_buf + ARP_SRC_IP_P, net_my_ip, 4);

    eth_build_header(requester_mac, (uint16_t) ((ETH_TYPE_ARP_H << 8) | ETH_TYPE_ARP_L));
    enc_packet_send(ETH_HEADER_LEN + ARP_PACKET_LEN);
}

void eth_arp_init(void) {
    s_gw_mac_valid = 0;
    s_gw_pending = 0;
    s_dest_mac_valid = 0;
    s_dest_pending = 0;
}

void arp_poll(void) {
    uint32_t now = millis_now();
    if (s_gw_pending && !s_gw_mac_valid &&
        (now - s_gw_last_request_ms) >= ARP_RETRY_MS) {
        s_gw_last_request_ms = now;
        arp_send_request(net_gw_ip);
    }
    if (s_dest_pending && !s_dest_mac_valid &&
        (now - s_dest_last_request_ms) >= ARP_RETRY_MS) {
        s_dest_last_request_ms = now;
        arp_send_request(s_dest_ip);
    }
}

uint8_t eth_resolve_mac(const uint8_t dst_ip[4], uint8_t out_mac[6]) {
    if (net_is_same_subnet(dst_ip)) {
        if (s_dest_mac_valid && memcmp(s_dest_ip, dst_ip, 4) == 0) {
            memcpy(out_mac, s_dest_mac, 6);
            return 1;
        }
        // Target beda dari yang lagi di-cache -> ganti slot, mulai resolve baru.
        if (!s_dest_pending || memcmp(s_dest_ip, dst_ip, 4) != 0) {
            memcpy(s_dest_ip, dst_ip, 4);
            s_dest_mac_valid = 0;
            s_dest_pending = 1;
            s_dest_last_request_ms = millis_now();
            arp_send_request(dst_ip);
        }
        return 0;
    } else {
        if (s_gw_mac_valid) {
            memcpy(out_mac, s_gw_mac, 6);
            return 1;
        }
        if (!s_gw_pending) {
            s_gw_pending = 1;
            s_gw_last_request_ms = millis_now();
            arp_send_request(net_gw_ip);
        }
        return 0;
    }
}

void eth_arp_learn(const uint8_t ip[4], const uint8_t mac[6]) {
    // Isi langsung slot cache tujuan, TANPA kirim ARP request - kita baru
    // saja terima frame ini, jadi MAC-nya sudah pasti benar. Kalau ip ini
    // di LUAR subnet kita, eth_resolve_mac() gak akan pernah cek slot ini
    // buat dst_ip itu (dia lewat cabang gateway), jadi aman-aman saja -
    // cuma jadi cache "nganggur" yang nanti ke-replace kalau ada host lain.
    memcpy(s_dest_ip, ip, 4);
    memcpy(s_dest_mac, mac, 6);
    s_dest_mac_valid = 1;
    s_dest_pending = 0;
}

void eth_build_header(const uint8_t dst_mac[6], uint16_t ethertype) {
    memcpy(net_buf + ETH_DST_MAC_P, dst_mac, 6);
    memcpy(net_buf + ETH_SRC_MAC_P, net_my_mac, 6);
    net_buf[ETH_TYPE_H_P] = (uint8_t) (ethertype >> 8);
    net_buf[ETH_TYPE_L_P] = (uint8_t) ethertype;
}

uint8_t eth_input(uint16_t len) {
    if (len < ETH_HEADER_LEN + 2)
        return 0;

    uint8_t type_h = net_buf[ETH_TYPE_H_P];
    uint8_t type_l = net_buf[ETH_TYPE_L_P];

    if (type_h == ETH_TYPE_ARP_H && type_l == ETH_TYPE_ARP_L) {
        if (len < ETH_HEADER_LEN + ARP_PACKET_LEN)
            return 0;
        if (memcmp(net_buf + ARP_DST_IP_P, net_my_ip, 4) != 0)
            return 0; // ARP ini bukan soal IP kita

        uint8_t opcode_l = net_buf[ARP_OPCODE_L_P];
        if (opcode_l == ARP_OP_REQUEST) {
            arp_send_reply();
        } else if (opcode_l == ARP_OP_REPLY) {
            uint8_t src_ip[4];
            memcpy(src_ip, net_buf + ARP_SRC_IP_P, 4);
            if (s_gw_pending && memcmp(src_ip, net_gw_ip, 4) == 0) {
                memcpy(s_gw_mac, net_buf + ARP_SRC_MAC_P, 6);
                s_gw_mac_valid = 1;
                s_gw_pending = 0;
            } else if (s_dest_pending && memcmp(src_ip, s_dest_ip, 4) == 0) {
                memcpy(s_dest_mac, net_buf + ARP_SRC_MAC_P, 6);
                s_dest_mac_valid = 1;
                s_dest_pending = 0;
            }
        }
        return 0;
    }

    if (type_h == ETH_TYPE_IP_H && type_l == ETH_TYPE_IP_L) {
        if (len < ETH_HEADER_LEN + IP_HEADER_LEN)
            return 0;
        // Terima kalau: unicast ke IP kita, ATAU broadcast subnet, ATAU
        // broadcast global 255.255.255.255 (dipakai DHCP sebelum punya IP).
        if (memcmp(net_buf + IP_DST_P, net_my_ip, 4) == 0 ||
            memcmp(net_buf + IP_DST_P, net_broadcast_ip, 4) == 0 ||
            (net_buf[IP_DST_P] == 255 && net_buf[IP_DST_P + 1] == 255 &&
             net_buf[IP_DST_P + 2] == 255 && net_buf[IP_DST_P + 3] == 255)) {
            return 1; // caller (ip_icmp layer) lanjut proses
        }
        return 0;
    }

    return 0; // ethertype lain (IPv6, dst) - gak kita layani
}