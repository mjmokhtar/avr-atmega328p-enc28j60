# AVR ATmega328P ENC28J60 

Driver ENC28J60 + stack TCP/IP (ARP, IP, ICMP, UDP, TCP, DHCP, NTP) untuk
ATmega328, ditulis pure AVR-C via PlatformIO — tanpa Arduino framework,
tanpa library pihak ketiga.

> Ganti `OWNER/REPO` di badge URL atas dengan `username-github/nama-repo`
> kamu setelah di-push, biar badge-nya kebaca oleh GitHub.

**Status: sudah lolos compile-check PlatformIO (`pio run` -> SUCCESS).**
Belum diuji di hardware ENC28J60 asli. Baca bagian "Yang masih perlu
diverifikasi" di bawah sebelum flash ke device produksi.

```
RAM:   47.3% (968 / 2048 bytes)
Flash: 30.9% (9960 / 32256 bytes)
```
(diukur dengan semua fitur aktif sekaligus: ICMP + UDP + DHCP + NTP + TCP)

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
.github/workflows/        - CI (build tiap push) + auto-release (build tiap tag)
docs/                    - dokumentasi tambahan
test/                    - (opsional, unit test logic yang platform-independent)
```

## Build & flash

```bash
pio run                 # compile-check (env "uno")
pio run --target upload # compile + flash ke board (bootloader Arduino Uno)
pio device monitor -b 38400  # lihat log UART debug
```

Butuh board dengan chip **ATmega328** ber-bootloader Arduino Uno standar
(`board = uno` di `platformio.ini` — ini cuma soal cara flashing/fuse, kode
di dalamnya tetap pure AVR-C, bukan Arduino framework — lihat komentar di
`platformio.ini`).

## Pakai firmware jadi, tanpa compile sendiri

Tiap kali ada tag rilis (`v1.0.0`, dst) di-push ke repo ini, GitHub Actions
(`.github/workflows/release.yml`) otomatis compile dan attach file
`.hex` siap-flash ke halaman **Releases** repo — tinggal download, gak
perlu install PlatformIO atau compile apa pun:

1. Buka tab **Releases** di repo GitHub ini.
2. Download `enc28j60_stack-vX.Y.Z-uno.hex`.
3. Flash pakai `avrdude` langsung, atau lewat PlatformIO:
   ```bash
   avrdude -c arduino -p atmega328p -P COM3 -b 115200 -U flash:w:enc28j60_stack-vX.Y.Z-uno.hex:i
   ```
   (ganti `COM3` dengan port serial board kamu — `/dev/ttyUSB0` di Linux/Mac)

Tiap push/PR biasa (bukan tag) cuma menjalankan compile-check
(`.github/workflows/build.yml`) — hasil `.hex`-nya juga tersimpan sebagai
build artifact CI (bukan Release), buat ngecek hasil build per-commit.

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
- [x] Compile-check PlatformIO (lolos, lihat angka RAM/Flash di atas)
- [x] CI GitHub Actions (build tiap push/PR + auto-release tiap tag)

## Yang masih perlu diverifikasi (belum dilakukan)

- **Uji hardware asli** — timing SPI, wiring CS/MOSI/MISO/SCK, dan
  keseluruhan alur DHCP/TCP/UDP belum pernah dites di board ENC28J60
  sungguhan (baru lolos compile-check, belum jalan di chip fisik).
- Ganti `MY_MAC` di `src/main.c` kalau bikin lebih dari 1 device (hindari
  MAC bentrok di jaringan yang sama).

## Wiring SPI (default `net_config.h`)

| Sinyal   | Pin ATmega328 (gaya Arduino Uno) |
|----------|-----------------------------------|
| CS       | D10 (PB2)                          |
| MOSI     | D11 (PB3)                          |
| MISO     | D12 (PB4)                          |
| SCK      | D13 (PB5)                          |

MOSI/MISO/SCK adalah pin SPI hardware bawaan silikon ATmega328 (gak bisa
diganti sembarang pin lain). CS boleh diganti - edit `ENC_CS_DDR`/
`ENC_CS_PORT`/`ENC_CS_BIT` di `lib/enc28j60_net/include/net_config.h`.