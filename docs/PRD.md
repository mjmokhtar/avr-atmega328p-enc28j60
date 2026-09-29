# PRD — enc28j60_stack

## Problem / Goal
MJ needs an ATmega328 device that can communicate over Ethernet (TCP and
UDP) through the ENC28J60 chip, without any third-party library (EtherCard,
UIPEthernet, etc.) and without the Arduino framework — pure AVR-C via
PlatformIO. The ENC28J60 is only a MAC+PHY (it has no hardware TCP/IP stack
like the W5500), so the whole ARP/IP/ICMP/UDP/TCP/DHCP stack is written in
the firmware itself.

The device must also be able to grow: the same SPI bus has to be shareable
with other SPI peripherals (an SD card for data logging and a 16x2 LCD are
planned) without touching the network code.

Design reference: the EtherCard library (njh/ethercard) — studied, a few
techniques taken (a single buffer + byte offsets, ENC28J60 errata
workarounds, the DHCP state machine flow), rewritten in plain C, and the
parts outside the scope left out (HTTP client, DNS, WOL, Stash/BufferFiller).

## Scope
**In scope (v1):**
- ENC28J60 driver in pure AVR-C: raw register access over SPI, bank
  switching, MAC/PHY init, packetSend/packetReceive, errata workarounds
  (late collision, TXRST reset before every send).
- Ethernet frame layer + ARP (request/reply, cache for the gateway MAC + 1
  destination host — matching the RAM limit, not a full cache).
- IPv4 (header build/parse + checksum) + ICMP echo reply (for ping
  debugging).
- UDP (send/receive, can act as client and listener).
- DHCP client (state machine: INIT → SELECTING → REQUESTING → BOUND →
  RENEWING), the device obtains an IP automatically from the router. The
  first DISCOVER waits for the physical link to be up, and if no lease is
  obtained within `NET_DHCP_FALLBACK_MS` (counted from boot) the device falls
  back to the static IP from `net_config.h`. DHCP is retried in the
  background, and immediately when the cable is plugged in.
- TCP (simple state machine, single-packet payload per request/response,
  multi-session through port bit-packing like EtherCard — can act as client
  and server).
- NTP client (asks an NTP server for the time over UDP port 123, parses the
  reply timestamp) — lightweight because it rides on the existing UDP layer
  and needs no extra parsing layer such as HTTP.
- **Shared SPI bus (`lib/spi_bus`)**: one hardware SPI used by several
  devices. Each device is described by a descriptor (CS pin, SPI mode,
  clock, flags); `spi_begin()` applies that device's mode and clock right
  before pulling its CS low. Drivers never write `SPCR`/`SPSR` themselves.
- **UART debug output (`lib/uart`)**: TX-only UART0 output, kept separate
  from the application code so `main.c` only holds the application flow.
- Architecture: a standard network stack layering (SPI bus → ENC28J60 driver
  → Ethernet/ARP → IP/ICMP → UDP+DHCP → TCP), WITHOUT a separate mediator
  layer.
- No RTOS — a single polling super-loop, non-blocking (no long `delay()` or
  busy-wait in the main loop; DHCP/TCP run as polled state machines, not as
  blocking functions).
- WDT (watchdog) used as protection against real failures (1 reset per loop
  iteration, timeout > the worst-case duration of one iteration) — not a
  patch for blocking calls.

**Planned (next, not built yet):**
- SD card driver for data logging on the shared SPI bus.
- SPI 16x2 LCD driver (74HC595 backpack, write-only) on the shared SPI bus.

**Out of scope (v1):**
- HTTP client/server (text header parsing, large request/response buffers,
  and usually many parallel connections — it would risk collapsing the 2 KB
  RAM of the ATmega328 if forced in).
- DNS client, Wake-on-LAN.
- Full RFC-compliant TCP retransmission/window scaling — the payload is
  assumed to fit in 1 packet per request/response, matching the ATmega328
  RAM limit (2 KB).
- A full ARP cache for many LAN hosts at once.
- A second SPI bus (USART0 in MSPIM mode, or software SPI). The firmware is
  a single super-loop without DMA, so a second bus would not run anything in
  parallel; USART0 MSPIM would also clash with the UART debug output and
  with bootloader uploads.
- Interrupt-driven SPI (any ISR that touches SPI) and DMA.
- A full FatFs/SdFat on the SD card (needs a 512-byte sector buffer per open
  file — does not fit next to this network stack).

## Constraints
- Target MCU: ATmega328 (2 KB SRAM, 32 KB flash) — no Arduino framework,
  PlatformIO with the avr-gcc toolchain directly.
- Ethernet chip: ENC28J60 (MAC+PHY only, SPI).
- The device acts as both client and server (UDP as well as TCP).
- IP address: DHCP by default, but switchable to a static IP through 1
  toggle in net_config.h (`NET_USE_DHCP`) — dhcp.c is not used at all when
  static is chosen.
