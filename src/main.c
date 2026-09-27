// main.c — demo firmware: init ENC28J60 -> (DHCP atau IP statis) -> loop
// non-blocking (poll paket, UDP echo port 5000, TCP echo port 7, NTP).
//
// WDT dipakai sebagai pengaman kegagalan ASLI (lihat diskusi sebelumnya):
// 1x wdt_reset() per iterasi loop, timeout disetel jauh di atas durasi
// wajar 1 iterasi. TIDAK ada wdt_reset() nyebar di banyak tempat, karena
// loop ini didesain non-blocking dari awal (gak ada delay()/while() lama).
//
// UART debug ditulis manual di sini (bukan bagian lib/enc28j60_net) -
// cuma buat MJ lihat apa yang terjadi lewat Serial Monitor, di luar
// scope stack jaringan itu sendiri.

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdio.h>

#include "net_config.h"
#include "millis.h"
#include "enc28j60_hw.h"
#include "eth_arp.h"
#include "ip_icmp.h"
#include "net_offsets.h"
#include "net_state.h"
#include "udp.h"
#if NET_USE_DHCP
#include "dhcp.h"
#endif
#if NET_ENABLE_NTP
#include "ntp.h"
#endif
#include "tcp.h"

// ---------------------------------------------------------------------
// GANTI MAC address ini untuk tiap device kalau kamu bikin lebih dari 1
// unit - dua device dengan MAC sama di jaringan yang sama akan bikin
// masalah (ARP/switch bingung). Byte pertama 0x02 menandai "locally
// administered" - aman dipakai bebas untuk keperluan internal begini.
// ---------------------------------------------------------------------
static const uint8_t MY_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

// Port demo UDP echo & TCP echo - ganti di sini kalau mau port lain,
// gak perlu cari-cari lagi di dalam main() (dipakai di udp_listen()/
// tcp_listen() di bawah, dan komentar contoh netcat).
#define DEMO_UDP_ECHO_PORT  5000
#define DEMO_TCP_ECHO_PORT  8080

#define UART_BAUD  38400UL
#define UART_UBRR  ((F_CPU / (16UL * UART_BAUD)) - 1)

// Batas panjang isi paket yang di-print ke serial - biar 1 paket gede gak
// nge-spam serial berlebihan (UART 38400 baud lumayan lambat).
#define UART_DATA_PRINT_MAX  48

// -------------------------- UART debug (TX only) ----------------------
static void uart_init(void) {
    UBRR0H = (uint8_t) (UART_UBRR >> 8);
    UBRR0L = (uint8_t) UART_UBRR;
    UCSR0B = (1 << TXEN0);               // aktifkan transmitter saja
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8N1
}

static void uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0)))
        ;
    UDR0 = (uint8_t) c;
}

static void uart_print(const char *s) {
    while (*s)
        uart_putc(*s++);
}

static void uart_print_ip(const uint8_t ip[4]) {
    char buf[17];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    uart_print(buf);
}

static void uart_print_ipport(const uint8_t ip[4], uint16_t port) {
    uart_print_ip(ip);
    char buf[8];
    snprintf(buf, sizeof buf, ":%u", port);
    uart_print(buf);
}

// Print isi data apa adanya (byte mentah, bukan lewat snprintf) - dibatasi
// panjangnya biar 1 paket gede gak nge-spam serial berlebihan. data
// menunjuk ke net_buf, JANGAN dipanggil setelah paket berikutnya diterima
// (sama seperti aturan pointer di udp.h/tcp.h).
static void uart_print_data(const uint8_t *data, uint16_t len) {
    uint16_t n = (len > UART_DATA_PRINT_MAX) ? UART_DATA_PRINT_MAX : len;
    for (uint16_t i = 0; i < n; i++)
        uart_putc((char) data[i]);
    if (len > n)
        uart_print("...(dipotong)");
}

