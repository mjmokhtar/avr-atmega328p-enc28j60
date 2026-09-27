# AVR ATmega328P ENC28J60

Driver ENC28J60 + stack TCP/IP (ARP, IP, ICMP, UDP, TCP, DHCP, NTP) untuk
ATmega328, ditulis pure AVR-C via PlatformIO — tanpa Arduino framework,
tanpa library pihak ketiga.

**Status: sudah diuji di hardware ENC28J60 asli — DHCP dapat IP, ICMP,
UDP echo, dan TCP echo semuanya jalan.** Bukan cuma lolos compile-check
lagi.

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
DNS server, Wake-on-LAN, Stash/BufferFiller) tidak diikutkan. NTP dipakai
(beda dari rencana awal yang membuang semua layer aplikasi EtherCard).

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

## Serial debug & protokol buat aplikasi lain (mis. ESP32)

UART TX-only, default **38400 baud, 8N1**. Ada 2 jenis baris di output:

- **Baris log manusia** (`[boot] ...`, `[status] ...`, `[TCP] koneksi
  baru ...`, dst) — cuma buat dibaca orang, formatnya bisa berubah kapan
  saja, JANGAN diparsing oleh program lain.
- **Baris data machine-readable** — 1 baris = 1 pesan, selalu diawali
  prefix tetap, aman diparsing dengan `startsWith()` + split. Baris
  selain 3 jenis ini boleh diabaikan begitu saja oleh parser.

| Prefix | Kapan muncul | Contoh |
|---|---|---|
| `NET:` | Sekali, begitu dapat IP (DHCP selesai / IP statis siap) | `NET:ip=192.168.8.184;mask=255.255.255.0;gw=192.168.8.1;dns=192.168.8.1` |
| `UDP:` | Tiap ada datagram UDP masuk ke port echo (lihat `DEMO_UDP_ECHO_PORT`) | `UDP:halo!` |
| `TCP:` | Tiap ada data TCP masuk ke port echo (lihat `DEMO_TCP_ECHO_PORT`) | `TCP:tes` |

Contoh parsing di ESP32 (Arduino framework), gak butuh library tambahan:

```cpp
void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("NET:")) {
      // pecah "ip=...;mask=...;gw=...;dns=..." per ';' lalu per '='
    } else if (line.startsWith("UDP:")) {
      String isi = line.substring(4);
    } else if (line.startsWith("TCP:")) {
      String isi = line.substring(4);
    }
    // baris lain (log manusia) otomatis kelewat kalau gak cocok prefix di atas
  }
}
```

**Batasan yang perlu diinget:** isi data (`UDP:`/`TCP:` payload) JANGAN
mengandung byte `\r`/`\n` sendiri (motong baris lebih cepat dari
seharusnya) — aman buat teks pendek, bukan buat data biner sembarang.
Panjang payload yang di-print juga dibatasi `UART_DATA_PRINT_MAX` (48
byte) di `src/main.c` biar gak nge-spam serial.

## Status implementasi

- [x] SPI driver + register ENC28J60 (enc28j60_hw.c)
- [x] Ethernet frame + ARP (eth_arp.c) + net_state.c (my_ip/my_mac bersama)
- [x] IPv4 + ICMP (ip_icmp.c)
- [x] UDP (udp.c)
- [x] DHCP client (dhcp.c) - bisa dimatikan, ganti IP statis lewat
      `NET_USE_DHCP` di net_config.h
- [x] NTP client (ntp.c, di atas UDP)
- [x] TCP (tcp.c) - client + server, 2 sesi, tanpa retransmission data
- [x] main.c demo (UART debug, UDP+TCP echo port bisa diganti lewat
      `DEMO_UDP_ECHO_PORT`/`DEMO_TCP_ECHO_PORT`)
- [x] Protokol serial machine-readable (`NET:`/`UDP:`/`TCP:`) buat
      dikonsumsi device lain (ESP32, dst)
- [x] Compile-check PlatformIO (lolos, lihat angka RAM/Flash di atas)
- [x] CI GitHub Actions (build tiap push/PR + auto-release tiap tag)
- [x] **Diuji di hardware ENC28J60 asli** - DHCP dapat IP, UDP echo, TCP
      echo semuanya terverifikasi jalan (bukan cuma compile-check)

## Bug yang ditemukan & diperbaiki lewat pengujian hardware asli

Dicatat di sini biar jelas apa yang sempat salah dan kenapa fix-nya
begitu - berguna kalau nanti ada perilaku aneh yang mirip:

1. **`millis.c` include `<avr/atomic.h>` (gak ada)** — harusnya
   `<util/atomic.h>`. Fatal compile error, ketahuan di compile-check
   pertama.
