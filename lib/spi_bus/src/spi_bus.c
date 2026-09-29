// spi_bus.c — implementasi bus SPI bersama. Lihat aturan pemakaian di
// spi_bus.h (transaksi tidak boleh bersarang, tidak ada ISR yang boleh
// memakai SPI, semua CS di-HIGH-kan dulu sebelum transaksi pertama).

#include <avr/io.h>
#include <avr/interrupt.h>
#include "spi_bus.h"

// Nilai yang tidak mungkin jadi cfg valid (bit 6..4 tidak pernah dipakai).
#define CFG_UNKNOWN  0x7F

static uint8_t s_inited = 0;
static uint8_t s_cur_cfg = CFG_UNKNOWN;   // cfg yang sedang terpasang di SPCR/SPSR
static const spi_dev_t *s_owner = 0;      // perangkat yang sedang di dalam transaksi
static uint8_t s_err = 0;

#if SPI_BUS_DISABLE_IRQ
static uint8_t s_sreg;
#endif

void spi_bus_init(void) {
    if (s_inited)
        return;
    s_inited = 1;

    // SS (PB2) HARUS output walau bukan dipakai sebagai CS: kalau jadi
    // input dan terbaca LOW, modul SPI ATmega otomatis pindah ke slave.
    PORTB |= (1 << PB2);
    DDRB  |= (1 << PB2) | (1 << PB3) | (1 << PB5);   // SS, MOSI, SCK
    DDRB  &= (uint8_t) ~(1 << PB4);                  // MISO

    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = 0;
    s_cur_cfg = CFG_UNKNOWN;   // paksa spi_begin() pertama memasang cfg
}

void spi_dev_init(const spi_dev_t *dev) {
    // PORT dulu baru DDR: pin langsung output HIGH, tanpa glitch LOW.
    *dev->cs_port |= dev->cs_mask;
    *dev->cs_ddr  |= dev->cs_mask;
}

void spi_begin(const spi_dev_t *dev) {
#if SPI_BUS_DISABLE_IRQ
    if (!s_owner) {          // jangan timpa SREG asli kalau bersarang
        s_sreg = SREG;
        cli();
    }
#endif
    if (s_owner && s_err != 0xFF)
        s_err++;             // transaksi bersarang = bug pemanggil
    s_owner = dev;

    if (dev->cfg != s_cur_cfg) {
        // Aman: tidak ada transfer berjalan (tiap spi_xfer menunggu SPIF)
        // dan semua CS sedang HIGH, jadi tidak ada perangkat yang
        // mendengar perubahan idle-level SCK.
        SPCR = (1 << SPE) | (1 << MSTR) | (dev->cfg & 0x0F);
        SPSR = (dev->cfg & SPI_2X) ? (1 << SPI2X) : 0;
        s_cur_cfg = dev->cfg;
    }
    *dev->cs_port &= (uint8_t) ~dev->cs_mask;
}

void spi_end(const spi_dev_t *dev) {
    *dev->cs_port |= dev->cs_mask;
    if (dev->flags & SPI_DEV_RELEASE_MISO)
        spi_xfer(0xFF);      // 8 clock dengan CS HIGH: kartu SD melepas MISO
    s_owner = 0;
#if SPI_BUS_DISABLE_IRQ
    SREG = s_sreg;
#endif
}

uint8_t spi_bus_error(void) {
    uint8_t e = s_err;
    s_err = 0;
    return e;
}

void spi_read(uint8_t *buf, uint16_t len, uint8_t fill) {
    while (len--)
        *buf++ = spi_xfer(fill);
}

void spi_write(const uint8_t *buf, uint16_t len) {
    while (len--)
        spi_xfer(*buf++);
}