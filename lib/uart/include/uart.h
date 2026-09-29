// uart.h — output debug lewat UART0 (TX saja), pure AVR-C.
//
// Dipisah dari main.c supaya main.c cuma berisi alur aplikasi. Modul ini
// GAK tahu apa-apa soal jaringan/SPI — cuma kirim teks ke pin TX (PD1).
// Format baris "NET:"/"UDP:"/"TCP:" untuk aplikasi lain (mis. ESP32) tetap
// dibentuk di main.c; modul ini cuma alat kirimnya.

#ifndef UART_H
#define UART_H

#include <stdint.h>

// Baud rate. Boleh di-override lewat build_flags (-DUART_BAUD=115200UL).
// Format frame tetap 8N1, transmitter saja (RX tidak diaktifkan).
#ifndef UART_BAUD
#define UART_BAUD  38400UL
#endif

// Batas panjang isi paket yang dicetak uart_print_data() - biar 1 paket
// gede gak nge-spam serial (UART 38400 baud lumayan lambat).
#ifndef UART_DATA_PRINT_MAX
#define UART_DATA_PRINT_MAX  48
#endif

// Aktifkan transmitter UART0. Panggil sekali di awal main().
void uart_init(void);

// Kirim 1 karakter. BLOKING: menunggu register kirim kosong. Di 38400
// baud satu karakter ~260 us, jadi baris panjang menahan loop utama
// beberapa milidetik - cetak seperlunya.
void uart_putc(char c);

// Kirim string C (berakhiran '\0') apa adanya, tanpa newline otomatis.
void uart_print(const char *s);

// Cetak IPv4 dalam format "a.b.c.d".
void uart_print_ip(const uint8_t ip[4]);

// Cetak IPv4 + port dalam format "a.b.c.d:port".
void uart_print_ipport(const uint8_t ip[4], uint16_t port);

// Cetak isi data mentah (byte apa adanya, bukan hex), maksimal
// UART_DATA_PRINT_MAX byte; kalau lebih panjang ditambah "...(dipotong)".
// data biasanya menunjuk ke net_buf - JANGAN dipanggil setelah paket
// berikutnya diterima (sama seperti aturan pointer di udp.h/tcp.h).
void uart_print_data(const uint8_t *data, uint16_t len);

#endif // UART_H