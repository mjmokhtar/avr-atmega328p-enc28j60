# ERD / Data structure — enc28j60_stack

## Entities

This firmware is not a persistent data processor (it is not a database), but
it has a few core data structures that are tied together through one shared
packet buffer (a pattern taken from the design of EtherCard's `net.h`), plus
a small description of the devices sharing the SPI bus:

- **Packet buffer** — one static buffer (`uint8_t net_buf[NET_BUF_SIZE]`,
  400 bytes by default) shared for RX and TX. Every layer reads/writes
  fields through fixed byte offsets (defined in `net_offsets.h`), not
  per-layer structs, to save RAM (no copying between layers).
- **ARP entry** — the gateway MAC address (`gw_mac[6]`) and the MAC address
  of 1 active destination host (`dest_mac[6]`), plus a pending/resolved
  status. Not a full ARP table — only 2 slots (gateway + 1 destination),
  matching the RAM limit.
- **Net config** — `my_ip[4]`, `netmask[4]`, `gw_ip[4]`, `dns_ip[4]` (if
  used), `mac[6]`, and the derived `broadcast_ip[4]` — obtained from DHCP,
  kept in RAM (not EEPROM, because it can change on every boot/lease
  renewal).
- **DHCP state** — state machine (`INIT/SELECTING/REQUESTING/BOUND/
  RENEWING`), `xid` (transaction id), `lease_start`, `lease_time`, the DHCP
  server IP, plus the static-fallback bookkeeping: `s_fallback_active`,
  `s_attempt_started`, `s_attempt_start_ms`, `s_fallback_retry_ms`, plus the
  link-event / resend bookkeeping: `s_link_prev`, `s_link_event`,
  `s_link_up_ms`, `s_last_tx_ms` (about 20 bytes in total). The fallback addresses themselves (`NET_STATIC_*`, 12 bytes)
  live in flash (`PROGMEM`), not in SRAM.
- **TCP session** — up to `NET_MAX_TCP_SESSIONS` (2) active TCP sessions,
  each 28 bytes: `active`, `state` (CLOSED/SYN_SENT/ESTABLISHED/CLOSING
  etc.), `remote_ip[4]`, `remote_port`, `local_port`, `snd_nxt`, `rcv_nxt`,
  two timers (SYN retry, handshake give-up) and a callback pointer.
- **TCP listener** — up to `NET_MAX_TCP_LISTENERS` (1) listened ports, each
  5 bytes: `active`, `port`, callback pointer.
- **UDP listener** — the list of listened ports + a callback handler (slots
  kept small: `NET_MAX_UDP_LISTENERS` = 3, not 8 like EtherCard, because our
  scope is narrower).
- **SPI device descriptor** (`spi_dev_t`, one per device on the shared bus,
  declared `static const` by the device's own driver) — `cs_port` and
  `cs_ddr` pointers, `cs_mask`, `cfg` (SPI mode | clock divider, with the
  double-speed bit), `flags` (e.g. `SPI_DEV_RELEASE_MISO` for SD cards).
  7 bytes on AVR (two 2-byte pointers + 3 bytes). Today only the ENC28J60
  has one (`s_enc_dev`); the SD card and the LCD get one each when their
  drivers are written.
- **SPI bus state** (private to `spi_bus.c`) — `s_inited` (bus initialised),
  `s_cur_cfg` (the mode/clock currently in `SPCR`/`SPSR`, so registers are
  rewritten only when the device changes), `s_owner` (the device inside a
  transaction, used to detect nested transactions), `s_err` (nested
  transaction counter returned by `spi_bus_error()`). 5 bytes in total.
- **UART** — no data structure of its own: `lib/uart` is a stateless sender
  (no buffer, no queue) that writes straight to the transmit register.

## Relationships

```
SPI bus state (s_cur_cfg, s_owner)
   ^
   |  spi_begin()/spi_end()
   |
SPI device descriptor (1 per device) <---- owned by its driver:
   |                                        ENC28J60 (today), SD card, LCD (planned)
   |
ENC28J60 driver ----> Packet buffer (shared)
                          |
                          +-- read/written by: Ethernet/ARP layer, IP/ICMP layer, UDP layer, TCP layer
                          |
ARP entry <---- used by the IP layer to resolve the destination MAC before sending
   |
Net config <---- filled by the DHCP state machine, used by the IP layer (src IP)
   |
DHCP state <---- runs on top of the UDP layer (ports 67/68)
   |
TCP session (N slots) <---- each independent, told apart by port
UDP listener (N slots) <---- each independent, told apart by port

UART (stateless) <---- called by main.c only; no other layer depends on it
```

## RAM budget

| Item | Size | Source |
|---|---|---|
| Whole firmware, all features on, before the SPI/UART refactors | 968 B of 2048 B (47.3%) | measured by `pio run` |
| `net_buf` | 400 B | `NET_BUF_SIZE` |
| TCP sessions | 2 × 28 = 56 B | `tcp_session_t` |
| TCP listener | 1 × 5 = 5 B | `tcp_listener_t` |
| SPI device descriptor for the ENC28J60 | 7 B | `spi_dev_t` |
| SPI bus state | 5 B | `spi_bus.c` |
| Refactor overhead (descriptor + bus state) | ~12 B | estimate, to be re-measured |
| DHCP fallback + link-event state + short serial strings | ~20 B + ~45 B | estimate, to be re-measured |
| Planned: SD sector buffer | 512 B | tight; see the PRD |

The remaining items (DHCP, ARP, net config, UDP listeners, NTP, millis,
string literals, ...) are included in the measured total but not itemised
here. Stack space comes on top and is not part of the static figure.

## Notes
- No flash/EEPROM storage in v1 — all state lives in RAM and is lost on
  reboot (the device always runs DHCP again at boot, per the PRD
  constraints).
- The final number of TCP session and UDP listener slots is set in
  `net_config.h`, sized to the RAM left after the packet buffer and the
  ENC28J60 driver are allocated.
- The data format of the planned SD log records is not defined yet; it will
  be decided together with the SD approach (see the PRD open questions).
- On AVR, `static const` data such as `spi_dev_t` is copied into SRAM at
  startup unless it is marked `PROGMEM`; that is why the descriptor is
  counted above. Plain string literals behave the same way.