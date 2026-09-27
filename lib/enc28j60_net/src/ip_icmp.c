// ip_icmp.c — implementasi IPv4 + ICMP echo reply.

#include <string.h>
#include "ip_icmp.h"
#include "enc28j60_hw.h"
#include "eth_arp.h"
#include "net_state.h"
#include "net_offsets.h"
#include "net_config.h"

void ip_icmp_init(void) {
    // Tidak ada state yang perlu di-reset di v1.
}

void net_fill_checksum(uint16_t dest_offset, uint16_t start, uint16_t len,
                       uint16_t pseudo_sum) {
    uint32_t sum = pseudo_sum;
    const uint8_t *ptr = net_buf + start;
    while (len > 1) {
        sum += ((uint16_t) ptr[0] << 8) | ptr[1];
        ptr += 2;
        len -= 2;
    }
    if (len) // byte ganjil terakhir, dianggap high byte dengan low byte 0
        sum += ((uint16_t) ptr[0]) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t result = (uint16_t) ~sum;
    net_buf[dest_offset]     = (uint8_t) (result >> 8);
    net_buf[dest_offset + 1] = (uint8_t) result;
}

void ip_prepare_send(const uint8_t dst_ip[4], uint8_t protocol, uint16_t total_len) {
    net_buf[IP_P]             = 0x45; // versi 4, IHL 5 (20 byte, tanpa opsi)
    net_buf[IP_P + 1]         = 0x00; // TOS
    net_buf[IP_TOTLEN_H_P]    = (uint8_t) (total_len >> 8);
    net_buf[IP_TOTLEN_L_P]    = (uint8_t) total_len;
    net_buf[IP_P + 4]         = 0x00; // identification (fragmentasi gak dipakai)
    net_buf[IP_P + 5]         = 0x00;
    net_buf[IP_FLAGS_P]       = 0x40; // don't-fragment
    net_buf[IP_FLAGS_P + 1]   = 0x00;
    net_buf[IP_TTL_P]         = 64;
    net_buf[IP_PROTO_P]       = protocol;
    net_buf[IP_CHECKSUM_P]     = 0;
    net_buf[IP_CHECKSUM_P + 1] = 0;
    memcpy(net_buf + IP_SRC_P, net_my_ip, 4);
    memcpy(net_buf + IP_DST_P, dst_ip, 4);
}

uint8_t ip_send_finalize(uint16_t total_len) {
    net_fill_checksum(IP_CHECKSUM_P, IP_P, IP_HEADER_LEN, 0);

    uint8_t dst_ip[4];
    memcpy(dst_ip, net_buf + IP_DST_P, 4);

    uint8_t dst_mac[6];
    uint8_t is_bcast = (dst_ip[0] == 255 && dst_ip[1] == 255 &&
                         dst_ip[2] == 255 && dst_ip[3] == 255) ||
                        memcmp(dst_ip, net_broadcast_ip, 4) == 0 ||
                        (dst_ip[0] & 0xF0) == 0xE0; // multicast, didekati sbg broadcast
    if (is_bcast) {
        memset(dst_mac, 0xFF, 6);
    } else if (!eth_resolve_mac(dst_ip, dst_mac)) {
        return 0; // MAC belum ada, paket ini dibuang - caller retry nanti
    }

    eth_build_header(dst_mac, (uint16_t) ((ETH_TYPE_IP_H << 8) | ETH_TYPE_IP_L));
    enc_packet_send(ETH_HEADER_LEN + total_len);
    return 1;
}

#if NET_ENABLE_ICMP
static void icmp_send_echo_reply(uint16_t ip_total_len) {
    // Balas ke pengirim: swap MAC dan IP src/dst LANGSUNG dari paket yang
    // baru diterima (gak perlu ARP lagi, kita baru saja dengar dari dia).
    uint8_t requester_mac[6];
    uint8_t requester_ip[4];
    memcpy(requester_mac, net_buf + ETH_SRC_MAC_P, 6);
    memcpy(requester_ip, net_buf + IP_SRC_P, 4);

    uint16_t icmp_len = ip_total_len - IP_HEADER_LEN;

    net_buf[ICMP_TYPE_P] = ICMP_TYPE_ECHOREPLY;
    // code (net_buf[ICMP_CODE_P]) tetap 0, sama seperti request.
    net_buf[ICMP_CHECKSUM_P] = 0;
    net_buf[ICMP_CHECKSUM_P + 1] = 0;

    ip_prepare_send(requester_ip, IP_PROTO_ICMP, ip_total_len);
    net_fill_checksum(ICMP_CHECKSUM_P, ICMP_TYPE_P, icmp_len, 0);

    eth_build_header(requester_mac, (uint16_t) ((ETH_TYPE_IP_H << 8) | ETH_TYPE_IP_L));
    net_fill_checksum(IP_CHECKSUM_P, IP_P, IP_HEADER_LEN, 0);
    enc_packet_send(ETH_HEADER_LEN + ip_total_len);
}
#endif

uint8_t ip_input(uint16_t len) {
    if (len < ETH_HEADER_LEN + IP_HEADER_LEN)
        return 0;
    if ((net_buf[IP_P] & 0xF0) != 0x40)
        return 0; // bukan IPv4
    if ((net_buf[IP_P] & 0x0F) != 5)
        return 0; // ada IP options - tidak didukung v1, paket dibuang

    uint16_t ip_total_len = ((uint16_t) net_buf[IP_TOTLEN_H_P] << 8) |
                            net_buf[IP_TOTLEN_L_P];
    uint8_t protocol = net_buf[IP_PROTO_P];

#if NET_ENABLE_ICMP
    if (protocol == IP_PROTO_ICMP) {
        if (len >= ETH_HEADER_LEN + IP_HEADER_LEN + 8 &&
            net_buf[ICMP_TYPE_P] == ICMP_TYPE_ECHOREQUEST) {
            icmp_send_echo_reply(ip_total_len);
        }
        return 0;
    }
#endif

    if (protocol == IP_PROTO_UDP || protocol == IP_PROTO_TCP) {
        // Ajarin cache ARP dari MAC pengirim yang BARU KITA TERIMA, sebelum
        // udp.c/tcp.c sempat balas. Tanpa ini, balasan pertama (UDP reply /
        // TCP SYN-ACK) ke host yang belum pernah "dikenal" bakal gagal
        // sekali (nunggu 1 round-trip ARP dulu) - ketahuan pas testing:
        // percobaan TCP connect pertama sering "Could not connect" karena
        // client keburu timeout duluan sebelum ARP kita selesai, padahal
        // firmware sebenarnya cuma butuh sedikit lebih lama buat retry.
        eth_arp_learn(net_buf + IP_SRC_P, net_buf + ETH_SRC_MAC_P);
        return protocol; // udp.c / tcp.c yang lanjut proses payload
    }

    return 0; // protokol IP lain, tidak kita layani
}