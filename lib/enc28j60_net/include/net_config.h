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
//
// PENTING: nilai ini juga otomatis jadi batas MSS (Maximum Segment Size)
// yang diiklankan TCP kita ke peer (lihat TCP_MSS_ADVERTISE di tcp.c) -
// peer yang taat RFC gak akan kirim segment TCP lebih besar dari sisa
// buffer ini. Kalau kamu NAIKKAN NET_BUF_SIZE, MSS yang diiklankan ikut
// naik otomatis (gak perlu ubah tempat lain).
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

// IP statis. Dipakai kalau NET_USE_DHCP == 0, DAN sebagai cadangan kalau
// DHCP gagal (lihat NET_DHCP_FALLBACK_STATIC di bawah).
//
// WAJIB disesuaikan dengan jaringan kamu: subnet-nya harus sama dengan
// router, dan alamatnya harus DI LUAR rentang yang dibagikan DHCP router
// (kalau tidak, bisa bentrok dengan perangkat lain). Nilai di bawah
// mengikuti log hardware di README (jaringan 192.168.8.x).
#define NET_STATIC_IP       {192, 168, 8, 200}
#define NET_STATIC_NETMASK  {255, 255, 255, 0}
#define NET_STATIC_GATEWAY  {192, 168, 8, 1}

// ---------------------------------------------------------------------
// Fallback: kalau DHCP tidak dapat IP, pakai IP statis di atas
// ---------------------------------------------------------------------
// 1 = kalau sampai NET_DHCP_FALLBACK_MS ms sejak boot belum ada lease
//     (baik karena router tidak menjawab MAUPUN karena kabel tidak
//     terpasang / link tidak naik), device pindah ke NET_STATIC_*. Baris
//     "[NET] fallback static" muncul di serial.
// 0 = perilaku lama: DHCP diulang terus tanpa batas, tanpa IP statis.
// Hanya berlaku kalau NET_USE_DHCP == 1.
#define NET_DHCP_FALLBACK_STATIC   1

// Batas waktu sebelum pindah ke IP statis, dihitung dari BOOT (bukan dari
// link up). Kalau link normal (naik dalam ~1-3 dtk), ada 2 percobaan
// DISCOVER dalam rentang ini (diulang tiap 10 dtk). Naikkan kalau router/
// switch kamu lambat menyala.
#define NET_DHCP_FALLBACK_MS       15000UL

// Setelah pindah ke IP statis, DHCP tetap dicoba lagi di belakang layar
// tiap sekian ms (berguna kalau router baru menyala belakangan, mis. setelah
// listrik mati). Kalau berhasil, IP statis otomatis diganti lease DHCP dan
// baris "NET:" dicetak ulang. Isi 0 untuk berhenti mencoba setelah fallback
// (IP statis dipakai sampai device di-reset).
#define NET_DHCP_RETRY_AFTER_FALLBACK_MS  30000UL

// Jeda (ms) setelah link BARU naik (kabel dicolok) sebelum DISCOVER dikirim,
// supaya PHY/switch/router sempat settle. Saat link naik, DHCP dicari segera
// - melewati jadwal retry di atas - walau sedang memakai IP statis cadangan.
#define NET_DHCP_LINK_SETTLE_MS  1000UL

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