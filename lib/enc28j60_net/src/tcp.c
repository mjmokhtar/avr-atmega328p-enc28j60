// tcp.c — implementasi TCP (lihat batasan v1 di tcp.h).

#include <string.h>
#include "tcp.h"
#include "enc28j60_hw.h"
#include "ip_icmp.h"
#include "eth_arp.h"
#include "net_state.h"
#include "net_offsets.h"
#include "net_config.h"
#include "millis.h"

#define TCP_DATA_P            TCP_OPTIONS_P // header selalu 20 byte, tanpa opsi (lihat batasan)
#define TCP_WINDOW            512
#define TCP_HANDSHAKE_RETRY_MS 2000UL
#define TCP_HANDSHAKE_GIVEUP_MS 15000UL

#define ST_CLOSED         0
#define ST_SYN_SENT       1
#define ST_SYN_RECEIVED   2
#define ST_ESTABLISHED    3

typedef struct {
    uint8_t  active;
    uint8_t  state;
    uint8_t  remote_ip[4];
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t timer_ms;      // kapan terakhir SYN (re)dikirim - buat jadwal retry
    uint32_t handshake_start_ms; // kapan handshake MULAI - buat giveup timeout
    tcp_callback_t callback;
} tcp_session_t;

typedef struct {
    uint8_t  active;
    uint16_t port;
    tcp_callback_t callback;
} tcp_listener_t;

static tcp_session_t  s_sessions[NET_MAX_TCP_SESSIONS];
static tcp_listener_t s_listeners[NET_MAX_TCP_LISTENERS];
static uint16_t s_next_local_port = 49152;

static uint16_t alloc_local_port(void) {
    uint16_t p = s_next_local_port++;
    if (s_next_local_port == 0) // wrap 16-bit, hindari port 0
        s_next_local_port = 49152;
    return p;
}

static void write_be32(uint16_t offset, uint32_t value) {
    net_buf[offset]     = (uint8_t) (value >> 24);
    net_buf[offset + 1] = (uint8_t) (value >> 16);
    net_buf[offset + 2] = (uint8_t) (value >> 8);
    net_buf[offset + 3] = (uint8_t) value;
}

static uint32_t read_be32(uint16_t offset) {
    return ((uint32_t) net_buf[offset] << 24) | ((uint32_t) net_buf[offset + 1] << 16) |
           ((uint32_t) net_buf[offset + 2] << 8) | net_buf[offset + 3];
}

