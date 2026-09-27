# enc28j60_stack

Driver ENC28J60 + stack TCP/IP (ARP, IP, ICMP, UDP, TCP, DHCP) untuk
ATmega328, ditulis pure AVR-C via PlatformIO — tanpa Arduino framework,
tanpa library pihak ketiga.

Status: semua layer inti selesai ditulis, BELUM di-compile-check /
BELUM diuji di hardware asli. Baca bagian "Yang masih perlu diverifikasi"
di bawah sebelum flash ke device.

## Kenapa nulis sendiri, bukan pakai EtherCard/UIPEthernet?

Supaya paham penuh setiap layer (SPI raw → MAC/PHY init → Ethernet frame →
ARP → IP → ICMP/UDP/TCP → DHCP) dan bebas dari overhead class-based C++
Arduino yang gak dibutuhkan di target 2KB RAM ini.

Desain library EtherCard (njh/ethercard) dipakai sebagai referensi belajar
untuk beberapa teknik (buffer+offset byte tunggal untuk hemat RAM, errata
workaround chip ENC28J60, alur DHCP state machine) — dibaca, dipahami, lalu
ditulis ulang dalam gaya sendiri. Bagian yang di luar scope (HTTP client,
DNS, Wake-on-LAN, Stash/BufferFiller) tidak diikutkan. NTP dipakai (beda
dari rencana awal yang membuang semua layer aplikasi EtherCard).

## Arsitektur

Layering standar network stack, tanpa mediator layer:

```
SPI raw (AVR register)
   |
ENC28J60 driver (init, packetSend/packetReceive, errata workaround)
   |
Ethernet frame + ARP
   |
IPv4 + ICMP
   |
UDP (+ DHCP client di atasnya)   TCP (state machine sederhana, multi-session)
```

Tanpa RTOS — single super-loop non-blocking. Lihat PRD.md untuk detail
scope dan keputusan yang sudah disepakati, ERD.md untuk struktur data.

## Struktur folder

```
src/main.c              - demo aplikasi (init -> DHCP -> loop UDP+TCP)
lib/enc28j60_net/        - library kita sendiri
  include/                - header per layer
  src/                    - implementasi per layer
docs/                    - dokumentasi tambahan
test/                    - (opsional, unit test logic yang platform-independent)
```

## Status implementasi

- [x] SPI driver + register ENC28J60 (enc28j60_hw.c)
- [x] Ethernet frame + ARP (eth_arp.c) + net_state.c (my_ip/my_mac bersama)
- [x] IPv4 + ICMP (ip_icmp.c)
- [x] UDP (udp.c)
- [x] DHCP client (dhcp.c) - bisa dimatikan, ganti IP statis lewat
      `NET_USE_DHCP` di net_config.h
- [x] NTP client (ntp.c, di atas UDP)
- [x] TCP (tcp.c) - client + server, 2 sesi, tanpa retransmission data
- [x] main.c demo (UART debug, UDP echo port 5000, TCP echo port 7)

## Yang masih perlu diverifikasi (belum dilakukan)

- **Compile-check** — belum ada file yang dicoba di-compile PlatformIO.
  Kemungkinan ada typo/nama field kecil yang baru ketahuan saat build.
- **Uji hardware asli** — timing SPI, wiring CS/MOSI/MISO/SCK, dan
  keseluruhan alur DHCP/TCP/UDP belum pernah dites di board sungguhan.
- **Ukuran flash/RAM aktual** — belum diukur (`avr-size`) apakah muat di
  32KB flash / 2KB RAM ATmega328 dengan semua fitur aktif.
- Ganti `MY_MAC` di main.c kalau bikin lebih dari 1 device (hindari MAC
  bentrok di jaringan yang sama).