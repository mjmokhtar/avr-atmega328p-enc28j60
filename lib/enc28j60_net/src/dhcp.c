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
#include <avr/pgmspace.h>
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

// --- fallback ke IP statis (lihat net_config.h) ---
static uint8_t  s_fallback_active = 0;   // 1 = sedang memakai IP statis cadangan
static uint8_t  s_attempt_started = 0;   // 1 = timer fallback sedang berjalan untuk percobaan ini
static uint32_t s_attempt_start_ms;      // waktu percobaan ini dimulai (boot / awal pencarian ulang)
static uint32_t s_fallback_retry_ms;     // patokan waktu retry DHCP di belakang layar

// --- deteksi link naik (kabel baru dicolok) & kirim ulang DISCOVER ---
static uint8_t  s_link_prev  = 1;   // anggap up saat boot: boot dgn kabel terpasang tidak diberi jeda
static uint8_t  s_link_event = 0;   // 1 = link baru naik, DHCP dicari segera setelah jeda settle
static uint32_t s_link_up_ms;       // kapan link naik
static uint32_t s_last_tx_ms;       // kapan DISCOVER terakhir dikirim

#ifndef NET_DHCP_LINK_SETTLE_MS
#define NET_DHCP_LINK_SETTLE_MS 1000UL
#endif
#define DHCP_DISCOVER_RESEND_MS 3000UL

static const uint8_t BCAST_IP[4] = {255, 255, 255, 255};

#if NET_DHCP_FALLBACK_STATIC
// Pasang NET_STATIC_* (disimpan di flash, bukan SRAM) sebagai IP aktif.
// DNS tidak diketahui, jadi dikosongkan.
static void dhcp_apply_static(uint32_t now) {
    static const uint8_t ip[4] PROGMEM = NET_STATIC_IP;
    static const uint8_t nm[4] PROGMEM = NET_STATIC_NETMASK;
    static const uint8_t gw[4] PROGMEM = NET_STATIC_GATEWAY;
    memcpy_P(net_my_ip, ip, 4);
    memcpy_P(net_netmask, nm, 4);
    memcpy_P(net_gw_ip, gw, 4);
    memset(net_dns_ip, 0, 4);
    net_state_update_broadcast();

    s_fallback_active = 1;
    s_fallback_retry_ms = now;
    s_state = DHCP_STATE_INIT;   // dhcp_poll() menunggu jadwal retry di INIT
}
#endif

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

    // RFC 2131: selama belum punya lease, source IP harus 0.0.0.0. Saat
    // IP statis cadangan aktif, net_my_ip berisi IP statis - kirim DISCOVER/
    // REQUEST awal dengan source itu bisa dibuang router yang ketat. Jadi
    // sementara dikosongkan hanya untuk pengiriman ini. Renewal (BOUND/
    // RENEWING) tetap memakai IP asli.
    uint8_t saved_ip[4];
    uint8_t zero_src = (s_state != DHCP_STATE_BOUND && s_state != DHCP_STATE_RENEWING);
    if (zero_src) {
        memcpy(saved_ip, net_my_ip, 4);
        memset(net_my_ip, 0, 4);
    }
    udp_send_inplace(DHCP_CLIENT_PORT, BCAST_IP, DHCP_SERVER_PORT, total_len);
    if (zero_src)
        memcpy(net_my_ip, saved_ip, 4);
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
            s_fallback_active = 0;   // lease DHCP menggantikan IP statis (kalau ada)
            s_attempt_started = 0;   // percobaan berikutnya (mis. renewal gagal) mulai dari nol
        } else if (msg_type == DHCP_MSG_NAK) {
            s_state = DHCP_STATE_INIT; // server nolak, mulai ulang dari nol
        }
    }
}

void dhcp_start(void) {
    s_state = DHCP_STATE_INIT;
    s_fallback_active = 0;
    // Timer fallback mulai dihitung dari SINI (boot), tidak menunggu link
    // up atau DISCOVER pertama. Kalau kabel tidak terpasang / link tidak
    // pernah naik, device tetap pindah ke IP statis setelah
    // NET_DHCP_FALLBACK_MS - bukan menggantung tanpa IP selamanya.
    s_attempt_started = 1;
    s_attempt_start_ms = millis_now();
    s_link_prev = 1;
    s_link_event = 0;
    memset(net_my_ip, 0, 4);
    udp_listen(DHCP_CLIENT_PORT, dhcp_on_packet);
}