// Bangun 1 segmen TCP dan kirim, PAKAI seq/ack sesuai isi s->snd_nxt dan
// s->rcv_nxt SAAT INI (fungsi ini TIDAK memutuskan kapan seq boleh maju -
// itu keputusan caller, supaya aman dipanggil ulang buat retransmit SYN/
// SYN-ACK dengan seq yang identik). Return 1 kalau berhasil dikirim, 0
// kalau MAC tujuan belum resolve ARP (caller boleh panggil lagi nanti,
// gak ada state yang berubah kalau return 0).
static uint8_t tcp_build_and_send(tcp_session_t *s, uint8_t flags,
                                  const uint8_t *data, uint16_t data_len) {
    net_buf[TCP_SRC_PORT_H_P] = (uint8_t) (s->local_port >> 8);
    net_buf[TCP_SRC_PORT_L_P] = (uint8_t) s->local_port;
    net_buf[TCP_DST_PORT_H_P] = (uint8_t) (s->remote_port >> 8);
    net_buf[TCP_DST_PORT_L_P] = (uint8_t) s->remote_port;
    write_be32(TCP_SEQ_H_P, s->snd_nxt);
    write_be32(TCP_ACK_H_P, s->rcv_nxt);
    net_buf[TCP_HEADER_LEN_P] = 0x50; // 5 word (20 byte), tanpa opsi
    net_buf[TCP_FLAGS_P] = flags;
    net_buf[TCP_WIN_SIZE_P]     = (uint8_t) (TCP_WINDOW >> 8);
    net_buf[TCP_WIN_SIZE_P + 1] = (uint8_t) TCP_WINDOW;
    net_buf[TCP_URGENT_P]     = 0;
    net_buf[TCP_URGENT_P + 1] = 0;
    if (data_len > 0)
        memcpy(net_buf + TCP_DATA_P, data, data_len);
    net_buf[TCP_CHECKSUM_H_P]     = 0;
    net_buf[TCP_CHECKSUM_H_P + 1] = 0;

    uint16_t seg_len = (uint16_t) (TCP_HEADER_LEN_PLAIN + data_len);
    ip_prepare_send(s->remote_ip, IP_PROTO_TCP, (uint16_t) (IP_HEADER_LEN + seg_len));
    net_fill_checksum(TCP_CHECKSUM_H_P, IP_SRC_P, (uint16_t) (8 + seg_len),
                      (uint16_t) (IP_PROTO_TCP + seg_len));

    if (!ip_send_finalize((uint16_t) (IP_HEADER_LEN + seg_len)))
        return 0; // MAC belum resolve - state TIDAK berubah, caller boleh retry

    // Data (tcp_send()) itu fire-and-forget satu kali (lihat batasan di
    // tcp.h - gak ada retransmit data), jadi aman maju di sini. SYN/FIN
    // TIDAK dimajukan di sini - caller yang memutuskan kapan pindah state
    // (lihat pemanggil tcp_build_and_send di bawah).
    if (data_len > 0)
        s->snd_nxt += data_len;
    return 1;
}

static tcp_session_t *find_free_session(void) {
    for (uint8_t i = 0; i < NET_MAX_TCP_SESSIONS; i++)
        if (!s_sessions[i].active)
            return &s_sessions[i];
    return 0;
}

static tcp_session_t *find_session(const uint8_t remote_ip[4], uint16_t remote_port,
                                   uint16_t local_port) {
    for (uint8_t i = 0; i < NET_MAX_TCP_SESSIONS; i++) {
        tcp_session_t *s = &s_sessions[i];
        if (s->active && s->remote_port == remote_port && s->local_port == local_port &&
            memcmp(s->remote_ip, remote_ip, 4) == 0)
            return s;
    }
    return 0;
}

static uint8_t session_id_of(tcp_session_t *s) {
    return (uint8_t) (s - s_sessions);
}

static void close_session(tcp_session_t *s, uint8_t notify) {
    if (notify && s->callback)
        s->callback(session_id_of(s), TCP_EVENT_CLOSED, 0, 0);
    s->active = 0;
    s->state = ST_CLOSED;
}

void tcp_init(void) {
    memset(s_sessions, 0, sizeof s_sessions);
    memset(s_listeners, 0, sizeof s_listeners);
}

uint8_t tcp_listen(uint16_t port, tcp_callback_t callback) {
    for (uint8_t i = 0; i < NET_MAX_TCP_LISTENERS; i++) {
        if (!s_listeners[i].active) {
            s_listeners[i].active = 1;
            s_listeners[i].port = port;
            s_listeners[i].callback = callback;
            return 1;
        }
    }
    return 0;
}

uint8_t tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port,
                    tcp_callback_t callback) {
    tcp_session_t *s = find_free_session();
    if (!s)
        return 0xFF;
    memset(s, 0, sizeof *s);
    s->active = 1;
    s->state = ST_SYN_SENT;
    memcpy(s->remote_ip, remote_ip, 4);
    s->remote_port = remote_port;
    s->local_port = alloc_local_port();
    s->snd_nxt = millis_now() ^ 0x12345678UL; // initial seq - lihat catatan xid di dhcp.c, prinsip sama
    s->rcv_nxt = 0;
    s->timer_ms = 0; // 0 = belum pernah kirim SYN, tcp_poll() yang kirim pertama kali
    s->handshake_start_ms = millis_now();
    s->callback = callback;
    return session_id_of(s);
}

