**🇮🇩 Bahasa Indonesia** | [🇬🇧 English](README.en.md)

# AVR ATmega328P ENC28J60

Driver ENC28J60 + stack TCP/IP (ARP, IP, ICMP, UDP, TCP, DHCP, NTP) untuk
ATmega328, ditulis dengan AVR-C murni lewat PlatformIO — tanpa framework
Arduino, tanpa library pihak ketiga. Bus SPI dipakai bersama lewat lapisan
kecil `spi_bus`, sehingga perangkat SPI lain (SD card, LCD SPI, ...) bisa
ditambahkan nanti tanpa menyentuh kode jaringan. Output debug UART ada di
library `uart` tersendiri, terpisah dari kode aplikasi.

**Status: stack jaringan sudah diuji di hardware ENC28J60 asli — DHCP
mendapat IP, ICMP, UDP echo, dan TCP echo semuanya berfungsi.** Perlu dicatat,
tes itu dilakukan *sebelum* refactor bus SPI bersama (lihat
[Bus SPI bersama](#bus-spi-bersama-libspi_bus)); kode hasil refactor baru
diperiksa sintaksnya di compiler host dan masih perlu tes ulang hardware
yang tercantum di [Yang masih perlu diverifikasi](#yang-masih-perlu-diverifikasi-belum-dilakukan).

```
RAM:   [======    ]  58.8% (used 1204 bytes from 2048 bytes)
Flash: [====      ]  38.8% (used 12500 bytes from 32256 bytes)
```
(diukur dengan semua fitur aktif sekaligus: ICMP + UDP + DHCP + NTP + TCP,
sebelum refactor bus SPI bersama. Refactor diperkirakan menambah sekitar
12 byte RAM dan ~150 byte flash — ini estimasi, bukan hasil ukur; jalankan
ulang `pio run` untuk angka sebenarnya.)

## Apa arti istilah-istilah ini? (sesederhana mungkin)

Kalau jaringan masih baru buat kamu, singkatan di atas (ARP, IP, ICMP, ...)
bisa membingungkan. Berikut penjelasannya dengan perumpamaan sehari-hari,
BUKAN definisi buku teks:

- **MAC address** — nomor seri unik yang "dicap pabrik" di setiap perangkat
  jaringan (seperti nomor rangka motor). Tidak pernah berubah dan tidak bisa
  kamu pilih sendiri.
- **IP address** — alamat rumah di jaringan (misalnya `192.168.8.184`). Beda
  dengan MAC, alamat ini BISA berubah setiap kali pindah jaringan.
- **ARP** — cara mencari tahu "MAC address mana yang memegang IP ini". Seperti
  berteriak di satu kampung: *"Yang tinggal di rumah nomor 5, siapa nama
  aslimu?"* — lalu pemilik rumah itu menjawab. Perangkat mengingat jawabannya
  beberapa saat supaya tidak perlu berteriak lagi setiap kali mengirim surat
  ke rumah yang sama.
- **Ethernet frame** — amplop paling luar untuk mengirim data DI DALAM satu
  jaringan lokal (kabel/switch yang sama). Isinya hanya "dari MAC mana, ke MAC
  mana".
- **IP / paket IP** — amplop KEDUA, di dalam amplop Ethernet. Isinya "dari IP
  mana, ke IP mana" — inilah yang membuat data bisa berjalan lebih jauh,
  melewati banyak jaringan (sampai ke internet), tidak hanya satu kabel.
- **ICMP** — pesan "halo, kamu masih hidup?" (`ping`). Dipakai untuk mengecek
  apakah sebuah perangkat di jaringan menyala dan terjangkau, tanpa mengirim
  data sungguhan.
- **UDP** — mengirim surat TANPA tanda terima. Cepat dan ringan, tapi kalau
  suratnya hilang di jalan, pengirim tidak pernah tahu. Cocok untuk pesan
  kecil yang kalau sesekali hilang tidak fatal.
- **TCP** — mengirim surat DENGAN tanda terima wajib. Setiap surat harus
  dijawab "sudah diterima" sebelum surat berikutnya dikirim — lebih berat
  dari UDP, tapi andal untuk apa pun yang harus sampai utuh (perintah, file
  kecil).
- **TCP handshake** (3 langkah: SYN → SYN-ACK → ACK) — salam sopan sebelum
  percakapan serius: *"hai, boleh terhubung?"* → *"boleh, saya juga siap"* →
  *"oke, mulai ya"*. Baru setelah 3 langkah ini data sungguhan boleh dikirim.
- **Port** — nomor pintu KECIL di dalam satu alamat IP. Satu rumah (satu IP)
  bisa punya banyak pintu (port) untuk layanan berbeda sekaligus — port `7`
  untuk layanan echo, port `5000` untuk UDP echo di proyek ini, dan
  seterusnya.
- **DHCP** — "resepsionis" otomatis di jaringan yang membagikan alamat IP ke
  perangkat yang baru tersambung, jadi tidak ada yang perlu mengatur manual.
  Alamatnya hanya DIPINJAMKAN (ada masa berlaku, disebut *lease* — lihat poin
  berikut), bukan dimiliki selamanya.
- **DHCP lease** — masa berlaku "pinjaman" IP dari DHCP di atas. Sebelum habis,
  perangkat harus meminta server mengonfirmasi lagi (*renew*) kalau ingin
  tetap memakai IP yang sama.
- **NTP** — cara bertanya ke server waktu di internet "sekarang jam berapa?",
  supaya perangkat tanpa jam berbaterai (RTC) tetap tahu tanggal dan jam yang
  benar.
- **Checksum** — angka kecil yang dihitung dari isi paket dan dikirim bersama
  paket itu. Penerima menghitung ulang dari yang sampai — kalau hasilnya beda
  dengan checksum yang dikirim, berarti sebagian paket rusak di jalan
  (seperti menjumlahkan ulang total di struk belanja: kalau tidak cocok
  dengan yang tercetak, ada yang salah).
- **MSS (Maximum Segment Size)** — batas "jangan kirim satu potongan TCP lebih
  besar dari ini", supaya penerima sanggup menampungnya (RAM perangkat kita
  sangat kecil, jadi tidak muat potongan yang terlalu besar sekaligus).
- **Broadcast** — berteriak ke SEMUA orang di jaringan sekaligus, bukan
  berbisik ke satu alamat. Dipakai, misalnya, saat perangkat belum punya IP
  sama sekali dan perlu bertanya "ada server DHCP di sini?"
- **Link up / link down** — keadaan sambungan FISIK (kabel Ethernet). "up" =
  kabel tersambung dan aktif, "down" = tercabut / longgar / tidak ada sinyal
  sama sekali — ini soal kabelnya, BUKAN soal sudah dapat IP atau belum
  (keduanya dua hal yang berbeda, lihat "IP tetap valid saat link fisik
  putus-nyambung" di bawah).

## Standar TCP yang dipakai di sini

### Echo Protocol (RFC 862) — kenapa data yang dikembalikan PERSIS sama

Port echo TCP/UDP demo (`DEMO_TCP_ECHO_PORT`/`DEMO_UDP_ECHO_PORT`) di proyek
ini mengikuti **RFC 862 — Echo Protocol**: data apa pun yang dikirim ke
server dikirim balik **byte demi byte identik**, tidak diubah, tanpa
tambahan apa pun. Ini standar lama (dari awal era internet) yang ada hanya
untuk **tes konektivitas** — memastikan jalur kirim/terima di jaringan
benar-benar bekerja — bukan untuk aplikasi sungguhan. Kalau kamu kirim
`woii` dan perangkat menjawab `woii`, persis sama, itu **bukan bug** —
artinya server echo bekerja dengan benar. Kalau ingin perangkat menjawab hal
lain, itu sudah bukan protokol echo standar, dan kamu harus menulis logika
sendiri di `on_tcp_echo()` / `on_udp_echo()` (`src/main.c`).

### Flag TCP (bukan cuma ACK)

TCP punya beberapa "flag" 1-bit di header-nya, dipakai untuk menandai tujuan
tiap paket. Semuanya didefinisikan di
`lib/enc28j60_net/include/net_offsets.h`:

| Flag | Arti sederhana |
|---|---|
| `SYN` | "Aku mau mulai koneksi, boleh?" — dikirim di awal koneksi |
| `ACK` | "Oke, sudah diterima/dimengerti" — menempel di hampir setiap paket setelah handshake |
| `SYN`+`ACK` | Gabungan: "boleh terhubung, aku juga siap" (balasan di langkah 2 handshake) |
| `PSH` | "Ini data sungguhan, langsung serahkan ke aplikasi, jangan ditahan di buffer" |
| `FIN` | "Aku selesai mengirim, mau menutup koneksi" (pamit dengan sopan) |
| `RST` | "Ada yang salah/tidak valid, koneksi dibatalkan paksa" (bukan pamit, lebih kasar dari FIN) |
| `URG` | Data prioritas mendesak — **tidak dipakai** di proyek ini (field-nya selalu dinolkan, lihat `TCP_URGENT_P` di `tcp.c`) |

Alur di firmware ini (bisa langsung dicocokkan dengan baris `[TCP] ...` di
serial monitor kamu):

1. Klien mengirim **SYN** → perangkat menjawab **SYN+ACK** → klien menjawab
   **ACK** → koneksi menjadi `ESTABLISHED`, dan log `[TCP] koneksi baru`
   muncul.
2. Setiap kali data dikirim (ke arah mana pun) → **PSH+ACK** (lihat
   `tcp_send()` di `tcp.c`, yang selalu mengirim kombinasi 2 flag ini).
3. Satu pihak mengirim **FIN+ACK** → perangkat langsung menjawab **FIN+ACK**
   juga (penutupan satu sisi, bukan `CLOSE_WAIT` penuh — lihat batasan v1 di
   `tcp.h`) → sesi ditutup, dan log `[TCP] sesi ditutup` muncul.
4. Kalau **RST** datang → perangkat langsung menutup sesi tanpa membalas
   apa pun (lihat `tcp_input()`, blok `if (flags & TCP_FLAG_RST)`).

## Kenapa ditulis dari nol, bukan pakai EtherCard/UIPEthernet?

Supaya benar-benar paham setiap lapisan (SPI mentah → init MAC/PHY →
Ethernet frame → ARP → IP → ICMP/UDP/TCP → DHCP) dan menghindari overhead
C++ berbasis class dari Arduino yang tidak diperlukan di target RAM 2 KB.

Desain library EtherCard (njh/ethercard) dipakai sebagai referensi belajar
untuk beberapa teknik (satu buffer + offset byte untuk menghemat RAM,
workaround errata chip ENC28J60, alur state machine DHCP) — dibaca,
dipahami, lalu ditulis ulang dengan gaya sendiri. Bagian di luar cakupan
(HTTP client, DNS server, Wake-on-LAN, Stash/BufferFiller) tidak
dimasukkan. NTP disertakan (berbeda dari rencana awal yang membuang semua
lapisan aplikasi EtherCard).

## Arsitektur

Pelapisan stack jaringan standar, tanpa lapisan mediator:

```
SPI mentah (register AVR)
   |
spi_bus (bus bersama: CS per perangkat + mode SPI + clock)
   |
Driver ENC28J60 (init, packetSend/packetReceive, workaround errata)
   |
Ethernet frame + ARP
   |
IPv4 + ICMP
   |
UDP (+ klien DHCP di atasnya)   TCP (state machine sederhana, multi-sesi)
```

Tanpa RTOS — satu super-loop non-blocking. Lihat `docs/PRD.md` untuk
cakupan, batasan, dan catatan keputusan, serta `docs/ERD.md` untuk struktur
data dan anggaran RAM.

## Struktur folder

```
src/main.c              - aplikasi demo saja (init -> DHCP -> loop UDP+TCP)
lib/uart/                - output debug UART0 (hanya TX)
  include/                - uart.h
  src/                    - uart.c
lib/spi_bus/             - bus SPI bersama (descriptor perangkat, begin/end)
  include/                - spi_bus.h
  src/                    - spi_bus.c
lib/enc28j60_net/        - library jaringan buatan sendiri
  include/                - satu header per lapisan
  src/                    - satu implementasi per lapisan
.github/workflows/        - CI (build tiap push) + auto-release (build tiap tag)
docs/                    - dokumentasi tambahan
test/                    - (opsional, unit test untuk logika yang tidak bergantung platform)
```

## Bus SPI bersama (`lib/spi_bus`)

ATmega328P hanya punya SATU SPI hardware. Beberapa perangkat bisa berbagi
bus itu, tapi pin chip-select (CS) terpisah per perangkat saja tidak cukup:
tiap perangkat juga butuh **mode** dan **kecepatan clock** SPI sendiri
(ENC28J60: mode 0 pada fosc/2; SD card harus diinisialisasi pada ≤ 400 kHz;
display bervariasi). Karena itu setiap perangkat dideskripsikan dengan satu
`spi_dev_t`, dan `spi_begin()` menerapkan mode dan clock perangkat itu tepat
sebelum menarik CS-nya LOW. Driver perangkat tidak pernah menyentuh
`SPCR`/`SPSR` sendiri.

Karena firmware ini super-loop tunggal tanpa DMA dan tanpa RTOS, bus SPI
kedua tidak akan membuat apa pun berjalan paralel — satu bus dengan
beberapa chip-select adalah desain yang tepat di sini.

### API

| Fungsi | Fungsinya |
|---|---|
| `spi_bus_init()` | MOSI/SCK output, MISO input, SS (PB2) output HIGH, SPI master aktif. Idempotent. |
| `spi_dev_init(&dev)` | Jadikan pin CS perangkat sebagai output, HIGH (tidak terpilih). |
| `spi_begin(&dev)` | Terapkan mode + clock perangkat (hanya jika berbeda dari yang sedang aktif), lalu CS LOW. |
| `spi_end(&dev)` | CS HIGH (+ 8 clock dummy jika `SPI_DEV_RELEASE_MISO` diset). |
| `spi_xfer(b)` | Tukar satu byte (inline, secepat versi lama di dalam driver). |
| `spi_read(buf, len, fill)` / `spi_write(buf, len)` | Transfer massal. |
| `spi_bus_error()` | Jumlah pelanggaran transaksi bersarang sejak panggilan terakhir (lalu direset). |

Sebuah perangkat dideskripsikan seperti ini (milik ENC28J60, dari
`enc28j60_hw.c`):

```c
static const spi_dev_t s_enc_dev =
    SPI_DEV(ENC_CS_PORT, ENC_CS_DDR, ENC_CS_BIT, SPI_MODE0 | SPI_DIV2, 0);
```

Mode: `SPI_MODE0..3`. Pembagi clock: `SPI_DIV2 .. SPI_DIV128` (relatif
terhadap `F_CPU`). Flag: `SPI_DEV_RELEASE_MISO` (dibutuhkan SD card, lihat
di bawah).

### Aturan (wajib)

1. **Satu transaksi = `spi_begin()` … `spi_end()`**, selesai sebelum
   perangkat lain dipakai. Transaksi tidak boleh bersarang.
2. **Tidak ada ISR yang boleh memakai SPI.** ISR hanya mengeset flag
   `volatile`; pekerjaan sebenarnya dilakukan main loop. (Satu-satunya ISR di
   proyek ini adalah timer `millis`, yang tidak menyentuh SPI.)
3. **CS setiap perangkat harus dipaksa HIGH (`spi_dev_init()`) sebelum
   transaksi pertama ke perangkat MANA PUN di bus** — kalau tidak, pin CS
   yang mengambang bisa membuat perangkat lain ikut "mendengar" lalu lintas
   perangkat yang lain. Saat ini `enc_init()` melakukannya untuk ENC28J60;
   begitu ada lebih banyak perangkat, panggil `spi_bus_init()` dan
   `spi_dev_init()` tiap perangkat di `main()` *sebelum* `enc_init()`.

### Apa yang berubah dibanding driver lama

- Setup SPI, `spi_xfer`, dan penanganan CS dulu berupa fungsi `static` di
  dalam `enc28j60_hw.c`. Sekarang semuanya ada di `spi_bus`; driver ENC28J60
  hanya memiliki descriptor perangkatnya. API publik di `enc28j60_hw.h`
  tidak berubah, dan tidak ada lapisan atas (ARP/IP/UDP/TCP/DHCP/NTP) yang
  menyentuh SPI, jadi tidak ada yang berubah di atas driver.
- `cli()` lama di `chip_select()` dan `sei()` tanpa syarat di
  `chip_deselect()` **dihapus**. Tidak ada ISR yang memakai SPI, jadi
  keduanya tidak melindungi apa pun; `sei()` tanpa syarat bisa mengaktifkan
  kembali interrupt yang sengaja dimatikan pemanggil; dan memblokir
  interrupt selama transfer panjang perangkat lain (mis. blok SD 512 byte
  pada clock lambat) akan membuat `millis()` kehilangan tick. Kalau kamu
  curiga perubahan ini bermasalah, build dengan `-DSPI_BUS_DISABLE_IRQ=1`
  untuk mengembalikan pemblokiran interrupt (status register disimpan dan
  dipulihkan, jadi aman meski pemanggil sudah mematikan interrupt).
- Timing ENC28J60 tidak berubah: SPI mode 0, fosc/2 (8 MHz pada clock
  16 MHz). Jangan diperlambat.

### Menambah perangkat SPI lain

1. Pilih pin bebas untuk CS-nya dan definisikan `spi_dev_t` dengan mode dan
   clock dari datasheet-nya.
2. Panggil `spi_dev_init(&dev)` untuknya di `main()` *sebelum* `enc_init()`.
3. Bungkus setiap akses dengan `spi_begin(&dev)` … `spi_end(&dev)`. Jangan
   pernah menulis `SPCR`/`SPSR` sendiri.

Catatan untuk perangkat yang direncanakan berikutnya (driver belum ditulis):

- **SD card** — inisialisasi pada ≤ 400 kHz (`SPI_DIV64` = 250 kHz atau
  `SPI_DIV128` = 125 kHz pada 16 MHz), lalu pindahkan descriptor ke
  `SPI_DIV2`. Set `SPI_DEV_RELEASE_MISO`: banyak kartu terus menggerakkan
  MISO sampai menerima clock tambahan saat CS HIGH, yang akan bertabrakan
  dengan ENC28J60. Kendala sebenarnya adalah RAM (lihat di bawah), dan
  penulisan satu blok bisa memakan puluhan sampai ratusan milidetik, selama
  itu tidak ada paket yang dibaca — tulis secara batch, idealnya saat tidak
  ada paket menunggu, karena retransmisi TCP belum diimplementasikan.
- **LCD SPI 16x2 (backpack 74HC595)** — hanya-tulis, jadi tidak bisa
  bertabrakan di MISO. 74HC595 tidak punya chip-select sungguhan: ia
  menggeser data di *setiap* tepi SCK, dan outputnya baru berubah pada tepi
  naik pin latch. Pakai pin latch sebagai "CS": HIGH saat diam, LOW saat
  menggeser, HIGH di akhir. HD44780 tidak punya pembacaan busy-flag lewat
  backpack, jadi driver butuh delay (init ≈ 40–50 ms sekali saat boot,
  `clear` ≈ 1,6 ms): perbarui baris per baris dan hindari `clear` di main
  loop.

Memori: dengan 968 byte RAM yang sudah terpakai (hasil ukur), buffer sektor
SD 512 byte itu pas-pasan. Pindahkan dulu string literal `uart_print("...")`
di `main.c` ke flash (`PSTR`) — di AVR string literal biasa disalin ke SRAM.
FatFs/SdFat penuh tidak muat bersama stack jaringan ini; gunakan logging
sektor sekuensial mentah, atau Petit FatFs dengan file yang dialokasikan
di muka.

## Build & flash

```bash
pio run                 # cek kompilasi (env "uno")
pio run --target upload # kompilasi + flash ke board (bootloader Arduino Uno)
pio device monitor -b 38400  # lihat log debug UART
```

Kamu butuh board dengan chip **ATmega328** dan bootloader Arduino Uno standar
(`board = uno` di `platformio.ini` — ini hanya memengaruhi cara chip
di-flash/fuse; kodenya sendiri tetap AVR-C murni, bukan framework Arduino —
lihat komentar di `platformio.ini`).

## Memakai firmware jadi, tanpa kompilasi

Setiap kali tag rilis (`v1.0.0`, dst.) di-push ke repo ini, GitHub Actions
(`.github/workflows/release.yml`) mengompilasi otomatis dan melampirkan file
`.hex` siap-flash ke halaman **Releases** repo — tinggal unduh, tanpa perlu
memasang PlatformIO atau mengompilasi apa pun:

1. Buka tab **Releases** di repo GitHub ini.
2. Unduh `enc28j60_stack-vX.Y.Z-uno.hex`.
3. Flash dengan `avrdude` langsung, atau lewat PlatformIO:
   ```bash
   avrdude -c arduino -p atmega328p -P COM3 -b 115200 -U flash:w:enc28j60_stack-vX.Y.Z-uno.hex:i
   ```
   (ganti `COM3` dengan port serial board kamu — `/dev/ttyUSB0` di
   Linux/Mac)

Setiap push/PR biasa (bukan tag) hanya menjalankan cek kompilasi
(`.github/workflows/build.yml`) — hasil `.hex`-nya juga disimpan sebagai
artifact build CI (bukan Release), untuk memeriksa hasil build per commit.

## Debug serial & protokol untuk aplikasi lain (mis. ESP32)

UART hanya-TX, default **38400 baud, 8N1**. Kode UART ada di `lib/uart`
(`uart.h` / `uart.c`); ia hanya tahu cara mengirim teks ke pin TX (PD1) dan
tidak tahu apa-apa soal jaringan atau SPI. Format baris `NET:`/`UDP:`/`TCP:`
di bawah dibuat di `src/main.c`; `lib/uart` hanyalah pengirimnya.

| Fungsi | Fungsinya |
|---|---|
| `uart_init()` | Aktifkan transmitter (8N1). Panggil sekali di awal `main()`. |
| `uart_putc(c)` / `uart_print(s)` | Kirim satu karakter / string C (tanpa newline otomatis). Blocking. |
| `uart_print_ip(ip)` / `uart_print_ipport(ip, port)` | Cetak `a.b.c.d` / `a.b.c.d:port`. |
| `uart_print_data(data, len)` | Cetak byte payload mentah, dibatasi `UART_DATA_PRINT_MAX`, dengan `...(dipotong)` kalau terpotong. |

Baud rate dan batas payload bisa diganti dari `platformio.ini` dengan
`build_flags = -DUART_BAUD=115200UL -DUART_DATA_PRINT_MAX=32`. `uart.c`
butuh `F_CPU` (sudah disediakan `board = uno`) dan menghentikan build dengan
error kalau tidak ada, alih-alih diam-diam memakai baud rate yang salah.
Pengiriman bersifat blocking: pada 38400 baud satu karakter memakan sekitar
260 µs, jadi baris panjang menahan main loop beberapa milidetik — cetak
seperlunya saja.

Ada 2 jenis baris di output:

- **Baris log untuk manusia** (`[boot] ...`, `[status] ...`, `[TCP] koneksi
  baru ...`, dst.) — hanya untuk dibaca orang; formatnya bisa berubah kapan
  saja, program lain TIDAK boleh mem-parse-nya.
- **Baris data yang bisa dibaca mesin** — 1 baris = 1 pesan, selalu diawali
  prefix tetap, aman di-parse dengan `startsWith()` + split. Baris selain 3
  jenis ini boleh diabaikan begitu saja oleh parser.

| Prefix | Kapan muncul | Contoh |
|---|---|---|
| `NET:` | Sekali begitu IP didapat (DHCP selesai / IP statis siap), dan lagi setiap IP berubah | `NET:ip=192.168.8.184;mask=255.255.255.0;gw=192.168.8.1;dns=192.168.8.1` |
| `UDP:` | Untuk setiap datagram UDP yang masuk di port echo (lihat `DEMO_UDP_ECHO_PORT`) | `UDP:halo!` |
| `TCP:` | Untuk setiap potongan data TCP yang masuk di port echo (lihat `DEMO_TCP_ECHO_PORT`) | `TCP:tes` |

Contoh parsing di ESP32 (framework Arduino), tanpa library tambahan:

```cpp
void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("NET:")) {
      // pecah "ip=...;mask=...;gw=...;dns=..." dengan ';' lalu dengan '='
    } else if (line.startsWith("UDP:")) {
      String payload = line.substring(4);
    } else if (line.startsWith("TCP:")) {
      String payload = line.substring(4);
    }
    // baris lain (log manusia) otomatis dilewati kalau tidak cocok dengan prefix di atas
  }
}
```

**Batasan yang perlu diingat:** data (payload `UDP:`/`TCP:`) sendiri TIDAK
boleh mengandung byte `\r`/`\n` (memotong barisnya) — aman untuk teks
pendek, tidak untuk data biner sembarang. Panjang payload yang dicetak juga
dibatasi `UART_DATA_PRINT_MAX` (48 byte) di `lib/uart/include/uart.h`
supaya port serial tidak kebanjiran.

## Status implementasi

- [x] Driver SPI + register ENC28J60 (enc28j60_hw.c)
- [x] Bus SPI bersama (`lib/spi_bus`) — driver ENC28J60 dipindahkan ke sana
      (baru cek sintaks di host; tes ulang hardware masih tertunda)
- [x] Output debug UART dipisah dari `main.c` ke `lib/uart` (fungsi
      dipindahkan tanpa diubah; baru cek sintaks di host; tes ulang hardware
      masih tertunda)
- [x] Ethernet frame + ARP (eth_arp.c) + net_state.c (my_ip/my_mac bersama)
- [x] IPv4 + ICMP (ip_icmp.c)
- [x] UDP (udp.c)
- [x] Klien DHCP (dhcp.c) - bisa dimatikan, pindah ke IP statis dengan
      `NET_USE_DHCP` di net_config.h
- [x] DHCP menunggu link fisik sebelum DISCOVER pertama, pindah ke IP statis
      kalau lease tidak datang, dan langsung mengambil lease begitu kabel
      dicolok (lihat
      [Fallback DHCP ke IP statis](#fallback-dhcp-ke-ip-statis));
      terverifikasi di hardware
- [x] Klien NTP (ntp.c, di atas UDP)
- [x] TCP (tcp.c) - klien + server, 2 sesi, tanpa retransmisi data
- [x] Demo main.c (debug UART, port echo UDP+TCP bisa diganti lewat
      `DEMO_UDP_ECHO_PORT`/`DEMO_TCP_ECHO_PORT`)
- [x] Protokol serial yang bisa dibaca mesin (`NET:`/`UDP:`/`TCP:`) untuk
      dikonsumsi perangkat lain (ESP32, dll.)
- [x] Cek kompilasi PlatformIO (lolos, lihat angka RAM/Flash di atas)
- [x] GitHub Actions CI (build tiap push/PR + auto-release tiap tag)
- [x] **Diuji di hardware ENC28J60 asli** (sebelum refactor bus SPI) -
      DHCP mendapat IP, UDP echo dan TCP echo terbukti berfungsi (bukan
      sekadar lolos kompilasi)
- [ ] Driver SD card untuk logging data (direncanakan)
- [ ] Driver LCD SPI 16x2 (74HC595) (direncanakan)

## Bug yang ditemukan & diperbaiki lewat tes hardware asli

Dicatat di sini supaya jelas apa yang salah dan kenapa perbaikannya
berbentuk begitu - berguna kalau perilaku aneh yang mirip muncul lagi:

1. **`millis.c` meng-include `<avr/atomic.h>` (tidak ada)** — seharusnya
   `<util/atomic.h>`. Error kompilasi fatal, tertangkap di cek kompilasi
   pertama.
2. **LTO (Link-Time Optimization) + library buatan sendiri membuat linker
   gagal** ("undefined reference" ke SEMUA fungsi kita) — perbaikan:
   `build_unflags = -flto` di `platformio.ini`.
3. **DHCP tidak pernah dapat IP** padahal link fisik sudah up — akar
   masalah: bit "broadcast" di header DHCP (`flags`, offset 10-11) tidak
   pernah diset, sehingga server DHCP mengirim OFFER/ACK secara unicast ke
   IP yang belum kita punya, dan paket itu DIBUANG oleh filter `eth_input()`
   kita sendiri. Perbaikan: set bit `0x8000` itu di `dhcp_send()`.
4. **Koneksi TCP pertama ke perangkat sering gagal ("Could not connect")**
   dari klien baru yang belum "dikenal" — akar masalah: SYN-ACK/balasan UDP
   pertama ke host baru harus menunggu satu putaran ARP dulu (cache miss),
   dan kalau timeout di sisi klien lebih pendek dari itu, klien menyerah.
   Perbaikan: `eth_arp_learn()` — begitu paket UDP/TCP masuk, cache ARP
   langsung diisi dari MAC pengirim yang BARU SAJA diterima (tidak perlu
   request ARP terpisah lagi).

## Fallback DHCP ke IP statis

**Gejala yang melatarbelakangi ini:** setelah dinyalakan, perangkat kadang
tidak punya IP sampai tombol reset ditekan sekali atau dua kali.

**Penyebab (ditemukan dengan membaca `dhcp.c`):** DHCPDISCOVER pertama dulu
dikirim pada iterasi loop pertama setelah boot, tanpa mengecek link fisik.
Tepat setelah `enc_init()`, PHY masih bernegosiasi link (sekitar 1–3 detik,
lebih lama di sebagian switch), sehingga frame itu hilang begitu saja — dan
percobaan berikutnya baru 10 detik kemudian (`DHCP_TIMEOUT_MS`). Menekan
reset sebelum 10 detik itu lewat memulai siklus yang sama lagi.

**Apa yang berubah:**
1. `dhcp_poll()` menahan DISCOVER sampai `enc_link_up()` bernilai true. Kalau
   link naik *belakangan* (kabel dicolok), ia menunggu
   `NET_DHCP_LINK_SETTLE_MS` (1 detik) supaya PHY/switch sempat stabil, lalu
   langsung mencari.
2. Timer fallback dihitung dari `dhcp_start()` (boot), bukan dari DISCOVER
   pertama. Kalau lease tidak didapat dalam `NET_DHCP_FALLBACK_MS` (default
   15 detik) — router diam **atau** kabel tidak pernah dicolok — perangkat
   pindah ke `NET_STATIC_IP` / `NET_STATIC_NETMASK` / `NET_STATIC_GATEWAY`.
   Serial menampilkan `[NET] fallback static`, lalu baris `NET:` seperti
   biasa.
3. Selama mencari, DISCOVER dikirim ulang tiap 3 detik (transaction id
   sama) di dalam jendela 10 detik, jadi satu frame yang hilang tidak lagi
   memakan 10 detik.
4. DISCOVER dan REQUEST pertama dikirim dengan source IP 0.0.0.0 (RFC 2131),
   bahkan saat IP statis aktif; IP statis dipulihkan tepat setelah
   pengiriman. Renewal tetap memakai source IP asli.
5. Setelah fallback, DHCP dicoba lagi di belakang layar tiap
   `NET_DHCP_RETRY_AFTER_FALLBACK_MS` (default 30 detik; `0` = tidak
   pernah). Kejadian link-up (kabel dicolok) melewati penantian itu dan
   langsung mencari. IP statis tidak pernah dihapus selama pencarian. Kalau
   lease datang, ia menggantikan IP statis, serial menampilkan
   `[NET] DHCP ok`, dan `NET:` dicetak lagi. Sesi TCP yang terbuka pada IP
   lama hilang saat itu terjadi. Kalau DHCP terus gagal, perangkat tetap
   memakai IP statis.

Semuanya diatur di `net_config.h`:

```c
#define NET_STATIC_IP       {192, 168, 8, 200}   // harus cocok dengan subnet KAMU dan berada
#define NET_STATIC_NETMASK  {255, 255, 255, 0}   // di luar pool DHCP router
#define NET_STATIC_GATEWAY  {192, 168, 8, 1}
#define NET_DHCP_FALLBACK_STATIC   1              // 0 = perilaku lama (coba terus selamanya)
#define NET_DHCP_FALLBACK_MS       15000UL        // dihitung dari boot
#define NET_DHCP_RETRY_AFTER_FALLBACK_MS  30000UL
#define NET_DHCP_LINK_SETTLE_MS    1000UL         // jeda setelah link BARU naik
```

**Terverifikasi di hardware (log serial):** cold boot dengan kabel → lease
dalam beberapa detik; boot tanpa kabel → `[NET] fallback static` setelah
15 detik; kabel dicolok sesudahnya → `[NET] DHCP ok` dan `mode=dhcp/bound`
tanpa restart; kabel dicabut/dicolok saat sudah bound → lease tetap;
dicolok ke jaringan tanpa DHCP → tetap di IP statis.

**Penting:** alamat statis default mengikuti jaringan yang terlihat di log
hardware (192.168.8.x). Kalau jaringanmu berbeda, ganti — IP statis di
subnet yang salah tidak bisa menjangkau apa pun. Pilih juga alamat di luar
rentang DHCP router, kalau tidak bisa bentrok dengan perangkat lain. Belum
ada pengecekan konflik IP (ARP probe).

**Baris status:** `[status] link=..., ip=..., mode=dhcp|static-fb/<state>`.
`dhcp/bound` = lease dari DHCP; `static-fb/...` = fallback statis aktif.

**Kalau masalah tetap ada padahal link bagus,** periksa catu daya: ENC28J60
menarik sekitar 160 mA dan rail 3,3V yang lemah bisa membuat chip gagal
inisialisasi (serial menampilkan `ENC28J60 TIDAK terdeteksi`, dan watchdog
me-reboot MCU). Kapasitor 10–100 µF dekat modul adalah hal murah yang layak
dicoba.

## IP tetap valid saat link fisik putus-nyambung

Terverifikasi dari log hardware asli: link ENC28J60 memang sempat
berkedip (`link=down` muncul sebentar di antara baris `link=up`, kemungkinan
besar karena catu daya 3,3V yang lemah, bukan bug firmware) — dan `ip=OK`
**tidak pernah turun** selama itu. Ini perilaku yang benar: lease DHCP
berbasis waktu di sisi server, bukan berbasis status link, jadi kedipan link
sesaat tidak membatalkan IP.

Ada satu celah, yang sekarang sudah ditutup: kalau link mati **cukup lama**
(bukan sekadar kedipan sesaat — ambang default `NET_LINK_DOWN_RENEW_MS` =
3000 ms di `net_config.h`), kabel mungkin sudah dipindah ke jaringan/switch
lain, atau IP kita sudah diberikan server ke perangkat lain selagi kita
"pergi". Supaya aman, begitu link pulih setelah mati selama itu, firmware
otomatis memanggil `dhcp_force_renew()` — meminta server mengonfirmasi
lease lagi (DHCPREQUEST, bukan DISCOVER dari nol), tanpa membuang IP yang
sedang dipakai kecuali server benar-benar menolak (NAK). Kalau ternyata IP
berubah setelah ini, baris `NET:` di serial dicetak lagi otomatis (tidak
hanya sekali saat boot).

Kedipan link yang lebih pendek dari ambang itu (kasus catu daya di atas)
sengaja TIDAK boleh memicu apa pun — untuk menghindari membanjiri server
dengan DHCPREQUEST setiap kali link berkedip sesaat.

## Penguatan protokol (verifikasi checksum RX + MSS)

Ditemukan lewat code review (bukan dari gejala di hardware) — stack v1 hanya
**menghitung** checksum saat mengirim, tapi tidak pernah **memverifikasi**
checksum paket yang **masuk**. Ini sudah diperbaiki:

1. **Verifikasi checksum RX** — header IP, ICMP, UDP (kecuali pengirim
   sengaja mengirim `0x0000`, artinya checksum tidak dipakai, sesuai
   RFC 768), dan TCP sekarang divalidasi begitu paket masuk. Paket yang
   checksum-nya tidak cocok (mis. rusak karena noise di kabel) dibuang
   tanpa suara dan tidak pernah diproses atau dijawab.
2. **Penjaga panjang paket vs. klaim header** — `ip_input()` sekarang
   membandingkan byte yang BENAR-BENAR diterima dengan panjang yang
   diklaim header IP. Frame yang terpotong (lebih besar dari
   `NET_BUF_SIZE`, lihat di atas) sekarang dibuang, bukan diproses
   sebagian (yang sebelumnya berisiko membaca sisa data paket lama yang
   masih menempel di buffer).
3. **TCP sekarang mengiklankan MSS** (Maximum Segment Size) di opsi
   SYN/SYN-ACK, otomatis mengikuti `NET_BUF_SIZE`. Ini menutup poin #2 dari
   sisi pengirim: peer yang patuh RFC tidak akan pernah mengirim segmen lebih
   besar dari yang sanggup kita tampung, jadi kasus "terpotong" di atas
   idealnya tidak pernah terjadi dengan klien normal — penjaga di poin 2
   tetap ada sebagai pengaman untuk klien yang mengabaikan MSS.

Poin lain yang dipertimbangkan tapi TIDAK diubah, beserta alasannya:

- **Cache ARP tujuan 1 slot** — kalau beberapa host dengan IP berbeda
  "berbicara" ke perangkat hampir bersamaan, cache tertimpa oleh masing-masing
  (jeda ARP ulang yang kecil, bukan kehilangan data — `eth_arp_learn()`
  tetap mengisinya lagi dari frame yang diterima). Ini keputusan desain RAM
  yang disengaja, bukan bug — dibiarkan seperti semula.
- **Flag broadcast DHCP saat renewal** — dicek ulang, dan ternyata tidak
  berpengaruh (perangkat sudah punya IP valid, jadi balasan server bisa kita
  terima baik broadcast maupun unicast). Tidak diubah.
- **Retransmisi data TCP** — BELUM diimplementasikan. Ini bukan perbaikan
  kecil: butuh salinan data yang di-buffer per sesi TCP untuk pengulangan
  kirim (kira-kira `NET_MAX_TCP_SESSIONS` × MSS byte RAM tambahan, 400-700+
  byte tergantung skemanya), yang di ATmega328 2 KB ini adalah keputusan
  trade-off yang harus dibahas dulu, bukan ditambahkan diam-diam.

## Yang masih perlu diverifikasi (belum dilakukan)

- **Tes ulang hardware setelah refactor bus SPI bersama dan UART**: build
  dengan `pio run` (perhatikan error linker `undefined reference to
  spi_bus_init` — itu menunjuk ke urutan link library), lalu cek deteksi
  ENC28J60 (`[boot] ENC28J60 rev ... terdeteksi`), DHCP, ping, UDP echo
  (port 5000) dan TCP echo (port 8080), serta log serial masih muncul pada
  38400 baud (error linker yang menyebut `uart_*` juga menunjuk ke urutan
  link library). Kalau ada yang tidak beres, coba
  `-DSPI_BUS_DISABLE_IRQ=1` dulu untuk menyingkirkan dugaan bahwa penghapusan
  `cli()`/`sei()` penyebabnya.
- Ukur ulang RAM/Flash dengan `pio run` (angka di bagian atas dibuat
  sebelum refactor).
- Tes renewal lease DHCP jangka panjang (normal, bukan dari pemulihan link)
  belum dicoba - baru tes singkat.
- `dhcp_force_renew()` saat link pulih (lihat bagian "IP tetap valid saat
  link fisik putus-nyambung" di atas) belum diuji pada skenario kabel
  benar-benar dicabut lalu dicolok ke jaringan lain - baru terverifikasi
  dengan membaca kode dan mencocokkannya dengan log link-flap yang ada.
- Ubah `MY_MAC` di `src/main.c` kalau kamu membuat lebih dari satu
  perangkat (hindari MAC yang sama di satu jaringan).

## Pengkabelan ENC28J60 (default `net_config.h`)

| Pin modul ENC28J60 | Sambung ke ATmega328 (gaya Arduino Uno) | Catatan |
|---|---|---|
| VCC | **3.3V** | Chip ENC28J60 itu 3,3V, bukan 5V - cek datasheet modulmu kalau ada regulator onboard |
| GND | GND | Ground bersama wajib |
| SCK | D13 (PB5) | Pin SPI hardware, tidak bisa dipindah |
| SI / MOSI | D11 (PB3) | Pin SPI hardware, tidak bisa dipindah |
| SO / MISO | D12 (PB4) | Pin SPI hardware, tidak bisa dipindah |
| CS / SS | D10 (PB2) | Ditentukan software, bisa diganti (lihat di bawah) |
| INT | tidak disambung | Driver ini polling, bukan berbasis interrupt |
| RESET (kalau ada) | tidak disambung | Reset dilakukan lewat software (`OP_SOFT_RESET`) |

Ganti pin CS lewat 3 baris ini di
`lib/enc28j60_net/include/net_config.h`:
```c
#define ENC_CS_DDR      DDRB
#define ENC_CS_PORT     PORTB
#define ENC_CS_BIT      PB2   // <- ganti sesuai pin baru
```

Perangkat SPI lain (SD card, LCD, ...) berbagi SCK, MOSI, dan MISO dengan
ENC28J60 dan hanya butuh pin CS sendiri — lihat
[Bus SPI bersama](#bus-spi-bersama-libspi_bus). Biarkan PB2 tetap output
walau tidak dipakai sebagai CS: kalau pin SS hardware menjadi input dan
terbaca LOW, modul SPI diam-diam pindah ke mode slave. SD card adalah
perangkat 3,3V; pakai modul dengan level shifter kalau ATmega berjalan
di 5V.