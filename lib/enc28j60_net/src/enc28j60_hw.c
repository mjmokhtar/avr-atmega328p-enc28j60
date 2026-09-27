// enc28j60_hw.c — implementasi driver ENC28J60, pure AVR-C.
//
// Ditulis ulang dari nol, desain register/urutan init/errata workaround
// mengacu ke driver EtherCard (Pascal Stang / Guido Socher / jcw, GPL v2)
// sebagai referensi belajar — bukan hasil copy-paste. Nama register di
// bawah (ERDPT, ECON1, MACON3, dst) adalah nama resmi dari datasheet
// Microchip ENC28J60, bukan milik library manapun.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include "enc28j60_hw.h"

uint8_t net_buf[NET_BUF_SIZE];

// ---------------------------------------------------------------------
// Batas buffer internal chip (8KB RAM di dalam ENC28J60 sendiri — beda
// dari net_buf di atas yang RAM host AVR). RXSTART harus 0 (errata B4/5).
// ---------------------------------------------------------------------
#define RXSTART_INIT    0x0000
#define RXSTOP_INIT     0x0BFF
#define TXSTART_INIT    0x0C00
#define TXSTOP_INIT     0x11FF

// ---------------------------------------------------------------------
// Alamat register kontrol (gabungan: bit 0-4 alamat, bit 5-6 bank, bit 7
// penanda MAC/PHY yang butuh dummy-byte ekstra saat dibaca)
// ---------------------------------------------------------------------
#define ADDR_MASK   0x1F
#define BANK_MASK   0x60

#define EIE     0x1B
#define EIR     0x1C
#define ESTAT   0x1D
#define ECON2   0x1E
#define ECON1   0x1F

#define ERDPT   (0x00|0x00)
#define EWRPT   (0x02|0x00)
#define ETXST   (0x04|0x00)
#define ETXND   (0x06|0x00)
#define ERXST   (0x08|0x00)
#define ERXND   (0x0A|0x00)
#define ERXRDPT (0x0C|0x00)

#define ERXFCON (0x18|0x20)
#define EPKTCNT (0x19|0x20)
#define EPMM0   (0x08|0x20)
#define EPMCS   (0x10|0x20)

#define MACON1  (0x00|0x40|0x80)
#define MACON3  (0x02|0x40|0x80)
#define MABBIPG (0x04|0x40|0x80)
#define MAIPG   (0x06|0x40|0x80)
#define MAMXFL  (0x0A|0x40|0x80)
#define MICMD   (0x12|0x40|0x80)
#define MIREGADR (0x14|0x40|0x80)
#define MIWR    (0x16|0x40|0x80)
#define MIRD    (0x18|0x40|0x80)

#define MAADR1  (0x00|0x60|0x80)
#define MAADR0  (0x01|0x60|0x80)
#define MAADR3  (0x02|0x60|0x80)
#define MAADR2  (0x03|0x60|0x80)
#define MAADR5  (0x04|0x60|0x80)
#define MAADR4  (0x05|0x60|0x80)
#define MISTAT  (0x0A|0x60|0x80)
#define EREVID  (0x12|0x60)

// Bit-bit register
#define ERXFCON_UCEN  0x80
#define ERXFCON_CRCEN 0x20
#define ERXFCON_PMEN  0x10
#define ERXFCON_MCEN  0x02
#define ERXFCON_BCEN  0x01

#define EIE_INTIE   0x80
#define EIE_PKTIE   0x40

#define EIR_TXIF    0x08
#define EIR_TXERIF  0x02

#define ESTAT_CLKRDY 0x01
#define ESTAT_RXBUSY 0x04

#define ECON2_PKTDEC 0x40
#define ECON2_PWRSV  0x20
#define ECON2_VRPS   0x08

#define ECON1_TXRST  0x80
#define ECON1_TXRTS  0x08
#define ECON1_RXEN   0x04
#define ECON1_BSEL1  0x02
#define ECON1_BSEL0  0x01

#define MACON1_MARXEN 0x01
#define MACON3_PADCFG0 0x20
#define MACON3_TXCRCEN 0x10
#define MACON3_FRMLNEN 0x02

#define MICMD_MIIRD  0x01
#define MISTAT_BUSY  0x01

// PHY registers
#define PHCON2  0x10
#define PHSTAT2 0x11
#define PHLCON  0x14
#define PHCON2_HDLDIS 0x0100

