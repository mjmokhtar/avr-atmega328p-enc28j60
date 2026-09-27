// dhcp.c — implementasi DHCP client.
//
// Simplifikasi yang sengaja diambil (dicatat, bukan disembunyikan):
// - Renewal (state RENEWING) tetap kirim broadcast ke 255.255.255.255,
//   bukan unicast ke server seperti disarankan RFC 2131. Praktiknya
//   hampir semua router/DHCP server rumahan/kantor tetap merespon
//   broadcast request saat renewal, jadi ini aman untuk v1.
// - Tidak kirim option 12 (hostname) - banyak server DHCP gak butuh itu.
// - xid pakai millis_now() sebagai sumber "acak" - cukup buat bedain
//   transaksi kita dari device lain di jaringan yang sama, bukan buat
//   keamanan kriptografis (DHCP memang gak didesain aman secara itu).

#include <string.h>
#include "dhcp.h"
#include "udp.h"
#include "enc28j60_hw.h"
#include "net_state.h"
#include "net_offsets.h"
#include "millis.h"

#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68
#define DHCP_TIMEOUT_MS   10000UL

// --- layout paket DHCP, offset relatif ke awal payload UDP (net_buf+UDP_DATA_P) ---
#define DHCP_OP_P       0
#define DHCP_HTYPE_P    1
#define DHCP_HLEN_P     2
#define DHCP_XID_P      4    // 4 byte
#define DHCP_YIADDR_P   16   // 4 byte ("your" IP, dari server)
#define DHCP_CHADDR_P   28   // 16 byte (cukup pakai 6 byte pertama = MAC)
#define DHCP_FIXED_LEN  236  // panjang bagian tetap sebelum opsi

#define DHCP_MSG_DISCOVER  1
#define DHCP_MSG_OFFER     2
#define DHCP_MSG_REQUEST   3
#define DHCP_MSG_ACK       5
#define DHCP_MSG_NAK       6

#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_RENEWAL_TIME   58
#define DHCP_OPT_END            255

static dhcp_state_t s_state = DHCP_STATE_INIT;
static uint32_t s_xid;
static uint32_t s_state_timer;
static uint32_t s_lease_start;
static uint32_t s_lease_time_ms = 0xFFFFFFFFUL; // default: dianggap "selamanya" sampai server bilang lain
static uint8_t  s_dhcp_server_ip[4];

static const uint8_t BCAST_IP[4] = {255, 255, 255, 255};

static void dhcp_send(uint8_t msg_type, const uint8_t *requested_ip) {
    uint8_t *base = net_buf + UDP_DATA_P;
    memset(base, 0, DHCP_FIXED_LEN);
    base[DHCP_OP_P] = 1;    // BOOTREQUEST
    base[DHCP_HTYPE_P] = 1; // Ethernet 10Mb
    base[DHCP_HLEN_P] = 6;  // panjang MAC
    base[DHCP_XID_P + 0] = (uint8_t) (s_xid >> 24);
    base[DHCP_XID_P + 1] = (uint8_t) (s_xid >> 16);
    base[DHCP_XID_P + 2] = (uint8_t) (s_xid >> 8);
    base[DHCP_XID_P + 3] = (uint8_t) s_xid;
    // Set bit "broadcast" (0x8000) di flags field (offset 10-11, BOOTP) -
    // paksa server SELALU balas OFFER/ACK ke 255.255.255.255, bukan
    // unicast ke IP yang ditawarkan. Ini WAJIB: kita belum punya IP sama
    // sekali di titik ini, jadi kalau server unicast (banyak router
    // rumahan begitu, lewat MAC dari CHADDR), paketnya sampai secara
    // fisik tapi DIBUANG oleh filter eth_input() kita sendiri (IP tujuan
    // gak cocok net_my_ip yang masih 0.0.0.0) - inilah kenapa DHCP bisa
    // "gantung" gak pernah dapat IP walau link fisik sudah up.
    base[10] = 0x80;
    base[11] = 0x00;
    if (s_state == DHCP_STATE_BOUND || s_state == DHCP_STATE_RENEWING)
        memcpy(base + 12 /* ciaddr */, net_my_ip, 4);
    memcpy(base + DHCP_CHADDR_P, net_my_mac, 6);

    uint8_t *p = base + DHCP_FIXED_LEN;
    *p++ = 0x63; *p++ = 0x82; *p++ = 0x53; *p++ = 0x63; // magic cookie DHCP

    *p++ = DHCP_OPT_MSG_TYPE; *p++ = 1; *p++ = msg_type;

    *p++ = 61; *p++ = 7; *p++ = 1; // client identifier: htype=1 + MAC
    memcpy(p, net_my_mac, 6); p += 6;

    if (requested_ip != NULL) {
        *p++ = 50; *p++ = 4;
        memcpy(p, requested_ip, 4); p += 4;
        *p++ = DHCP_OPT_SERVER_ID; *p++ = 4;
        memcpy(p, s_dhcp_server_ip, 4); p += 4;
    }

    *p++ = 55; *p++ = 3; // parameter request list
    *p++ = DHCP_OPT_SUBNET_MASK;
    *p++ = DHCP_OPT_ROUTER;
    *p++ = 6; // domain name server (gak kita pakai sekarang, tapi umum diminta)

    *p++ = DHCP_OPT_END;

    uint16_t total_len = (uint16_t) (p - base);
    udp_send_inplace(DHCP_CLIENT_PORT, BCAST_IP, DHCP_SERVER_PORT, total_len);
}

