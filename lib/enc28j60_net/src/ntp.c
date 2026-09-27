// ntp.c — implementasi NTP client.

#include <string.h>
#include "ntp.h"
#include "udp.h"
#include "enc28j60_hw.h"
#include "net_offsets.h"

#define NTP_PORT        123
#define NTP_PACKET_LEN  48

// Selisih epoch NTP (1 Jan 1900) ke epoch Unix (1 Jan 1970), dalam detik.
#define NTP_UNIX_EPOCH_DIFF  2208988800UL

static ntp_callback_t s_callback = 0;

static void ntp_on_packet(const uint8_t src_ip[4], uint16_t src_port,
                          uint16_t dst_port, const uint8_t *data, uint16_t len) {
    (void) src_ip; (void) src_port; (void) dst_port;
    if (len < NTP_PACKET_LEN || !s_callback)
        return;

    // "Transmit Timestamp" server (detik sejak epoch NTP), byte 40-43,
    // big-endian. Bagian pecahan detik (byte 44-47) kita gak pakai -
    // cukup presisi detik buat kebutuhan device ini.
    uint32_t ntp_seconds = ((uint32_t) data[40] << 24) | ((uint32_t) data[41] << 16) |
                           ((uint32_t) data[42] << 8) | data[43];

    if (ntp_seconds < NTP_UNIX_EPOCH_DIFF)
        return; // balasan gak wajar (server belum sync / data korup)

    s_callback(ntp_seconds - NTP_UNIX_EPOCH_DIFF);
}

void ntp_init(ntp_callback_t callback) {
    s_callback = callback;
    udp_listen(NTP_PORT, ntp_on_packet);
}

uint8_t ntp_request(const uint8_t server_ip[4]) {
    uint8_t *p = net_buf + UDP_DATA_P;
    memset(p, 0, NTP_PACKET_LEN);
    // Byte 0: LI=0 (no warning), VN=3 (NTPv3, paling luas didukung),
    // Mode=3 (client). 0b00_011_011 = 0x1B.
    p[0] = 0x1B;
    return udp_send_inplace(NTP_PORT, server_ip, NTP_PORT, NTP_PACKET_LEN);
}