// tcp.h — TCP state machine sederhana, client DAN server, multi-session.
//
// BATASAN v1 (sengaja, dicatat di PRD.md juga):
// - Tidak ada retransmission - paket yang gak sampai/gak di-ACK ya hilang.
// - Tidak ada reordering buffer - paket out-of-order langsung dibuang.
// - Satu tcp_send() = satu paket (data gak dipecah jadi beberapa segmen).
// - Begitu peer kirim FIN, kita langsung balas FIN juga (bukan tahan di
//   CLOSE_WAIT menunggu aplikasi selesai) - closing sepihak yang simpel.
// Ini cukup buat request/response kecil (mis. kirim status, command
// pendek) tapi BUKAN pengganti TCP penuh buat transfer data besar.

#ifndef TCP_H
#define TCP_H

#include <stdint.h>

typedef enum {
    TCP_EVENT_CONNECTED, // handshake selesai (client maupun server), siap tcp_send()
    TCP_EVENT_DATA,      // data masuk - data/len terisi
    TCP_EVENT_CLOSED      // sesi ditutup (oleh peer atau timeout) - data/len jangan dipakai
} tcp_event_t;

// data menunjuk langsung ke net_buf - jangan disimpan pointer-nya untuk
// dipakai belakangan (isinya berubah begitu paket berikutnya diterima).
typedef void (*tcp_callback_t)(uint8_t session_id, tcp_event_t event,
                               const uint8_t *data, uint16_t len);

void tcp_init(void);

// Panggil tiap iterasi loop utama - mengecek timeout handshake/idle.
void tcp_poll(void);

// Server: buka port buat nerima koneksi. Return 1 kalau berhasil, 0
// kalau slot listener penuh (NET_MAX_TCP_LISTENERS di net_config.h).
uint8_t tcp_listen(uint16_t port, tcp_callback_t callback);

// Client: mulai koneksi baru (SYN dikirim di background oleh tcp_poll,
// nunggu ARP resolve dulu kalau perlu). Return session_id (0..N-1) kalau
// ada slot, atau 0xFF kalau NET_MAX_TCP_SESSIONS penuh.
uint8_t tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port,
                    tcp_callback_t callback);

// Kirim data di sesi yang statusnya sudah TCP_EVENT_CONNECTED.
// Return 1 kalau terkirim (bukan berarti sudah di-ACK peer - lihat
// batasan "tidak ada retransmission" di atas).
uint8_t tcp_send(uint8_t session_id, const uint8_t *data, uint16_t len);

// Tutup sesi (kirim FIN, bebaskan slot).
void tcp_close(uint8_t session_id);

// Ambil IP+port remote dari 1 sesi aktif - dipakai buat logging/debug di
// callback (tcp_callback_t cuma dikasih session_id, gak ada info alamat).
// out_remote_ip/out_remote_port boleh NULL kalau salah satu gak dibutuhkan.
// Return 1 kalau session_id valid & aktif, 0 kalau tidak (isi output gak
// diubah kalau return 0).
uint8_t tcp_get_session_info(uint8_t session_id, uint8_t out_remote_ip[4],
                             uint16_t *out_remote_port);

// Dipanggil dispatcher utama setelah ip_input() return IP_PROTO_TCP.
void tcp_input(uint16_t len);

#endif // TCP_H