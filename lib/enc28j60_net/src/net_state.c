#include <string.h>
#include "net_state.h"

uint8_t net_my_mac[6]      = {0, 0, 0, 0, 0, 0};
uint8_t net_my_ip[4]       = {0, 0, 0, 0};
uint8_t net_netmask[4]     = {255, 255, 255, 0};
uint8_t net_gw_ip[4]       = {0, 0, 0, 0};
uint8_t net_broadcast_ip[4] = {0, 0, 0, 0};

uint8_t net_has_ip(void) {
    return net_my_ip[0] != 0 || net_my_ip[1] != 0 ||
           net_my_ip[2] != 0 || net_my_ip[3] != 0;
}

void net_state_update_broadcast(void) {
    for (uint8_t i = 0; i < 4; i++)
        net_broadcast_ip[i] = net_my_ip[i] | (uint8_t) ~net_netmask[i];
}

uint8_t net_is_same_subnet(const uint8_t target_ip[4]) {
    if (target_ip[0] == 0 || net_my_ip[0] == 0)
        return 0;
    for (uint8_t i = 0; i < 4; i++)
        if ((target_ip[i] & net_netmask[i]) != (net_my_ip[i] & net_netmask[i]))
            return 0;
    return 1;
}