// Cari 1 opsi tertentu di dalam blok opsi DHCP. Return pointer ke data
// opsi (bukan ke byte kode/panjangnya) dan isi *out_len, atau NULL kalau
// gak ketemu / data korup.
static const uint8_t *dhcp_find_option(const uint8_t *data, uint16_t len,
                                      uint8_t want_option, uint8_t *out_len) {
    if (len <= DHCP_FIXED_LEN + 4)
        return NULL;
    const uint8_t *p = data + DHCP_FIXED_LEN + 4; // lewati magic cookie
    const uint8_t *end = data + len;
    while (p < end) {
        uint8_t opt = *p++;
        if (opt == DHCP_OPT_END || opt == 0)
            break;
        if (p >= end) break;
        uint8_t opt_len = *p++;
        if (p + opt_len > end) break;
        if (opt == want_option) {
            *out_len = opt_len;
            return p;
        }
        p += opt_len;
    }
    return NULL;
}

static void dhcp_on_packet(const uint8_t src_ip[4], uint16_t src_port,
                           uint16_t dst_port, const uint8_t *data, uint16_t len) {
    (void) dst_port;
    if (src_port != DHCP_SERVER_PORT || len < DHCP_FIXED_LEN + 4)
        return;

    uint32_t recv_xid = ((uint32_t) data[DHCP_XID_P] << 24) |
                        ((uint32_t) data[DHCP_XID_P + 1] << 16) |
                        ((uint32_t) data[DHCP_XID_P + 2] << 8) |
                        data[DHCP_XID_P + 3];
    if (recv_xid != s_xid)
        return; // bukan balasan buat transaksi kita

    uint8_t opt_len;
    const uint8_t *msg_type_ptr = dhcp_find_option(data, len, DHCP_OPT_MSG_TYPE, &opt_len);
    if (!msg_type_ptr)
        return;
    uint8_t msg_type = *msg_type_ptr;

    if (s_state == DHCP_STATE_SELECTING && msg_type == DHCP_MSG_OFFER) {
        uint8_t offered_ip[4];
        memcpy(offered_ip, data + DHCP_YIADDR_P, 4);

        const uint8_t *server_id = dhcp_find_option(data, len, DHCP_OPT_SERVER_ID, &opt_len);
        if (server_id && opt_len == 4)
            memcpy(s_dhcp_server_ip, server_id, 4);

        dhcp_send(DHCP_MSG_REQUEST, offered_ip);
        s_state = DHCP_STATE_REQUESTING;
        s_state_timer = millis_now();
        return;
    }

    if ((s_state == DHCP_STATE_REQUESTING || s_state == DHCP_STATE_RENEWING)) {
        if (msg_type == DHCP_MSG_ACK) {
            memcpy(net_my_ip, data + DHCP_YIADDR_P, 4);

            const uint8_t *opt;
            opt = dhcp_find_option(data, len, DHCP_OPT_SUBNET_MASK, &opt_len);
            if (opt && opt_len == 4) memcpy(net_netmask, opt, 4);

            opt = dhcp_find_option(data, len, DHCP_OPT_ROUTER, &opt_len);
            if (opt && opt_len == 4) memcpy(net_gw_ip, opt, 4);

            // Opsi 6 bisa berisi lebih dari 1 alamat DNS (opt_len kelipatan
            // 4) - kita cuma ambil yang pertama, cukup buat kebutuhan device
            // ini (gak ada resolver DNS di v1, cuma disimpan buat info/log).
            opt = dhcp_find_option(data, len, DHCP_OPT_DNS, &opt_len);
            if (opt && opt_len >= 4) memcpy(net_dns_ip, opt, 4);

            opt = dhcp_find_option(data, len, DHCP_OPT_LEASE_TIME, &opt_len);
            if (!opt) opt = dhcp_find_option(data, len, DHCP_OPT_RENEWAL_TIME, &opt_len);
            if (opt && opt_len == 4) {
                uint32_t lease_s = ((uint32_t) opt[0] << 24) | ((uint32_t) opt[1] << 16) |
                                   ((uint32_t) opt[2] << 8) | opt[3];
                s_lease_time_ms = (lease_s == 0xFFFFFFFFUL) ? 0xFFFFFFFFUL : lease_s * 1000UL;
            }

            net_state_update_broadcast();
            s_lease_start = millis_now();
            s_state = DHCP_STATE_BOUND;
        } else if (msg_type == DHCP_MSG_NAK) {
            s_state = DHCP_STATE_INIT; // server nolak, mulai ulang dari nol
        }
    }
}