// SPI opcode
#define OP_READ_CTRL_REG   0x00
#define OP_READ_BUF_MEM    0x3A
#define OP_WRITE_CTRL_REG  0x40
#define OP_WRITE_BUF_MEM   0x7A
#define OP_BIT_FIELD_SET   0x80
#define OP_BIT_FIELD_CLR   0xA0
#define OP_SOFT_RESET      0xFF

#define MAX_FRAMELEN  1500

static uint8_t s_bank;

// -----------------------------------------------------------------
// SPI dasar (register native AVR, bukan pinMode/digitalWrite Arduino)
// -----------------------------------------------------------------
static void spi_init(void) {
    // MOSI=PB3, SCK=PB5 output; MISO=PB4 input (fixed silicon ATmega328).
    DDRB |= (1 << PB3) | (1 << PB5);
    DDRB &= (uint8_t) ~(1 << PB4);

    // Pin CS ke ENC28J60 (dikonfigurasi lewat net_config.h).
    ENC_CS_DDR |= (1 << ENC_CS_BIT);
    ENC_CS_PORT |= (1 << ENC_CS_BIT); // idle = HIGH (deselect)

    // SPI master, mode 0, clock fosc/2 (SPI2X=1, SPR1:0=0) -> 8MHz @ F_CPU 16MHz.
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR |= (1 << SPI2X);
}

static inline void chip_select(void) {
    // errata note (diwarisi dari EtherCard): matikan interrupt selama CS
    // aktif, supaya transaksi SPI gak pernah "digigit" ISR millis di
    // tengah jalan lalu bikin timing SetBank/EIR jadi gak konsisten.
    cli();
    ENC_CS_PORT &= (uint8_t) ~(1 << ENC_CS_BIT);
}

static inline void chip_deselect(void) {
    ENC_CS_PORT |= (1 << ENC_CS_BIT);
    sei();
}

static inline uint8_t spi_xfer(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)))
        ;
    return SPDR;
}

static uint8_t read_op(uint8_t op, uint8_t address) {
    chip_select();
    spi_xfer(op | (address & ADDR_MASK));
    uint8_t result = spi_xfer(0x00);
    if (address & 0x80)          // register MAC/PHY butuh 1 dummy byte ekstra
        result = spi_xfer(0x00);
    chip_deselect();
    return result;
}

static void write_op(uint8_t op, uint8_t address, uint8_t data) {
    chip_select();
    spi_xfer(op | (address & ADDR_MASK));
    spi_xfer(data);
    chip_deselect();
}

static void read_buf(uint16_t len, uint8_t *data) {
    chip_select();
    if (len != 0) {
        spi_xfer(OP_READ_BUF_MEM);
        while (len--)
            *data++ = spi_xfer(0x00);
    }
    chip_deselect();
}

static void write_buf(uint16_t len, const uint8_t *data) {
    chip_select();
    if (len != 0) {
        spi_xfer(OP_WRITE_BUF_MEM);
        while (len--)
            spi_xfer(*data++);
    }
    chip_deselect();
}

static void set_bank(uint8_t address) {
    if ((address & BANK_MASK) != s_bank) {
        write_op(OP_BIT_FIELD_CLR, ECON1, ECON1_BSEL1 | ECON1_BSEL0);
        s_bank = address & BANK_MASK;
        write_op(OP_BIT_FIELD_SET, ECON1, s_bank >> 5);
    }
}

static uint8_t reg_read8(uint8_t address) {
    set_bank(address);
    return read_op(OP_READ_CTRL_REG, address);
}

// Belum dipakai di v1 ini (semua reg 16-bit yang kita baca cukup lewat
// reg_read8 dua kali secara manual) - disediakan kalau nanti perlu.
// Kalau compiler protes "defined but not used", itu memang normal.
static uint16_t reg_read16(uint8_t address) __attribute__((unused));
static uint16_t reg_read16(uint8_t address) {
    return reg_read8(address) + ((uint16_t) reg_read8(address + 1) << 8);
}

static void reg_write8(uint8_t address, uint8_t data) {
    set_bank(address);
    write_op(OP_WRITE_CTRL_REG, address, data);
}

static void reg_write16(uint8_t address, uint16_t data) {
    reg_write8(address, (uint8_t) data);
    reg_write8(address + 1, (uint8_t) (data >> 8));
}