- No third-party ENC28J60/TCP-IP library — the driver is written from
  scratch (referring to the EtherCard design as a learning reference is
  allowed).
- **SPI bus rules** (see `lib/spi_bus/include/spi_bus.h`):
  1. One transaction = `spi_begin()` … `spi_end()`, finished before any
     other device is used; never nested.
  2. No ISR may use SPI. An ISR only sets a `volatile` flag, and the main
     loop does the work.
  3. Every device's CS must be driven HIGH (`spi_dev_init()`) before the
     first transaction to ANY device on the bus.
- **ENC28J60 SPI timing** stays at mode 0, fosc/2 (8 MHz at a 16 MHz
  clock). It must not be slowed down.
- **RAM budget**: the measured usage before the SPI/UART refactors was 968 of
  2048 bytes (47.3%) with every feature enabled; the refactors add roughly
  12 bytes (estimate). What is left must cover the stack and anything new.
  In particular a 512-byte SD sector buffer is a tight fit, so plain string
  literals passed to `uart_print()` (which AVR copies into SRAM) should be
  moved to flash first.
- **Blocking work must stay short**: a UART character takes ~260 µs at 38400
  baud, an SD block write can take tens to hundreds of milliseconds, and the
  LCD needs busy-wait delays. While these run, no packet is read; the chip's
  RX buffer is only 3 KB and TCP retransmission is not implemented. Slow
  work must therefore be batched and, where possible, done when no packet is
  pending. The WDT timeout (4 s) must stay far above any single blocking
  step.

## Decision log
| Decision | Reason |
|---|---|
| One shared SPI bus with a per-device descriptor, instead of a second SPI bus | The ATmega328P has one hardware SPI; a second bus (USART MSPIM / software) brings no parallelism in a single super-loop and has real costs (see Out of scope). |
| Removed `cli()`/`sei()` around ENC28J60 transactions | No ISR uses SPI, so they protected nothing; the unconditional `sei()` could re-enable interrupts a caller had disabled, and blocking interrupts during a long transfer of another device (e.g. an SD block) makes `millis()` lose ticks. `-DSPI_BUS_DISABLE_IRQ=1` restores it (with a saved/restored status register) for comparison. I could not open the Microchip errata to confirm that the old `cli()` was not needed for a silicon reason, so the hardware re-test is required. |
| DHCP holds DISCOVER until the link is up, falls back to the static IP `NET_DHCP_FALLBACK_MS` (15 s) after boot, retries in the background (30 s) and immediately on a link-up event (after a 1 s settle) | Cold boots sometimes needed a manual reset: the first DISCOVER was sent while the PHY was still negotiating and the retry was 10 s away. Falling back keeps the device reachable when the router is slow or missing or the cable is unplugged; the link-up trigger recovers a lease as soon as the cable is plugged in. The static IP must match the LAN's subnet (default 192.168.8.x). |
| DISCOVER/REQUEST sent with source 0.0.0.0, DISCOVER resent every 3 s (same xid) | RFC 2131 requires it (some routers drop a DISCOVER carrying the static IP); the reference EtherCard does the same zeroing. Resends avoid losing 10 s to one dropped frame. |
| Each driver owns its device descriptor; `spi_bus` only knows the bus | Adding a device never requires editing `spi_bus` or the network code. |
| `spi_xfer` stays `static inline` in the header | No function-call overhead per byte; same speed as the old in-driver version. |
| UART code split out into `lib/uart` | Easier to read and reuse; `main.c` keeps only the application flow. The functions were moved unchanged. |
| SD logging via raw sequential sectors, or Petit FatFs with a pre-allocated file | Full FatFs/SdFat do not fit in the RAM left next to the network stack. |
| SPI LCD latch pin acts as "CS" | A 74HC595 has no real chip-select: it shifts on every SCK edge and only updates its outputs on the rising edge of the latch pin. |

## Open questions
- **SD approach**: raw sequential sector logging (data can be pulled over
  UDP) versus Petit FatFs writing into a pre-allocated file (readable on a
  PC). Depends on whether the log must be read on a PC directly.
- **LCD module**: exact 74HC595 backpack pin mapping (DS/SH_CP/ST_CP and the
  bit order to RS/E/D4–D7) — needed before writing the driver.
- **DHCP link-wait and static fallback**: verified on hardware (cold boots,
  boot without cable, cable plugged in later, network without DHCP). Still
  open: confirm the static address in `net_config.h` is outside the router's
  DHCP pool. An IP-conflict (ARP probe) check for the static address is not
  implemented.
- **Hardware re-test** of the refactored firmware (ENC28J60 detection, DHCP,
  ping, UDP/TCP echo, serial log) and a fresh `pio run` RAM/Flash
  measurement. Watch for a linker `undefined reference` error, which would
  point to the library link order.
- **Errata check**: confirm the minimum SPI clock requirement of the
  ENC28J60 from the Microchip silicon errata (the search results seen so far
  point to an 8 MHz minimum, but the document could not be opened).