// ---------------------------- Handler UDP echo -------------------------
// Kirim balik apa pun yang diterima, ke pengirim yang sama - buat tes
// cepat pakai netcat: `nc -u <ip-device> <DEMO_UDP_ECHO_PORT>`
//
// Format serial buat dibaca aplikasi lain (mis. ESP32): SATU BARIS per
// pesan, "UDP:" diikuti isi data mentah, diakhiri newline. ESP32 tinggal
// Serial.readStringUntil('\n') lalu buang 4 karakter awal ("UDP:") -
// gak perlu decode panjang, gak perlu potong tanda kutip, gak ada baris
// lain yang bikin bingung. Batasan: isi data JANGAN mengandung byte '\r'
// atau '\n' sendiri (bakal motong baris lebih cepat dari seharusnya) -
// aman buat teks pendek biasa, bukan buat data biner sembarang.
static void on_udp_echo(const uint8_t src_ip[4], uint16_t src_port,
                        uint16_t dst_port, const uint8_t *data, uint16_t len) {
    // Baris info pengirim TERPISAH dari baris data "UDP:..." - sengaja
    // BUKAN "koneksi baru" (UDP gak punya sesi, ini cuma info per-paket,
    // dicetak tiap kali ada data masuk, bukan sekali di awal kayak TCP).
    uart_print("[UDP] dari ");
    uart_print_ipport(src_ip, src_port);
    uart_print("\r\n");

    uart_print("UDP:");
    uart_print_data(data, len);
    uart_print("\r\n");

    udp_send(dst_port, src_ip, src_port, data, len);
}

// ---------------------------- Handler TCP echo -------------------------
// DEMO_TCP_ECHO_PORT default 7 (port "echo" klasik). Tes pakai:
// `nc <ip-device> <DEMO_TCP_ECHO_PORT>`
static void on_tcp_echo(uint8_t session_id, tcp_event_t event,
                        const uint8_t *data, uint16_t len) {
    uint8_t remote_ip[4];
    uint16_t remote_port = 0;
    uint8_t have_info = tcp_get_session_info(session_id, remote_ip, &remote_port);

    switch (event) {
    case TCP_EVENT_CONNECTED:
        uart_print("[TCP] koneksi baru");
        if (have_info) {
            uart_print(" dari ");
            uart_print_ipport(remote_ip, remote_port);
        }
        uart_print("\r\n");
        break;

    case TCP_EVENT_DATA:
        // Format sama seperti UDP di atas: 1 baris, prefix "TCP:", isi
        // data mentah, newline - konsisten & gampang di-parsing sisi
        // aplikasi lain (cukup beda prefix-nya buat bedain sumbernya).
        uart_print("TCP:");
        uart_print_data(data, len);
        uart_print("\r\n");

        tcp_send(session_id, data, len);
        break;

    case TCP_EVENT_CLOSED:
        uart_print("[TCP] sesi ditutup\r\n");
        break;
    }
}

#if NET_ENABLE_NTP
static volatile uint32_t g_last_ntp_time = 0;

static void on_ntp_time(uint32_t unix_time) {
    g_last_ntp_time = unix_time;
    char buf[32];
    snprintf(buf, sizeof buf, "[NTP] waktu diterima: %lu\r\n", (unsigned long) unix_time);
    uart_print(buf);
}
#endif

