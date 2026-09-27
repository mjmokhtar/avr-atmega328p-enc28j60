// net_state.h — konfigurasi jaringan device saat ini (bukan konstanta).
//
// Semua layer (ARP, IP, DHCP, TCP, UDP) baca/tulis nilai-nilai ini lewat
// modul kecil ini, supaya gak ada layer yang harus "menembus" ke dalam
// state privat milik layer lain. Diisi oleh dhcp.c setelah lease
// didapat (atau lewat net_state_set_static() kalau nanti mau mode
// static IP juga).

#ifndef NET_STATE_H
#define NET_STATE_H

#include <stdint.h>

extern uint8_t net_my_mac[6];
extern uint8_t net_my_ip[4];
extern uint8_t net_netmask[4];
extern uint8_t net_gw_ip[4];
extern uint8_t net_broadcast_ip[4]; // dihitung otomatis dari my_ip | ~netmask

// True kalau device sudah punya IP valid (bukan 0.0.0.0) — dipakai layer
// lain (mis. TCP/UDP) buat cek "boleh kirim paket belum".
uint8_t net_has_ip(void);

// Hitung ulang net_broadcast_ip dari net_my_ip + net_netmask.
// Panggil tiap kali net_my_ip atau net_netmask berubah (dhcp.c).
void net_state_update_broadcast(void);

// True kalau target_ip ada di subnet lokal yang sama dengan device
// (dibandingkan lewat net_netmask) — dipakai buat nentuin kirim
// langsung ke MAC tujuan atau lewat gateway.
uint8_t net_is_same_subnet(const uint8_t target_ip[4]);

#endif // NET_STATE_H