void dhcp_poll(void) {
    uint32_t now = millis_now();

    // Pantau transisi link. Link naik = kabel baru dicolok / router hidup:
    // DHCP harus dicari SEKARANG (setelah jeda settle), walau sedang di IP
    // statis cadangan dan jadwal retry-nya belum tiba.
    uint8_t link = enc_link_up();
    if (link && !s_link_prev) {
        s_link_up_ms = now;
        s_link_event = 1;
    } else if (!link) {
        s_link_event = 0;
    }
    s_link_prev = link;

#if NET_DHCP_FALLBACK_STATIC
    // Sudah cukup lama mencoba tanpa hasil (link down ATAU router diam) ->
    // pindah ke IP statis. Hanya untuk pencarian IP; renewal (RENEWING)
    // punya jalurnya sendiri.
    if (!s_fallback_active && s_attempt_started &&
        s_state != DHCP_STATE_BOUND && s_state != DHCP_STATE_RENEWING &&
        (now - s_attempt_start_ms) >= NET_DHCP_FALLBACK_MS) {
        dhcp_apply_static(now);
    }
#endif

    switch (s_state) {
    case DHCP_STATE_INIT:
#if NET_DHCP_FALLBACK_STATIC
        // Sudah pakai IP statis: DHCP cuma dicoba lagi tiap interval retry
        // (0 = tidak pernah).
        // Kecuali link baru saja naik: itu melewati gerbang retry.
        if (s_fallback_active && !s_link_event &&
            (NET_DHCP_RETRY_AFTER_FALLBACK_MS == 0UL ||
             (now - s_fallback_retry_ms) < NET_DHCP_RETRY_AFTER_FALLBACK_MS))
            break;
#endif
        // Percobaan baru (mis. setelah renewal gagal): mulai hitung timer
        // fallback SEKARANG, sebelum cek link, supaya link yang tidak
        // pernah naik pun tetap berujung ke IP statis.
        if (!s_attempt_started) {
            s_attempt_started = 1;
            s_attempt_start_ms = now;
        }

        // Tahan DISCOVER sampai link fisik up: frame yang dikirim saat PHY
        // belum selesai negosiasi hilang percuma (dan retry baru 10 dtk
        // lagi). Ini penyebab "harus restart beberapa kali" sebelumnya.
        if (!link)
            break;

        // Link baru naik: beri PHY/switch/router waktu settle sebelum
        // DISCOVER pertama (frame terlalu awal hilang percuma).
        if (s_link_event && (now - s_link_up_ms) < NET_DHCP_LINK_SETTLE_MS)
            break;
        s_link_event = 0;

        s_xid = now; // "acak" secukupnya - lihat komentar di atas file
        if (!s_fallback_active)
            memset(net_my_ip, 0, 4);   // IP statis cadangan JANGAN dihapus
        dhcp_send(DHCP_MSG_DISCOVER, NULL);
        s_state = DHCP_STATE_SELECTING;
        s_state_timer = now;
        s_last_tx_ms = now;
        break;

    case DHCP_STATE_SELECTING:
    case DHCP_STATE_REQUESTING:
        // Kirim ulang DISCOVER (xid SAMA) tiap 3 dtk selama jendela 10 dtk:
        // satu frame yang hilang tidak lagi berarti menunggu 10 dtk penuh.
        if (s_state == DHCP_STATE_SELECTING &&
            (now - s_last_tx_ms) >= DHCP_DISCOVER_RESEND_MS &&
            (now - s_state_timer) <= DHCP_TIMEOUT_MS) {
            dhcp_send(DHCP_MSG_DISCOVER, NULL);
            s_last_tx_ms = now;
        }
        if ((now - s_state_timer) > DHCP_TIMEOUT_MS) {
            s_state = DHCP_STATE_INIT; // timeout - ulang dari DISCOVER
            if (s_fallback_active)
                s_fallback_retry_ms = now;   // retry berikutnya dihitung dari sini
        }
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

uint8_t dhcp_using_fallback(void) {
    return s_fallback_active;
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