int main(void) {
    uart_init();
    millis_init();
    sei(); // aktifkan interrupt global (dipakai Timer0 buat millis())

    // WDT sebagai pengaman kegagalan asli - lihat komentar di atas file.
    // 4 detik jauh di atas durasi wajar 1 iterasi loop non-blocking kita.
    wdt_enable(WDTO_4S);

    uart_print("\r\n[boot] ENC28J60 TCP/UDP stack\r\n");

    memcpy(net_my_mac, MY_MAC, 6);

    uint8_t rev = enc_init(net_my_mac);
    if (rev == 0) {
        uart_print("[boot] ENC28J60 TIDAK terdeteksi - cek wiring SPI/CS!\r\n");
        // Sengaja TIDAK wdt_reset() di sini - biarkan WDT gigit dan
        // reboot MCU, lalu coba init lagi dari awal setelah reset.
        for (;;)
            ;
    }
    {
        char buf[40];
        snprintf(buf, sizeof buf, "[boot] ENC28J60 rev %u terdeteksi\r\n", rev);
        uart_print(buf);
    }

    eth_arp_init();
    ip_icmp_init();
    udp_init();
    tcp_init();
#if NET_ENABLE_NTP
    ntp_init(on_ntp_time);
#endif

#if NET_USE_DHCP
    uart_print("[boot] mulai DHCP...\r\n");
    dhcp_start();
#else
    {
        static const uint8_t static_ip[4]  = NET_STATIC_IP;
        static const uint8_t static_nm[4]  = NET_STATIC_NETMASK;
        static const uint8_t static_gw[4]  = NET_STATIC_GATEWAY;
        memcpy(net_my_ip, static_ip, 4);
        memcpy(net_netmask, static_nm, 4);
        memcpy(net_gw_ip, static_gw, 4);
        net_state_update_broadcast();
        uart_print("[boot] IP statis: ");
        uart_print_ip(net_my_ip);
        uart_print("\r\n");
    }
#endif

    udp_listen(DEMO_UDP_ECHO_PORT, on_udp_echo);
    tcp_listen(DEMO_TCP_ECHO_PORT, on_tcp_echo);
    {
        char buf[48];
        snprintf(buf, sizeof buf, "[boot] UDP echo listen di port %u\r\n",
                 (unsigned) DEMO_UDP_ECHO_PORT);
        uart_print(buf);
        snprintf(buf, sizeof buf, "[boot] TCP echo listen di port %u\r\n",
                 (unsigned) DEMO_TCP_ECHO_PORT);
        uart_print(buf);
    }

    uint8_t had_ip = 0;
    uint32_t last_status_ms = millis_now();

    for (;;) {
        wdt_reset(); // SATU kali per iterasi - lihat prinsip di komentar atas

        uint16_t len = enc_packet_receive();
        if (eth_input(len)) { // 1 = frame IP buat kita, lanjut proses
            uint8_t proto = ip_input(len);
            if (proto == IP_PROTO_UDP)
                udp_input(len);
            else if (proto == IP_PROTO_TCP)
                tcp_input(len);
        }

        arp_poll();
#if NET_USE_DHCP
        dhcp_poll();
#endif
        tcp_poll();

        if (!had_ip && net_has_ip()) {
            had_ip = 1;
            // Format 1-baris sama seperti "UDP:"/"TCP:" - prefix "NET:",
            // isinya key=value dipisah ';'. ESP32 tinggal startsWith("NET:")
            // lalu pecah per ';' dan per '=' - gak perlu parsing 4 baris
            // terpisah kayak sebelumnya.
            uart_print("NET:ip=");
            uart_print_ip(net_my_ip);
            uart_print(";mask=");
            uart_print_ip(net_netmask);
            uart_print(";gw=");
            uart_print_ip(net_gw_ip);
            uart_print(";dns=");
            uart_print_ip(net_dns_ip); // 0.0.0.0 kalau server DHCP gak kasih opsi 6
            uart_print("\r\n");
        }

        // Laporan status tiap 5 detik - non-blocking (bandingkan millis,
        // BUKAN delay()), jadi gak mengganggu polling paket di atas.
        uint32_t now = millis_now();
        if (now - last_status_ms >= 5000UL) {
            last_status_ms = now;
            uart_print("[status] link=");
            uart_print(enc_link_up() ? "up" : "down");
            uart_print(had_ip ? ", ip=OK\r\n" : ", ip=belum dapat\r\n");
        }
    }
}