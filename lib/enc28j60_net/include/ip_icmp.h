// ip_icmp.h — IPv4 (build/parse/checksum) + ICMP echo reply.
//
// Fungsi checksum & ip_prepare_send/ip_send_finalize di sini dipakai
// BERSAMA oleh udp.c dan tcp.c nanti (pseudo-header checksum UDP/TCP
// butuh IP src/dst yang sudah ditulis duluan oleh ip_prepare_send) —
// makanya walau namanya "ip_icmp", dua fungsi itu bukan cuma buat ICMP.

#ifndef IP_ICMP_H
#define IP_ICMP_H

#include <stdint.h>

void ip_icmp_init(void);

// Checksum Internet standar (ones'-complement, RFC 1071) atas
// net_buf[start .. start+len-1], dimulai dari nilai awal pseudo_sum
// (pass 0 untuk checksum IP header polos; untuk UDP/TCP pass
// "protocol + panjang segmen" sebagai pseudo_sum, dan start = IP_SRC_P
// supaya 8 byte src+dst IP ikut terhitung sebagai pseudo-header).
// Hasil (2 byte, big-endian) ditulis ke net_buf[dest_offset .. +1] —
// caller HARUS men-nol-kan dulu ke-2 byte itu sebelum memanggil ini.
void net_fill_checksum(uint16_t dest_offset, uint16_t start, uint16_t len,
                       uint16_t pseudo_sum);

// Kebalikan dari net_fill_checksum: VERIFIKASI checksum paket yang BARU
// DITERIMA (bukan menghitung buat dikirim). Argumen sama persis (start,
// len, pseudo_sum yang sesuai protokolnya) TAPI beda 1 hal penting: byte
// checksum si pengirim TIDAK di-nol-kan dulu - ikut terhitung apa
// adanya. Sifat checksum Internet (ones'-complement): kalau datanya
// gak berubah selama pengiriman, jumlah semua word 16-bit (termasuk
// checksum itu sendiri) akan selalu 0xFFFF. Return 1 kalau valid, 0
// kalau korup (paket harus dibuang, JANGAN diproses).
uint8_t net_verify_checksum(uint16_t start, uint16_t len, uint16_t pseudo_sum);

// Tulis header IP (versi/IHL, TTL, protocol, src=net_my_ip, dst=dst_ip,
// total length) ke net_buf, checksum IP masih di-nol-kan (belum final).
// Panggil ini SEBELUM caller menghitung checksum UDP/TCP-nya sendiri,
// supaya src/dst IP sudah ada di buffer untuk pseudo-header mereka.
void ip_prepare_send(const uint8_t dst_ip[4], uint8_t protocol, uint16_t total_len);

// Finalisasi: hitung checksum IP header, resolve MAC tujuan (ARP kalau
// perlu), bangun header Ethernet, dan kirim ke ENC28J60.
// Return 1 kalau terkirim. Return 0 kalau MAC tujuan belum ada di cache
// ARP (paket ini DIBUANG, bukan diantre — caller yang harus retry
// dengan membangun ulang paketnya di iterasi loop berikutnya).
uint8_t ip_send_finalize(uint16_t total_len);

// Proses 1 frame IP yang sudah lolos eth_input()==1 (berarti buat kita).
// ICMP echo request otomatis dibalas di sini. Return IP_PROTO_UDP atau
// IP_PROTO_TCP kalau ada layer lain yang perlu lanjut proses payload-nya,
// return 0 kalau sudah tuntas (ICMP) atau protokol tidak didukung.
uint8_t ip_input(uint16_t len);

#endif // IP_ICMP_H