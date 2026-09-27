// udp.h — UDP send/receive dengan listener per port.
//
// DHCP dan NTP nanti gak dapat perlakuan khusus di sini — mereka cukup
// panggil udp_listen() buat daftar port mereka sendiri (68 dan 123),
// sama seperti aplikasi lain. Ini yang bikin udp.c gak perlu tahu
// apa-apa soal DHCP/NTP.

#ifndef UDP_H
#define UDP_H

#include <stdint.h>

// Dipanggil tiap ada datagram UDP masuk yang cocok port-nya.
// data menunjuk langsung ke net_buf (JANGAN disimpan pointer-nya untuk
// dipakai belakangan — isinya berubah begitu paket berikutnya diterima).
typedef void (*udp_callback_t)(const uint8_t src_ip[4], uint16_t src_port,
                               uint16_t dst_port, const uint8_t *data,
                               uint16_t len);

void udp_init(void);

// Daftar listener di satu port. Return 1 kalau berhasil, 0 kalau slot
// penuh (lihat NET_MAX_UDP_LISTENERS di net_config.h).
uint8_t udp_listen(uint16_t port, udp_callback_t callback);

// Kirim datagram UDP. len dibatasi otomatis ke sisa net_buf yang ada.
// Return 1 kalau terkirim. Return 0 kalau MAC tujuan belum resolve ARP
// (paket ini DIBUANG — caller yang retry di iterasi loop berikutnya,
// bukan udp.c yang mengantre paketnya).
uint8_t udp_send(uint16_t src_port, const uint8_t dst_ip[4], uint16_t dst_port,
                 const uint8_t *data, uint16_t len);

// Sama seperti udp_send(), tapi payload dianggap SUDAH ditulis caller
// langsung ke net_buf+UDP_DATA_P (dipakai DHCP/NTP yang bikin payload-nya
// langsung di buffer, biar gak dobel-copy dan gak butuh buffer RAM
// tambahan). len = panjang payload itu.
uint8_t udp_send_inplace(uint16_t src_port, const uint8_t dst_ip[4],
                         uint16_t dst_port, uint16_t len);

// Dipanggil oleh dispatcher utama (main.c) setelah ip_input() bilang ini
// paket UDP buat kita. len = panjang frame Ethernet penuh yang diterima.
void udp_input(uint16_t len);

#endif // UDP_H