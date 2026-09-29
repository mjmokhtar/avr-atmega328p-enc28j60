// spi_bus.h — satu bus SPI hardware ATmega328P dipakai BERSAMA banyak
// perangkat (ENC28J60, SD card, LCD SPI, dst), pure AVR-C.
//
// Ide utamanya: CS saja TIDAK cukup. Tiap perangkat punya mode SPI dan
// clock sendiri (ENC28J60: mode 0 fosc/2; SD: <=400kHz saat init; LCD/display
// bisa mode lain). Karena itu setiap perangkat dideskripsikan oleh satu
// spi_dev_t, dan spi_begin() memasang mode+clock perangkat itu TEPAT
// sebelum CS-nya diturunkan. Driver perangkat tidak pernah menyentuh
// SPCR/SPSR sendiri.
//
// ATURAN (wajib, ini yang menggantikan cli()/sei() lama di driver ENC):
//   1. Satu transaksi = spi_begin() ... spi_end(), selesai sebelum
//      perangkat lain dipakai. Tidak boleh bersarang.
//   2. TIDAK ADA ISR yang boleh memakai SPI. ISR cukup set flag volatile,
//      lalu loop utama yang mengurus perangkatnya.
//   3. Semua pin CS perangkat harus sudah output HIGH (spi_dev_init())
//      SEBELUM transaksi pertama ke perangkat MANAPUN di bus ini.
//
// Kalau aturan 1 dilanggar, spi_bus_error() mengembalikan angka > 0.

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <avr/io.h>
#include <stdint.h>

// ---------------------------------------------------------------------
// Opsi: matikan interrupt selama transaksi (perilaku lama driver ENC).
// Default 0 = TIDAK dimatikan. Aman karena tidak ada ISR yang memakai
// SPI (aturan 2). Kalau 1: SREG disimpan lalu dipulihkan (aman kalau
// pemanggil sendiri sedang cli), tapi transfer panjang (SD 512 byte pada
// clock lambat) akan bikin millis() kehilangan tick — jangan aktifkan
// kecuali sedang membandingkan perilaku.
// ---------------------------------------------------------------------
#ifndef SPI_BUS_DISABLE_IRQ
#define SPI_BUS_DISABLE_IRQ 0
#endif

// ---------------------------------------------------------------------
// Konfigurasi perangkat: gabungkan satu SPI_MODEx dengan satu SPI_DIVx,
// mis. (SPI_MODE0 | SPI_DIV2). Layout byte cfg:
//   bit 3..0 = CPOL CPHA SPR1 SPR0 (langsung ke SPCR)
//   bit 7    = SPI2X (double speed), masuk ke SPSR
// ---------------------------------------------------------------------
#define SPI_MODE0   0x00
#define SPI_MODE1   (1 << CPHA)
#define SPI_MODE2   (1 << CPOL)
#define SPI_MODE3   ((1 << CPOL) | (1 << CPHA))

#define SPI_2X      0x80
#define SPI_DIV2    (SPI_2X)
#define SPI_DIV4    0x00
#define SPI_DIV8    (SPI_2X | (1 << SPR0))
#define SPI_DIV16   (1 << SPR0)
#define SPI_DIV32   (SPI_2X | (1 << SPR1))
#define SPI_DIV64   (1 << SPR1)
#define SPI_DIV128  ((1 << SPR1) | (1 << SPR0))

// Flag perangkat
// SPI_DEV_RELEASE_MISO: setelah CS dinaikkan, kirim 8 clock dummy (0xFF)
// dengan CS HIGH. Wajib untuk SD card — banyak kartu baru melepas MISO
// (high-Z) setelah clock tambahan ini, kalau tidak MISO bertabrakan
// dengan perangkat berikutnya (mis. ENC28J60).
#define SPI_DEV_RELEASE_MISO  0x01

typedef struct {
    volatile uint8_t *cs_port;   // PORTx pin CS
    volatile uint8_t *cs_ddr;    // DDRx pin CS
    uint8_t cs_mask;             // (1 << bit)
    uint8_t cfg;                 // SPI_MODEx | SPI_DIVx
    uint8_t flags;               // SPI_DEV_*
} spi_dev_t;

// Helper penulisan descriptor. Contoh:
//   static const spi_dev_t enc = SPI_DEV(PORTB, DDRB, PB2, SPI_MODE0 | SPI_DIV2, 0);
#define SPI_DEV(port, ddr, bit, cfg, flags) \
    { &(port), &(ddr), (uint8_t) (1 << (bit)), (uint8_t) (cfg), (uint8_t) (flags) }

// Inisialisasi bus: MOSI/SCK output, MISO input, SS (PB2) output HIGH
// (jangan pernah jadi input -> SPI pindah ke mode slave), SPI master aktif.
// Aman dipanggil berkali-kali (yang kedua dst tidak melakukan apa-apa).
void spi_bus_init(void);

// Jadikan pin CS output HIGH (deselect). Panggil untuk SEMUA perangkat
// sebelum transaksi pertama ke perangkat mana pun.
void spi_dev_init(const spi_dev_t *dev);

// Mulai transaksi: pasang mode+clock perangkat (kalau beda dari yang
// aktif), lalu CS LOW.
void spi_begin(const spi_dev_t *dev);

// Akhiri transaksi: CS HIGH (+ 8 clock dummy kalau SPI_DEV_RELEASE_MISO).
void spi_end(const spi_dev_t *dev);

// Jumlah pelanggaran aturan 1 (spi_begin bersarang) sejak panggilan
// terakhir; nilai dibaca lalu di-reset ke 0.
uint8_t spi_bus_error(void);

// Tukar 1 byte. Inline supaya sama cepat dengan versi lama di driver ENC.
static inline uint8_t spi_xfer(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)))
        ;
    return SPDR;
}

// Baca len byte ke buf; setiap byte dibaca dengan mengirim `fill`
// (ENC28J60 pakai 0x00, SD card pakai 0xFF).
void spi_read(uint8_t *buf, uint16_t len, uint8_t fill);

// Kirim len byte dari buf (byte balasan dibuang).
void spi_write(const uint8_t *buf, uint16_t len);

#endif // SPI_BUS_H