2. **LTO (Link-Time Optimization) + library custom bikin linker gagal**
   ("undefined reference" ke SEMUA fungsi kita) — fix: `build_unflags =
   -flto` di `platformio.ini`.
3. **DHCP gak pernah dapat IP** walau link fisik up — root cause: bit
   "broadcast" di header DHCP (`flags`, offset 10-11) gak pernah di-set,
   jadi server DHCP unicast balasan OFFER/ACK ke IP yang belum kita
   punya, dan paket itu DIBUANG oleh filter `eth_input()` kita sendiri.
   Fix: set bit `0x8000` itu di `dhcp_send()`.
4. **Koneksi TCP pertama ke device sering gagal ("Could not connect")**
   dari client baru yang belum pernah "dikenal" — root cause: balasan
   SYN-ACK/UDP reply pertama ke host baru harus nunggu 1 round-trip ARP
   dulu (cache miss), dan kalau client-side timeout-nya lebih pendek
   dari itu, klien keburu nyerah. Fix: `eth_arp_learn()` — begitu ada
   paket UDP/TCP masuk, cache ARP langsung diisi dari MAC pengirim yang
   BARU diterima (gak perlu request ARP terpisah lagi).

## IP tetap valid saat link fisik kedip (link flapping)

Diverifikasi dari log hardware asli: link ENC28J60 sempat kedip
(`link=down` muncul sebentar di antara `link=up`, kemungkinan besar gara-gara
power supply 3.3V yang lemah, bukan bug firmware) — dan `ip=OK` **tidak
pernah drop** selama itu. Ini memang perilaku yang benar: lease DHCP itu
time-based di sisi server, bukan link-state-based, jadi kedipan link
sesaat gak bikin IP jadi gak valid.

Tapi ada 1 gap yang sudah ditutup: kalau link down-nya **cukup lama**
(bukan cuma kedipan sesaat — default ambang `NET_LINK_DOWN_RENEW_MS` =
3000 ms di `net_config.h`), ada kemungkinan kecil kabel dipindah ke
jaringan/switch lain, atau IP kita sempat dikasih ke device lain oleh
server selama kita "hilang". Untuk jaga-jaga itu, begitu link pulih
setelah down selama itu, firmware otomatis memanggil `dhcp_force_renew()`
— minta konfirmasi ulang lease ke server (DHCPREQUEST, bukan DISCOVER
dari nol), tanpa membuang IP yang sedang dipakai kecuali server memang
menolaknya (NAK). Kalau IP ternyata berubah setelah ini, baris `NET:` di
serial akan dicetak ulang otomatis (bukan cuma sekali di boot).

Kedipan link yang lebih pendek dari ambang itu (kasus power-supply di
atas) sengaja TIDAK memicu apa pun — biar gak spam DHCPREQUEST ke server
tiap kali link kedip sesaat.

## Yang masih perlu diverifikasi (belum dilakukan)

- Uji jangka panjang lease DHCP renewal (normal, bukan dari link-recovery)
  belum dicoba - baru pengujian singkat.
- `dhcp_force_renew()` saat link-recovery (lihat bagian "IP tetap valid
  saat link fisik kedip" di atas) belum diuji di skenario kabel benar-benar
  dicabut lalu dicolok ke jaringan lain - baru diverifikasi lewat pembacaan
  kode terhadap log link-flap yang ada.
- Ganti `MY_MAC` di `src/main.c` kalau bikin lebih dari 1 device (hindari
  MAC bentrok di jaringan yang sama).

## Wiring ENC28J60 (default `net_config.h`)

| Pin modul ENC28J60 | Sambung ke ATmega328 (gaya Arduino Uno) | Keterangan |
|---|---|---|
| VCC | **3.3V** | Chip ENC28J60-nya 3.3V, bukan 5V - cek datasheet modul kalau ada regulator on-board |
| GND | GND | Wajib common ground |
| SCK | D13 (PB5) | Pin SPI hardware, gak bisa pindah |
| SI / MOSI | D11 (PB3) | Pin SPI hardware, gak bisa pindah |
| SO / MISO | D12 (PB4) | Pin SPI hardware, gak bisa pindah |
| CS / SS | D10 (PB2) | Software-defined, boleh diganti (lihat di bawah) |
| INT | tidak disambung | Driver ini polling, bukan interrupt-driven |
| RESET (kalau ada) | tidak disambung | Reset dilakukan lewat software (`OP_SOFT_RESET`) |

Ganti pin CS lewat 3 baris ini di `lib/enc28j60_net/include/net_config.h`:
```c
#define ENC_CS_DDR      DDRB
#define ENC_CS_PORT     PORTB
#define ENC_CS_BIT      PB2   // <- ganti sesuai pin baru
```