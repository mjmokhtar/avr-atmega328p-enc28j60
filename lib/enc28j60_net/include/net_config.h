// net_config.h — konfigurasi project: pin, ukuran buffer, feature flags.
//
// Semua nilai di sini boleh (dan harus) disesuaikan MJ ke wiring/kebutuhan
// board aktual. File ini gak punya logic, cuma #define.

#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdint.h>

// ---------------------------------------------------------------------
// Pin chip-select (CS) ENC28J60
// ---------------------------------------------------------------------
// Default di bawah pakai PB2 (= pin fisik D10 di board gaya Arduino Uno),
// yaitu pin hardware SS bawaan ATmega328. Sengaja BUKAN pin 8 seperti
// default EtherCard, supaya modul SPI hardware gak pernah salah pindah
// ke mode slave gara-gara SS jadi input di tempat lain.
// Ganti ketiga define ini kalau CS ENC28J60 kamu di pin lain.
#define ENC_CS_DDR      DDRB
#define ENC_CS_PORT     PORTB
#define ENC_CS_BIT      PB2

// ---------------------------------------------------------------------
// Clock CPU — HARUS sinkron dengan F_CPU di platformio.ini
// ---------------------------------------------------------------------
#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// ---------------------------------------------------------------------
// Ukuran buffer paket bersama (RX dan TX pakai buffer yang sama)
// ---------------------------------------------------------------------
// 2KB RAM total di ATmega328 — buffer ini yang paling banyak makan RAM.
// EtherCard biasa pakai 500-800 byte. Mulai dari 400, naikkan kalau perlu
// setelah lihat sisa RAM pas semua layer selesai.
#define NET_BUF_SIZE    400

// ---------------------------------------------------------------------
// Feature flags — matikan (0) yang gak dipakai buat hemat flash/RAM
// ---------------------------------------------------------------------
#define NET_ENABLE_ICMP     1   // ping reply
#define NET_ENABLE_UDP      1
#define NET_ENABLE_DHCP     1   // butuh NET_ENABLE_UDP
#define NET_ENABLE_NTP      1   // butuh NET_ENABLE_UDP
#define NET_ENABLE_TCP      1

// ---------------------------------------------------------------------
// Sumber IP address: DHCP atau statis
// ---------------------------------------------------------------------
// 1 = main.c panggil dhcp_start()/dhcp_poll(), IP didapat otomatis dari
//     router (perilaku default project ini).
// 0 = main.c isi net_my_ip/net_netmask/net_gw_ip langsung dari nilai di
//     bawah, dhcp.c gak pernah dipanggil sama sekali - gak perlu ada
//     server DHCP di jaringan.
#define NET_USE_DHCP    1

#if !NET_USE_DHCP
// Cuma dipakai kalau NET_USE_DHCP == 0 - ganti sesuai jaringan kamu.
#define NET_STATIC_IP       {192, 168, 1, 200}
#define NET_STATIC_NETMASK  {255, 255, 255, 0}
#define NET_STATIC_GATEWAY  {192, 168, 1, 1}
#endif

// ---------------------------------------------------------------------
// Renew DHCP otomatis saat link fisik pulih (opsional, lihat main.c)
// ---------------------------------------------------------------------
// Kalau link ENC28J60 down selama >= NET_LINK_DOWN_RENEW_MS lalu pulih
// lagi, main.c memanggil dhcp_force_renew() (konfirmasi ulang lease ke
// server, BUKAN discover dari nol - lihat komentar di dhcp.h) - jaga-jaga
// kalau selama device "hilang" itu, kabelnya dipindah ke jaringan/switch
// lain, atau IP-nya sempat dikasih ke device lain oleh server.
//
// Threshold ini SENGAJA tidak terlalu kecil - kedipan sesaat (order
// puluhan/ratusan ms, misal gara-gara noise power supply) TIDAK akan
// memicu ini, supaya gak spam DHCPREQUEST tiap kali link kedip sebentar.
// Naikkan kalau jaringan kamu punya link-flap normal yang lebih lama dari
// ini (switch yang lambat re-negotiate, dst).
#define NET_LINK_DOWN_RENEW_MS  3000UL

// Jumlah slot — bukan array besar EtherCard (8 sesi), disesuaikan RAM kecil.
// NET_MAX_TCP_LISTENERS = berapa port yang boleh di-tcp_listen() (server).
// NET_MAX_TCP_SESSIONS   = berapa KONEKSI aktif sekaligus (client + hasil
//                           accept dari listener), independen dari di atas.
#define NET_MAX_TCP_LISTENERS   1
#define NET_MAX_TCP_SESSIONS    2
#define NET_MAX_UDP_LISTENERS   3

#endif // NET_CONFIG_H