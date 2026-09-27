#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h> 
#include "millis.h"
#include "net_config.h"

// OCR0A = (jumlah tick prescaler-64 dalam 1ms) - 1, mode CTC.
// 16MHz -> 250000 tick/s -> 250 tick/ms -> OCR0A = 249 (pas, tanpa sisa)
// 8MHz  -> 125000 tick/s -> 125 tick/ms -> OCR0A = 124 (pas, tanpa sisa)
// F_CPU lain: cek sisa bagi manual sebelum percaya nilai ini akurat.
#define MILLIS_OCR0A  ((F_CPU / 64UL / 1000UL) - 1)

static volatile uint32_t g_millis = 0;

ISR(TIMER0_COMPA_vect) {
    g_millis++;
}

void millis_init(void) {
    // Mode CTC (WGM01=1), reset counter tiap match OCR0A.
    TCCR0A = (1 << WGM01);
    // Prescaler 64: CS02=0, CS01=1, CS00=1.
    TCCR0B = (1 << CS01) | (1 << CS00);
    OCR0A  = (uint8_t) MILLIS_OCR0A;
    TCNT0  = 0;
    // Interrupt tiap Compare Match A.
    TIMSK0 = (1 << OCIE0A);
    // Catatan: global interrupt (sei()) diaktifkan di main.c, bukan di
    // sini — supaya init lain (ENC28J60, dll) yang butuh interrupt
    // masih mati selesai dulu sebelum interrupt pertama boleh masuk.
}

uint32_t millis_now(void) {
    uint32_t value;
    // Baca 32-bit volatile harus atomik: ISR bisa nambahin g_millis di
    // tengah pembacaan 4 byte-nya kalau interrupt gak dimatikan sesaat.
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        value = g_millis;
    }
    return value;
}