static uint16_t phy_read(uint8_t address) {
    reg_write8(MIREGADR, address);
    reg_write8(MICMD, MICMD_MIIRD);
    while (reg_read8(MISTAT) & MISTAT_BUSY)
        ;
    reg_write8(MICMD, 0x00);
    return reg_read8(MIRD + 1);
}

static void phy_write(uint8_t address, uint16_t data) {
    reg_write8(MIREGADR, address);
    reg_write16(MIWR, data);
    while (reg_read8(MISTAT) & MISTAT_BUSY)
        ;
}

// -----------------------------------------------------------------
// API publik
// -----------------------------------------------------------------
uint8_t enc_init(const uint8_t mac[6]) {
    spi_init();
    ENC_CS_PORT |= (1 << ENC_CS_BIT);

    write_op(OP_SOFT_RESET, 0, OP_SOFT_RESET);
    _delay_ms(2); // errata B7/2: chip butuh waktu settle setelah reset
    while (!(read_op(OP_READ_CTRL_REG, ESTAT) & ESTAT_CLKRDY))
        ;

    reg_write16(ERXST, RXSTART_INIT);
    reg_write16(ERXRDPT, RXSTART_INIT);
    reg_write16(ERXND, RXSTOP_INIT);
    reg_write16(ETXST, TXSTART_INIT);
    reg_write16(ETXND, TXSTOP_INIT);

    // LED: LEDA=link, LEDB=activity, stretch pulse (biar kelihatan kedip).
    phy_write(PHLCON, 0x476);

    reg_write8(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_PMEN | ERXFCON_BCEN);
    reg_write16(EPMM0, 0x303f);
    reg_write16(EPMCS, 0xf7f9);
    reg_write8(MACON1, MACON1_MARXEN);
    write_op(OP_BIT_FIELD_SET, MACON3,
             MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN);
    reg_write16(MAIPG, 0x0C12);
    reg_write8(MABBIPG, 0x12);
    reg_write16(MAMXFL, MAX_FRAMELEN);

    reg_write8(MAADR5, mac[0]);
    reg_write8(MAADR4, mac[1]);
    reg_write8(MAADR3, mac[2]);
    reg_write8(MAADR2, mac[3]);
    reg_write8(MAADR1, mac[4]);
    reg_write8(MAADR0, mac[5]);

    phy_write(PHCON2, PHCON2_HDLDIS);
    set_bank(ECON1);
    write_op(OP_BIT_FIELD_SET, EIE, EIE_INTIE | EIE_PKTIE);
    write_op(OP_BIT_FIELD_SET, ECON1, ECON1_RXEN);

    uint8_t rev = reg_read8(EREVID);
    // Microchip lupa naikkan angka revisi silikon B7 (bug dokumentasi
    // mereka sendiri) — kompensasi ini diwarisi dari driver EtherCard.
    if (rev > 5) ++rev;
    return rev;
}

uint8_t enc_link_up(void) {
    return (uint8_t) ((phy_read(PHSTAT2) >> 2) & 1);
}