uint8_t tcp_send(uint8_t session_id, const uint8_t *data, uint16_t len) {
    if (session_id >= NET_MAX_TCP_SESSIONS)
        return 0;
    tcp_session_t *s = &s_sessions[session_id];
    if (!s->active || s->state != ST_ESTABLISHED)
        return 0;
    uint16_t max_payload = NET_BUF_SIZE - TCP_DATA_P;
    if (len > max_payload)
        len = max_payload;
    return tcp_build_and_send(s, TCP_FLAG_PUSH | TCP_FLAG_ACK, data, len);
}

uint8_t tcp_get_session_info(uint8_t session_id, uint8_t out_remote_ip[4],
                             uint16_t *out_remote_port) {
    if (session_id >= NET_MAX_TCP_SESSIONS)
        return 0;
    tcp_session_t *s = &s_sessions[session_id];
    if (!s->active)
        return 0;
    if (out_remote_ip)
        memcpy(out_remote_ip, s->remote_ip, 4);
    if (out_remote_port)
        *out_remote_port = s->remote_port;
    return 1;
}

void tcp_close(uint8_t session_id) {
    if (session_id >= NET_MAX_TCP_SESSIONS)
        return;
    tcp_session_t *s = &s_sessions[session_id];
    if (!s->active)
        return;
    if (s->state == ST_ESTABLISHED)
        tcp_build_and_send(s, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    close_session(s, 0); // caller yang minta close - gak perlu notify diri sendiri
}

void tcp_poll(void) {
    uint32_t now = millis_now();
    for (uint8_t i = 0; i < NET_MAX_TCP_SESSIONS; i++) {
        tcp_session_t *s = &s_sessions[i];
        if (!s->active)
            continue;

        if (s->state == ST_SYN_SENT || s->state == ST_SYN_RECEIVED) {
            // Giveup dihitung dari AWAL handshake, bukan dari retry
            // terakhir - kalau tidak, retry yang terus mereset timer_ms
            // bikin giveup gak akan pernah kepicu.
            if ((now - s->handshake_start_ms) >= TCP_HANDSHAKE_GIVEUP_MS) {
                close_session(s, 1);
                continue;
            }
        }

        if (s->timer_ms == 0 || (now - s->timer_ms) >= TCP_HANDSHAKE_RETRY_MS) {
            uint8_t sent;
            if (s->state == ST_SYN_SENT)
                sent = tcp_build_and_send(s, TCP_FLAG_SYN, 0, 0);
            else if (s->state == ST_SYN_RECEIVED)
                sent = tcp_build_and_send(s, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
            else
                continue;
            // s->snd_nxt TIDAK diubah di sini (retransmit seq yang sama
            // persis) - baru dimajukan +1 saat handshake benar-benar
            // selesai (lihat tcp_input, transisi ke ST_ESTABLISHED).
            if (sent)
                s->timer_ms = now;
        }
    }
}

static void handle_established_payload(tcp_session_t *s, uint32_t seq,
                                       uint8_t flags, const uint8_t *data,
                                       uint16_t data_len) {
    if (seq != s->rcv_nxt) {
        // Out-of-order/duplikat - dibuang (lihat batasan "no reordering").
        return;
    }
    if (data_len > 0) {
        s->rcv_nxt += data_len;
        if (s->callback)
            s->callback(session_id_of(s), TCP_EVENT_DATA, data, data_len);
        tcp_build_and_send(s, TCP_FLAG_ACK, 0, 0);
    }
    if (flags & TCP_FLAG_FIN) {
        s->rcv_nxt += 1;
        // Balas FIN dengan FIN+ACK sekaligus (closing sepihak, bukan
        // CLOSE_WAIT penuh - lihat batasan di tcp.h).
        tcp_build_and_send(s, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
        close_session(s, 1);
    }
}

void tcp_input(uint16_t len) {
    if (len < ETH_HEADER_LEN + IP_HEADER_LEN + TCP_HEADER_LEN_PLAIN)
        return;

    uint16_t local_port  = ((uint16_t) net_buf[TCP_DST_PORT_H_P] << 8) | net_buf[TCP_DST_PORT_L_P];
    uint16_t remote_port = ((uint16_t) net_buf[TCP_SRC_PORT_H_P] << 8) | net_buf[TCP_SRC_PORT_L_P];
    uint8_t  remote_ip[4];
    memcpy(remote_ip, net_buf + IP_SRC_P, 4);

    uint32_t seq   = read_be32(TCP_SEQ_H_P);
    uint32_t ack   = read_be32(TCP_ACK_H_P);
    uint8_t  flags = net_buf[TCP_FLAGS_P];
    uint8_t  hdr_len = (uint8_t) ((net_buf[TCP_HEADER_LEN_P] >> 4) * 4);

    uint16_t ip_total_len = ((uint16_t) net_buf[IP_TOTLEN_H_P] << 8) | net_buf[IP_TOTLEN_L_P];
    if (ip_total_len < IP_HEADER_LEN + hdr_len)
        return;
    uint16_t data_len = ip_total_len - IP_HEADER_LEN - hdr_len;
    const uint8_t *data = net_buf + ETH_HEADER_LEN + IP_HEADER_LEN + hdr_len;

    tcp_session_t *s = find_session(remote_ip, remote_port, local_port);

    if (!s) {
        if (!(flags & TCP_FLAG_SYN) || (flags & TCP_FLAG_ACK))
            return; // bukan SYN murni (permintaan koneksi baru) - abaikan (lihat batasan: gak kirim RST)

        tcp_listener_t *l = 0;
        for (uint8_t i = 0; i < NET_MAX_TCP_LISTENERS; i++)
            if (s_listeners[i].active && s_listeners[i].port == local_port) {
                l = &s_listeners[i];
                break;
            }
        if (!l)
            return; // gak ada yang listen di port ini

        s = find_free_session();
        if (!s)
            return; // slot penuh - koneksi baru ditolak diam-diam (batasan v1)

        memset(s, 0, sizeof *s);
        s->active = 1;
        s->state = ST_SYN_RECEIVED;
        memcpy(s->remote_ip, remote_ip, 4);
        s->remote_port = remote_port;
        s->local_port = local_port;
        s->rcv_nxt = seq + 1;
        s->snd_nxt = millis_now() ^ 0x87654321UL;
        s->callback = l->callback;
        s->timer_ms = millis_now();
        s->handshake_start_ms = s->timer_ms;
        tcp_build_and_send(s, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
        return;
    }

    if (flags & TCP_FLAG_RST) {
        close_session(s, 1);
        return;
    }

    switch (s->state) {
    case ST_SYN_SENT:
        // ack harus == snd_nxt+1 karena SYN kita "memakai" 1 nomor urut,
        // walau belum kita majukan snd_nxt-nya sendiri (lihat catatan di
        // tcp_build_and_send).
        if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK) && ack == s->snd_nxt + 1) {
            s->snd_nxt += 1;
            s->rcv_nxt = seq + 1;
            s->state = ST_ESTABLISHED;
            tcp_build_and_send(s, TCP_FLAG_ACK, 0, 0);
            if (s->callback)
                s->callback(session_id_of(s), TCP_EVENT_CONNECTED, 0, 0);
        }
        break;

    case ST_SYN_RECEIVED:
        if ((flags & TCP_FLAG_ACK) && ack == s->snd_nxt + 1) {
            s->snd_nxt += 1;
            s->state = ST_ESTABLISHED;
            if (s->callback)
                s->callback(session_id_of(s), TCP_EVENT_CONNECTED, 0, 0);
            if (data_len > 0 || (flags & TCP_FLAG_FIN))
                handle_established_payload(s, seq, flags, data, data_len);
        }
        break;

    case ST_ESTABLISHED:
        handle_established_payload(s, seq, flags, data, data_len);
        break;

    default:
        break;
    }
}