void dhcp_start(void) {
    s_state = DHCP_STATE_INIT;
    memset(net_my_ip, 0, 4);
    udp_listen(DHCP_CLIENT_PORT, dhcp_on_packet);
}

void dhcp_poll(void) {
    uint32_t now = millis_now();

    switch (s_state) {
    case DHCP_STATE_INIT:
        s_xid = now; // "acak" secukupnya - lihat komentar di atas file
        memset(net_my_ip, 0, 4);
        dhcp_send(DHCP_MSG_DISCOVER, NULL);
        s_state = DHCP_STATE_SELECTING;
        s_state_timer = now;
        break;

    case DHCP_STATE_SELECTING:
    case DHCP_STATE_REQUESTING:
        if ((now - s_state_timer) > DHCP_TIMEOUT_MS)
            s_state = DHCP_STATE_INIT; // timeout - ulang dari DISCOVER
        break;

    case DHCP_STATE_BOUND:
        if (s_lease_time_ms != 0xFFFFFFFFUL &&
            (now - s_lease_start) >= s_lease_time_ms) {
            dhcp_send(DHCP_MSG_REQUEST, net_my_ip);
            s_state = DHCP_STATE_RENEWING;
            s_state_timer = now;
        }
        break;

    case DHCP_STATE_RENEWING:
        if ((now - s_state_timer) > DHCP_TIMEOUT_MS)
            s_state = DHCP_STATE_INIT; // renewal gagal - ambil IP baru dari nol
        break;
    }
}

dhcp_state_t dhcp_get_state(void) {
    return s_state;
}

void dhcp_force_renew(void) {
    if (s_state != DHCP_STATE_BOUND)
        return; // biarkan transaksi yang sedang berjalan selesai dulu

    // xid baru - ini transaksi baru dari sudut pandang server, walau
    // masih minta IP lama (mirip persis jalur normal BOUND->RENEWING di
    // dhcp_poll(), cuma dipicu link-recovery, bukan lease timer habis).
    s_xid = millis_now();
    dhcp_send(DHCP_MSG_REQUEST, net_my_ip);
    s_state = DHCP_STATE_RENEWING;
    s_state_timer = millis_now();
}