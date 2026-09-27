// net_offsets.h — offset byte tetap ke dalam net_buf[], per layer.
//
// Teknik ini (satu buffer dibaca/ditulis lewat offset tetap, bukan
// struct per layer) diambil dari pola desain EtherCard's net.h — tapi
// nilai offset di bawah bukan "milik" library manapun: ini posisi
// standar Ethernet II / ARP / IPv4 / ICMP / UDP / TCP dari RFC & IEEE
// 802.3, sama di semua implementasi manapun di dunia.
//
// Kenapa pakai offset langsung bukan struct: menghindari padding
// compiler dan copy antar-layer — penting di RAM 2KB ATmega328.

#ifndef NET_OFFSETS_H
#define NET_OFFSETS_H

// ===================== ETHERNET II (14 byte header) =====================
#define ETH_DST_MAC_P   0   // 6 byte
#define ETH_SRC_MAC_P   6   // 6 byte
#define ETH_TYPE_H_P    12
#define ETH_TYPE_L_P    13
#define ETH_HEADER_LEN  14

#define ETH_TYPE_ARP_H  0x08
#define ETH_TYPE_ARP_L  0x06
#define ETH_TYPE_IP_H   0x08
#define ETH_TYPE_IP_L   0x00

// ===================== ARP (28 byte, mulai offset 14) ===================
#define ARP_P             14
#define ARP_OPCODE_H_P    20
#define ARP_OPCODE_L_P    21
#define ARP_SRC_MAC_P     22   // 6 byte
#define ARP_SRC_IP_P      28   // 4 byte
#define ARP_DST_MAC_P     32   // 6 byte
#define ARP_DST_IP_P      38   // 4 byte
#define ARP_PACKET_LEN    28

#define ARP_OP_REQUEST    1
#define ARP_OP_REPLY      2

// ===================== IPv4 (20 byte, mulai offset 14) ===================
#define IP_P              14   // versi+IHL, awal header
#define IP_TOTLEN_H_P     16
#define IP_TOTLEN_L_P     17
#define IP_FLAGS_P        20   // + fragment offset (2 byte)
#define IP_TTL_P          22
#define IP_PROTO_P        23
#define IP_CHECKSUM_P     24
#define IP_SRC_P          26   // 4 byte
#define IP_DST_P          30   // 4 byte
#define IP_HEADER_LEN     20

#define IP_PROTO_ICMP     1
#define IP_PROTO_TCP      6
#define IP_PROTO_UDP      17

// ===================== ICMP (mulai offset 34 = 14+20) ====================
#define ICMP_TYPE_P       34
#define ICMP_CODE_P       35
#define ICMP_CHECKSUM_P   36
#define ICMP_IDENT_H_P    38
#define ICMP_SEQ_H_P      40
#define ICMP_DATA_P       42

#define ICMP_TYPE_ECHOREQUEST  8
#define ICMP_TYPE_ECHOREPLY    0

// ============== UDP (mulai offset 34, sama posisi dgn ICMP/TCP) ==========
#define UDP_SRC_PORT_H_P  34
#define UDP_SRC_PORT_L_P  35
#define UDP_DST_PORT_H_P  36
#define UDP_DST_PORT_L_P  37
#define UDP_LEN_H_P       38
#define UDP_LEN_L_P       39
#define UDP_CHECKSUM_H_P  40
#define UDP_CHECKSUM_L_P  41
#define UDP_DATA_P        42
#define UDP_HEADER_LEN    8

// ===================== TCP (mulai offset 34) ==============================
#define TCP_SRC_PORT_H_P  34
#define TCP_SRC_PORT_L_P  35
#define TCP_DST_PORT_H_P  36
#define TCP_DST_PORT_L_P  37
#define TCP_SEQ_H_P       38   // 4 byte
#define TCP_ACK_H_P       42   // 4 byte
#define TCP_HEADER_LEN_P  46   // data-offset (nibble atas) + reserved
#define TCP_FLAGS_P       47
#define TCP_WIN_SIZE_P    48
#define TCP_CHECKSUM_H_P  50
#define TCP_URGENT_P      52
#define TCP_OPTIONS_P     54
#define TCP_HEADER_LEN_PLAIN  20

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PUSH 0x08
#define TCP_FLAG_ACK  0x10

#endif // NET_OFFSETS_H