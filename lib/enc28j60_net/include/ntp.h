// ntp.h — NTP client minimal (RFC 5905 versi client-only, mode 3).
//
// Cuma butuh 1 request/reply, gak ada state machine kompleks - makanya
// jauh lebih ringan dibanding TCP atau bahkan DHCP.

#ifndef NTP_H
#define NTP_H

#include <stdint.h>

// Dipanggil tiap kali balasan NTP valid diterima. unix_time = detik sejak
// 1 Jan 1970 (epoch Unix) - sudah dikonversi dari epoch NTP (1900).
typedef void (*ntp_callback_t)(uint32_t unix_time);

// Daftar callback dan port listener NTP. Panggil sekali di main.c.
void ntp_init(ntp_callback_t callback);

// Kirim 1 request waktu ke server. Return 1 kalau terkirim, 0 kalau MAC
// tujuan belum ada di cache ARP (paket dibuang, panggil lagi nanti).
uint8_t ntp_request(const uint8_t server_ip[4]);

#endif // NTP_H