// eth_arp.h — layer frame Ethernet + ARP.
//
// Tugas layer ini: bungkus/buka header Ethernet, dan jaga cache MAC
// untuk 2 slot saja (gateway + 1 host tujuan aktif) — BUKAN cache ARP
// penuh multi-host, sesuai keterbatasan RAM (lihat ERD.md).

#ifndef ETH_ARP_H
#define ETH_ARP_H

#include <stdint.h>

void eth_arp_init(void);

// Panggil setiap iterasi loop utama (ada paket atau tidak), buat retry
// ARP request yang masih pending (non-blocking, dijadwal pakai millis()).
void arp_poll(void);

// Cari MAC address tujuan untuk kirim paket ke dst_ip:
// - Kalau dst_ip di subnet lokal -> resolve MAC host itu langsung.
// - Kalau di luar subnet -> resolve MAC gateway (paket lewat gateway).
// Return 1 kalau MAC ketemu di cache (out_mac terisi, siap kirim).
// Return 0 kalau belum ada -> ARP request otomatis dikirim di balik
// layar, caller cukup coba lagi di iterasi loop berikutnya.
uint8_t eth_resolve_mac(const uint8_t dst_ip[4], uint8_t out_mac[6]);

// "Ajarin" cache ARP secara paksa dari MAC yang BARU KITA TERIMA (misal
// pas ip_input() dapat paket dari ip/mac tertentu) - TANPA kirim ARP
// request, langsung isi slot cache tujuan. Dipakai supaya balasan
// pertama (UDP reply / TCP SYN-ACK) ke pengirim itu gak perlu nunggu
// 1 round-trip ARP lagi - kita kan baru saja "dengar" MAC-nya langsung
// dari frame yang diterima, jadi gak perlu tanya ulang lewat ARP.
// Aman dipanggil untuk IP di luar subnet juga (cache-nya cuma gak akan
// pernah dipakai eth_resolve_mac() buat kasus itu, lihat komentar di .c).
void eth_arp_learn(const uint8_t ip[4], const uint8_t mac[6]);

// Tulis header Ethernet (dst MAC, src MAC = net_my_mac, ethertype) ke
// net_buf[0..13]. Payload (ARP/IP) ditulis layer pemanggil di offset 14+.
void eth_build_header(const uint8_t dst_mac[6], uint16_t ethertype);

// Proses 1 frame yang baru masuk (len = hasil enc_packet_receive(), bisa 0).
// Return 1 kalau ini frame IP yang ditujukan ke kita dan BELUM diproses
// (caller lanjut panggil layer IP). Return 0 kalau sudah selesai
// ditangani di sini (ARP) atau memang bukan buat kita / bukan IP.
uint8_t eth_input(uint16_t len);

#endif // ETH_ARP_H