void enc_packet_send(uint16_t len) {
    // Errata Issue 12: SELALU reset transmit logic sebelum tiap kirim,
    // jangan cuma cek TXERIF dulu — versi lama stack Microchip sendiri
    // berubah jadi begini di revisi lebih baru.
    write_op(OP_BIT_FIELD_SET, ECON1, ECON1_TXRST);
    write_op(OP_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
    write_op(OP_BIT_FIELD_CLR, EIR, EIR_TXERIF | EIR_TXIF);

    reg_write16(EWRPT, TXSTART_INIT);
    reg_write16(ETXND, TXSTART_INIT + len);
    write_op(OP_WRITE_BUF_MEM, 0, 0x00); // per-packet control byte (default)
    write_buf(len, net_buf);

    write_op(OP_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

    // Errata Issue 13: jangan cuma tunggu TXIF, ada kasus hang di
    // silikon — batasi hitungan iterasi supaya gak macet selamanya.
    uint16_t count = 0;
    while ((reg_read8(EIR) & (EIR_TXIF | EIR_TXERIF)) == 0 && ++count < 1000U)
        ;

    if (reg_read8(EIR) & EIR_TXERIF) {
        // Ada error transmisi. V1 ini gak retry late-collision (beda
        // dari EtherCard yang punya opsi retry) — cukup batalkan TXRTS
        // supaya chip gak nyangkut nunggu transmisi yang gak akan lanjut.
        write_op(OP_BIT_FIELD_CLR, ECON1, ECON1_TXRTS);
    }
}

uint16_t enc_packet_receive(void) {
    static uint16_t next_packet_ptr = RXSTART_INIT;
    static uint8_t  unreleased = 0;
    uint16_t len = 0;

    if (unreleased) {
        if (next_packet_ptr == 0)
            reg_write16(ERXRDPT, RXSTOP_INIT);
        else
            reg_write16(ERXRDPT, next_packet_ptr - 1);
        unreleased = 0;
    }

    if (reg_read8(EPKTCNT) > 0) {
        reg_write16(ERDPT, next_packet_ptr);

        uint8_t header[6]; // nextPacket(2) + byteCount(2) + status(2)
        read_buf(sizeof header, header);

        next_packet_ptr = (uint16_t) header[0] | ((uint16_t) header[1] << 8);
        uint16_t byte_count = (uint16_t) header[2] | ((uint16_t) header[3] << 8);
        uint16_t status     = (uint16_t) header[4] | ((uint16_t) header[5] << 8);

        len = byte_count - 4; // buang 4 byte CRC di akhir frame
        if (len > NET_BUF_SIZE - 1)
            len = NET_BUF_SIZE - 1;

        if ((status & 0x80) == 0) // bit "received OK" chip tidak set -> buang
            len = 0;
        else
            read_buf(len, net_buf);

        net_buf[len] = 0;
        unreleased = 1;

        write_op(OP_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
    }
    return len;
}

// Flag persisten: kalau broadcast pernah di-enable PERMANEN (temporary=0),
// disable-sementara oleh DHCP (temporary=1) TIDAK boleh benar-benar
// mematikan penerimaan broadcast di chip — itu logic yang sama seperti
// EtherCard punya, sengaja dipertahankan supaya DHCP gak nyenggol setting
// permanen milik program utama.
static uint8_t s_broadcast_permanent = 0;

void enc_enable_broadcast(uint8_t temporary) {
    reg_write8(ERXFCON, reg_read8(ERXFCON) | ERXFCON_BCEN);
    if (!temporary)
        s_broadcast_permanent = 1;
}

void enc_disable_broadcast(uint8_t temporary) {
    if (!temporary)
        s_broadcast_permanent = 0;
    if (!s_broadcast_permanent)
        reg_write8(ERXFCON, reg_read8(ERXFCON) & (uint8_t) ~ERXFCON_BCEN);
}

void enc_enable_multicast(void) {
    reg_write8(ERXFCON, reg_read8(ERXFCON) | ERXFCON_MCEN);
}

void enc_disable_multicast(void) {
    reg_write8(ERXFCON, reg_read8(ERXFCON) & (uint8_t) ~ERXFCON_MCEN);
}

// Pola flag persisten yang sama seperti broadcast di atas.
static uint8_t s_promiscuous_permanent = 0;

void enc_enable_promiscuous(uint8_t temporary) {
    reg_write8(ERXFCON, reg_read8(ERXFCON) & ERXFCON_CRCEN);
    if (!temporary)
        s_promiscuous_permanent = 1;
}

void enc_disable_promiscuous(uint8_t temporary) {
    if (!temporary)
        s_promiscuous_permanent = 0;
    if (!s_promiscuous_permanent)
        reg_write8(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_PMEN | ERXFCON_BCEN);
}

void enc_power_down(void) {
    write_op(OP_BIT_FIELD_CLR, ECON1, ECON1_RXEN);
    while (reg_read8(ESTAT) & ESTAT_RXBUSY)
        ;
    while (reg_read8(ECON1) & ECON1_TXRTS)
        ;
    write_op(OP_BIT_FIELD_SET, ECON2, ECON2_VRPS);
    write_op(OP_BIT_FIELD_SET, ECON2, ECON2_PWRSV);
}

void enc_power_up(void) {
    write_op(OP_BIT_FIELD_CLR, ECON2, ECON2_PWRSV);
    while (!(reg_read8(ESTAT) & ESTAT_CLKRDY))
        ;
    write_op(OP_BIT_FIELD_SET, ECON1, ECON1_RXEN);
}