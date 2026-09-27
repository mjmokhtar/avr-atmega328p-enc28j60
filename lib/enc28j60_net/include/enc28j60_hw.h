// enc28j60_hw.h — driver level-register untuk Microchip ENC28J60.
//
// Layer ini HANYA tahu cara ngobrol SPI dengan chip: init, kirim frame
// mentah, terima frame mentah. Gak tahu apa-apa soal Ethernet/ARP/IP/dst
// — itu tugas layer di atasnya (eth_arp.c).
//
// Ditulis ulang dari nol dalam AVR-C murni, mengacu ke desain driver
// EtherCard (enc28j60.cpp/h) untuk urutan init register dan errata
// workaround hardware ENC28J60 (bug silikon yang didokumentasikan
// Microchip, bukan pilihan gaya kode) — bagian itu dipertahankan persis
// karena sudah "mahal" ditemukan orang lain lewat trial-error.

#ifndef ENC28J60_HW_H
#define ENC28J60_HW_H

#include <stdint.h>
#include "net_config.h"

// Buffer paket bersama — dipakai gantian oleh RX dan TX, dan oleh semua
// layer di atas (eth_arp/ip_icmp/udp/tcp) langsung lewat offset byte,
// TANPA copy antar layer. Ini yang bikin hemat RAM di 2KB ATmega328.
extern uint8_t net_buf[NET_BUF_SIZE];

// Inisialisasi SPI + reset + konfigurasi MAC/PHY chip.
// mac: pointer ke 6 byte MAC address yang mau dipasang ke chip.
// Return: revisi silikon ENC28J60 (>0 kalau berhasil), 0 kalau gagal
// (biasanya berarti salah wiring SPI atau chip gak terdeteksi).
uint8_t enc_init(const uint8_t mac[6]);

// True kalau link fisik (kabel Ethernet) terhubung dan PHY lock.
uint8_t enc_link_up(void);

// Kirim isi net_buf[0..len-1] sebagai satu frame Ethernet.
// Blocking: nunggu transmisi selesai (dibatasi hitungan iterasi, gak
// akan hang selamanya kalau chip macet — lihat komentar di .c).
void enc_packet_send(uint16_t len);

// Kalau ada paket masuk, disalin ke net_buf[] dan panjangnya dikembalikan.
// Return 0 kalau gak ada paket baru.
uint16_t enc_packet_receive(void);

// Kontrol filter penerimaan paket (dipakai misal oleh DHCP yang butuh
// terima broadcast sementara, lalu dimatikan lagi setelah dapat IP).
void enc_enable_broadcast(uint8_t temporary);
void enc_disable_broadcast(uint8_t temporary);
void enc_enable_multicast(void);
void enc_disable_multicast(void);
void enc_enable_promiscuous(uint8_t temporary);
void enc_disable_promiscuous(uint8_t temporary);

// Hemat daya kalau link lagi gak dipakai (opsional).
void enc_power_down(void);
void enc_power_up(void);

#endif // ENC28J60_HW_H