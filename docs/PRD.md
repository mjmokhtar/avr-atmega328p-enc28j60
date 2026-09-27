# PRD — enc28j60_stack

## Problem / Goal
MJ butuh device ATmega328 yang bisa komunikasi Ethernet (TCP dan UDP) lewat
chip ENC28J60, tanpa memakai library pihak ketiga (EtherCard, UIPEthernet,
dll) dan tanpa Arduino framework — pure AVR-C via PlatformIO. ENC28J60 hanya
MAC+PHY (tidak punya stack TCP/IP di hardware seperti W5500), jadi seluruh
layer ARP/IP/ICMP/UDP/TCP/DHCP ditulis sendiri di firmware.

Referensi desain: library EtherCard (njh/ethercard) — diamati, diambil
sebagian teknik (buffer+offset byte tunggal, errata workaround ENC28J60,
alur DHCP state machine), ditulis ulang dalam C murni, dan dibuang bagian
yang di luar scope (HTTP client, DNS, WOL, Stash/BufferFiller).

## Scope
**In scope (v1):**
- Driver ENC28J60 pure AVR-C: SPI raw register, bank switching, init
  MAC/PHY, packetSend/packetReceive, errata workaround (late collision,
  TXRST reset tiap kirim).
- Ethernet frame layer + ARP (request/reply, cache MAC gateway + 1 host
  tujuan — sesuai keterbatasan RAM, bukan cache penuh).
- IPv4 (build/parse header + checksum) + ICMP echo reply (untuk debug ping).
- UDP (send/receive, bisa jadi client dan listener).
- DHCP client (state machine: INIT → SELECTING → REQUESTING → BOUND →
  RENEWING), device dapat IP otomatis dari router.
- TCP (state machine sederhana, single-packet payload per request/response,
  multi-session lewat bit-packing port seperti EtherCard — bisa jadi client
  dan server).
- NTP client (request waktu ke NTP server via UDP port 123, parsing
  timestamp balasan) — ringan karena numpang di atas UDP layer yang sudah
  ada, tidak perlu layer parsing tambahan seperti HTTP.
- Arsitektur: layering standar network stack (SPI → ENC28J60 driver →
  Ethernet/ARP → IP/ICMP → UDP+DHCP → TCP), TANPA mediator layer terpisah.
- Tanpa RTOS — single super-loop polling, non-blocking (tidak ada
  `delay()`/busy-wait panjang di `loop()` utama; DHCP/TCP jalan sebagai
  state machine yang dipoll, bukan fungsi blocking).
- WDT (watchdog) dipakai sebagai pengaman kegagalan asli (1x reset per
  iterasi loop, timeout > durasi worst-case 1 iterasi) — bukan tambalan
  untuk blocking call.

**Out of scope (v1):**
- HTTP client/server (parsing header teks, buffer request/response yang
  besar, dan biasanya banyak koneksi paralel — berisiko bikin RAM 2KB
  ATmega328 kolaps kalau dipaksa masuk).
- DNS client, Wake-on-LAN.
- TCP retransmission/window scaling penuh (RFC-compliant) — payload
  diasumsikan muat dalam 1 paket per request/response, sesuai keterbatasan
  RAM ATmega328 (2KB).
- ARP cache penuh untuk banyak host LAN sekaligus.

## Constraints
- Target MCU: ATmega328 (2KB SRAM, 32KB flash) — tanpa Arduino framework,
  PlatformIO dengan toolchain avr-gcc langsung.
- Chip Ethernet: ENC28J60 (MAC+PHY only, SPI).
- Device berperan sebagai client dan server (baik UDP maupun TCP).
- IP address: DHCP secara default, tapi bisa di-switch ke static IP lewat
  1 toggle di net_config.h (`NET_USE_DHCP`) — dhcp.c gak ikut kepakai
  sama sekali kalau static dipilih.
- Tidak boleh pakai library ENC28J60/TCP-IP pihak ketiga — driver ditulis
  sendiri (boleh mengacu ke desain EtherCard sebagai referensi belajar).

## Open questions
(none untuk saat ini — semua keputusan arsitektur besar sudah disepakati;
detail lebih lanjut akan muncul saat implementasi tiap layer.)