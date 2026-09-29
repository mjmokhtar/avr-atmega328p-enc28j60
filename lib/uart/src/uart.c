// uart.c — implementasi output debug UART0 (TX saja). Lihat uart.h.

#include <avr/io.h>
#include <stdio.h>
#include "uart.h"

// F_CPU datang dari definisi board di platformio.ini (board = uno ->
// 16000000UL), sama seperti yang dipakai millis.c.
#ifndef F_CPU
#error "F_CPU belum didefinisikan - uart.c butuh F_CPU untuk hitung baud rate"
#endif

#define UART_UBRR  ((F_CPU / (16UL * UART_BAUD)) - 1)

void uart_init(void) {
    UBRR0H = (uint8_t) (UART_UBRR >> 8);
    UBRR0L = (uint8_t) UART_UBRR;
    UCSR0B = (1 << TXEN0);                  // aktifkan transmitter saja
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8N1
}

void uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0)))
        ;
    UDR0 = (uint8_t) c;
}

void uart_print(const char *s) {
    while (*s)
        uart_putc(*s++);
}

void uart_print_ip(const uint8_t ip[4]) {
    char buf[17];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    uart_print(buf);
}

void uart_print_ipport(const uint8_t ip[4], uint16_t port) {
    uart_print_ip(ip);
    char buf[8];
    snprintf(buf, sizeof buf, ":%u", port);
    uart_print(buf);
}

void uart_print_data(const uint8_t *data, uint16_t len) {
    uint16_t n = (len > UART_DATA_PRINT_MAX) ? UART_DATA_PRINT_MAX : len;
    for (uint16_t i = 0; i < n; i++)
        uart_putc((char) data[i]);
    if (len > n)
        uart_print("...(dipotong)");
}