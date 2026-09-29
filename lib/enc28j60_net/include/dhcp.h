// dhcp.h — DHCP client, state machine non-blocking (RFC 2131 disederhanakan).
//
// Beda penting dari behaviour "dhcpSetup()" ala EtherCard yang blocking:
// di sini TIDAK ADA while() yang nunggu sampai dapat IP. dhcp_poll() cuma
// mengecek "apa saatnya kirim/retry/renew sekarang", lalu balik ke
// caller. main.c yang manggil ini tiap iterasi loop.

#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

typedef enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_RENEWING
} dhcp_state_t;

// Mulai proses DHCP dari nol: reset ke INIT, daftar listener UDP port 68.
// Panggil sekali di main.c setelah udp_init().
void dhcp_start(void);

// Panggil tiap iterasi loop utama (bukan cuma sekali). Non-blocking.
//
// DISCOVER pertama SENGAJA ditahan sampai link fisik up (enc_link_up()):
// frame yang dikirim saat PHY belum selesai negosiasi link hilang percuma,
// dan percobaan berikutnya baru 10 detik kemudian.
//
// Kalau NET_DHCP_FALLBACK_STATIC == 1 (net_config.h) dan lease belum ada
// NET_DHCP_FALLBACK_MS setelah dhcp_start() - entah router diam atau link
// tidak pernah naik - IP statis dipasang otomatis; DHCP lalu dicoba lagi
// tiap NET_DHCP_RETRY_AFTER_FALLBACK_MS (0 = berhenti). DISCOVER hanya
// dikirim saat link up, jadi kalau kabel baru dicolok belakangan, lease
// DHCP dicari begitu link naik dan menggantikan IP statis.
void dhcp_poll(void);

// 1 kalau IP yang sedang dipakai adalah IP statis cadangan (DHCP gagal),
// 0 kalau tidak (belum fallback, atau sudah dapat lease DHCP).
uint8_t dhcp_using_fallback(void);

dhcp_state_t dhcp_get_state(void);

// Minta konfirmasi ulang lease SEKARANG (dipanggil main.c saat link fisik
// pulih setelah down cukup lama) - TANPA buang net_my_ip yang sekarang,
// beda dari dhcp_start() yang mulai dari nol (DISCOVER, IP jadi 0.0.0.0
// dulu). Cukup kirim DHCPREQUEST (broadcast) ke lease yang sudah ada -
// kalau server masih setuju, kita dapat ACK dan lanjut pakai IP yang
// sama; kalau server bilang NAK (server berubah/IP sudah dikasih ke
// device lain), s_state balik ke INIT dan dhcp_poll() otomatis DISCOVER
// ulang dari nol lewat jalur normal.
//
// Sengaja HANYA bertindak kalau state saat ini BOUND - kalau lagi
// SELECTING/REQUESTING/RENEWING/INIT (proses lain sudah berjalan),
// panggilan ini diabaikan supaya gak nabrak transaksi (xid) yang sedang
// berlangsung.
void dhcp_force_renew(void);

#endif // DHCP_H