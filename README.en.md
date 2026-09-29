[🇮🇩 Bahasa Indonesia](README.md) | **🇬🇧 English**

# AVR ATmega328P ENC28J60

ENC28J60 driver + TCP/IP stack (ARP, IP, ICMP, UDP, TCP, DHCP, NTP) for the
ATmega328, written in pure AVR-C with PlatformIO — no Arduino framework,
no third-party libraries. The SPI bus is shared through a small `spi_bus`
layer, so more SPI devices (SD card, SPI LCD, ...) can be added later
without touching the network code. The UART debug output lives in its own
small `uart` library, separate from the application code.

**Status: the network stack has been tested on real ENC28J60 hardware —
DHCP obtains an IP, ICMP, UDP echo and TCP echo all work.** Note that this
test was done *before* the shared SPI bus refactor (see
[Shared SPI bus](#shared-spi-bus-libspi_bus)); the refactored code has only
been syntax-checked on a host compiler so far and still needs the hardware
re-test listed under [Still to verify](#still-to-verify-not-done-yet).

```
RAM:   [======    ]  58.8% (used 1204 bytes from 2048 bytes)
Flash: [====      ]  38.8% (used 12500 bytes from 32256 bytes)
```
(measured with all features enabled at once: ICMP + UDP + DHCP + NTP + TCP,
before the shared SPI bus refactor. The refactor is expected to add roughly
12 bytes of RAM and ~150 bytes of flash — that is an estimate, not a
measurement; re-run `pio run` to get the real numbers.)

## What do these terms mean? (as simple as possible)

If networking is new to you, the abbreviations above (ARP, IP, ICMP, ...)
can be confusing. Here they are explained with everyday comparisons, NOT
textbook definitions:

- **MAC address** — a unique serial number "stamped by the factory" on every
  network device (like a motorbike's frame number). It never changes and
  you can't pick it yourself.
- **IP address** — the home address on the network (e.g. `192.168.8.184`).
  Unlike the MAC, this address CAN change every time you move to another
  network.
- **ARP** — the way to find out "which MAC address owns this IP". Like
  shouting across the neighbourhood: *"Whoever lives at house number 5,
  what is your real name?"* — and the owner of that house answers. The
  device remembers the answer for a while so it doesn't need to shout again
  every time it sends a letter to the same house.
- **Ethernet frame** — the outermost envelope for sending data INSIDE one
  local network (the same cable/switch). It only says "from which MAC, to
  which MAC".
- **IP / IP packet** — the SECOND envelope, inside the Ethernet envelope. It
  says "from which IP, to which IP" — this is what lets data travel further,
  across many networks (all the way to the internet), not just one cable.
- **ICMP** — the "hello, are you still alive?" message (`ping`). Used to
  check whether a device on the network is up and reachable, without
  sending any real data.
- **UDP** — sending a letter WITHOUT a receipt. Fast and light, but if the
  letter gets lost on the way, the sender never finds out. Good for small
  messages where an occasional loss is not fatal.
- **TCP** — sending a letter WITH a mandatory receipt. Every letter must be
  acknowledged with "received" before the next one is sent — heavier than
  UDP, but reliable for anything that must arrive intact (commands, small
  files).
- **TCP handshake** (3 steps: SYN → SYN-ACK → ACK) — the polite greeting
  before a serious conversation: *"hi, may I connect?"* → *"sure, I'm ready
  too"* → *"ok, let's start"*. Only after these 3 steps may real data be
  sent.
- **Port** — a SMALL door number inside one IP address. One house (one IP)
  can have many doors (ports) for different services at the same time —
  port `7` for the echo service, port `5000` for UDP echo in this project,
  and so on.
- **DHCP** — an automatic "receptionist" on the network that hands out IP
  addresses to newly connected devices, so nobody has to configure them by
  hand. The address is only LENT (it has an expiry, called a *lease* — see
  the next item), not owned forever.
- **DHCP lease** — the validity period of the IP "loan" from DHCP above.
  Before it runs out, the device must ask the server to confirm again
  (*renew*) if it wants to keep the same IP.
- **NTP** — a way to ask an internet time server "what time is it now?", so
  a device without its own battery-backed clock (RTC) still knows the right
  date and time.
- **Checksum** — a small number calculated from the contents of a packet and
  sent along with it. The receiver recalculates it from what arrived — if
  the result differs from the checksum that was sent, part of the packet got
  damaged on the way (like re-adding the total on a shopping receipt: if it
  doesn't match what's printed, something is wrong).
- **MSS (Maximum Segment Size)** — the limit of "don't send one TCP chunk
  bigger than this", so the receiver can hold it (our device has tiny RAM,
  so it can't hold an oversized chunk at once).
- **Broadcast** — shouting to EVERYONE on the network at once, instead of
  whispering to one specific address. Used, for example, when a device has
  no IP at all yet and needs to ask "is there a DHCP server here?"
- **Link up / link down** — the state of the PHYSICAL connection (the
  Ethernet cable). "up" = cable connected and active, "down" = unplugged /
  loose / no signal at all — this is about the cable, NOT about whether an IP
  was obtained (those are two different things, see "IP stays valid when the
  physical link flaps" below).

## TCP standards used here

### Echo Protocol (RFC 862) — why the returned data is EXACTLY the same

The demo TCP/UDP echo ports (`DEMO_TCP_ECHO_PORT`/`DEMO_UDP_ECHO_PORT`) in
this project follow **RFC 862 — Echo Protocol**: whatever data is sent to
the server is sent back **byte for byte identical**, unchanged, with
nothing added. This is an old standard (from the early days of the
internet) that exists only for **connectivity testing** — checking that the
send/receive path on the network really works — not for real applications.
If you send `woii` and the device answers `woii`, exactly the same, that is
**not a bug** — it means the echo server is working correctly. If you want
the device to answer something different, it is no longer the standard echo
protocol, and you have to write your own logic in `on_tcp_echo()` /
`on_udp_echo()` (`src/main.c`).

### TCP flags (not just ACK)

TCP has several 1-bit "flags" in its header, used to mark the purpose of
each packet. All of them are defined in
`lib/enc28j60_net/include/net_offsets.h`:

| Flag | Simple meaning |
|---|---|
| `SYN` | "I'd like to start a connection, may I?" — sent at the start of a connection |
| `ACK` | "OK, received/understood" — attached to almost every packet after the handshake |
| `SYN`+`ACK` | Combined: "you may connect, and I'm ready too" (reply in step 2 of the handshake) |
| `PSH` | "This is real data, hand it to the application right away, don't hold it in a buffer" |
| `FIN` | "I'm done sending, I want to close the connection" (a polite goodbye) |
| `RST` | "Something is wrong/invalid, the connection is forcibly cancelled" (not a goodbye, rougher than FIN) |
| `URG` | Urgent priority data — **not used** in this project (the field is always zeroed, see `TCP_URGENT_P` in `tcp.c`) |

Flow in this firmware (you can match it directly to the `[TCP] ...` lines in
your serial monitor):

1. The client sends **SYN** → the device answers **SYN+ACK** → the client
   answers **ACK** → the connection is `ESTABLISHED`, and the log
   `[TCP] koneksi baru` appears.
2. Every time data is sent (in either direction) → **PSH+ACK** (see
   `tcp_send()` in `tcp.c`, which always sends this 2-flag combination).
3. One side sends **FIN+ACK** → the device immediately answers **FIN+ACK**
   too (a one-sided close, not a full `CLOSE_WAIT` — see the v1 limitations
   in `tcp.h`) → the session is closed, and the log `[TCP] sesi ditutup`
   appears.
4. If an **RST** arrives → the device closes the session immediately without
   any reply (see `tcp_input()`, the `if (flags & TCP_FLAG_RST)` block).

## Why write it from scratch instead of using EtherCard/UIPEthernet?

To fully understand every layer (raw SPI → MAC/PHY init → Ethernet frame →
ARP → IP → ICMP/UDP/TCP → DHCP) and to avoid the class-based C++ overhead of
Arduino that is not needed on a 2 KB RAM target.

The EtherCard library design (njh/ethercard) was used as a learning
reference for a few techniques (a single buffer + byte offsets to save RAM,
ENC28J60 chip errata workarounds, the DHCP state machine flow) — read,
understood, then rewritten in our own style. Parts outside the scope (HTTP
client, DNS server, Wake-on-LAN, Stash/BufferFiller) were left out. NTP is
included (unlike the original plan, which dropped all of EtherCard's
application layers).

## Architecture

A standard network stack layering, without a mediator layer:

```
SPI raw (AVR registers)
   |
spi_bus (shared bus: per-device CS + SPI mode + clock)
   |
ENC28J60 driver (init, packetSend/packetReceive, errata workarounds)
   |
Ethernet frame + ARP
   |
IPv4 + ICMP
   |
UDP (+ DHCP client on top)   TCP (simple state machine, multi-session)
```

No RTOS — a single non-blocking super-loop. See `docs/PRD.md` for the
scope, the constraints and the decision log, and `docs/ERD.md` for the data
structures and the RAM budget.

## Folder structure

```
src/main.c              - demo application only (init -> DHCP -> UDP+TCP loop)
lib/uart/                - UART0 debug output (TX only)
  include/                - uart.h
  src/                    - uart.c
lib/spi_bus/             - shared SPI bus (device descriptors, begin/end)
  include/                - spi_bus.h
  src/                    - spi_bus.c
lib/enc28j60_net/        - our own network library
  include/                - one header per layer
  src/                    - one implementation per layer
.github/workflows/        - CI (build on every push) + auto-release (build on every tag)
docs/                    - additional documentation
test/                    - (optional, unit tests for platform-independent logic)
```

## Shared SPI bus (`lib/spi_bus`)

The ATmega328P has exactly ONE hardware SPI. Several devices can share it,
but a separate chip-select (CS) pin per device is not enough: each device
also needs its own SPI **mode** and **clock speed** (ENC28J60: mode 0 at
fosc/2; an SD card must be initialised at ≤ 400 kHz; displays vary). So every
device is described by one `spi_dev_t`, and `spi_begin()` applies that
device's mode and clock right before pulling its CS low. Device drivers
never touch `SPCR`/`SPSR` themselves.

Because the firmware is a single super-loop with no DMA and no RTOS, a
second SPI bus would not make anything run in parallel — one bus with
several chip-selects is the right design here.

### API

| Function | What it does |
|---|---|
| `spi_bus_init()` | MOSI/SCK output, MISO input, SS (PB2) output HIGH, SPI master on. Idempotent. |
| `spi_dev_init(&dev)` | Make the device's CS pin an output, HIGH (deselected). |
| `spi_begin(&dev)` | Apply the device's mode + clock (only if different from the current one), then CS LOW. |
| `spi_end(&dev)` | CS HIGH (+ 8 dummy clocks if `SPI_DEV_RELEASE_MISO` is set). |
| `spi_xfer(b)` | Exchange one byte (inline, as fast as the old in-driver version). |
| `spi_read(buf, len, fill)` / `spi_write(buf, len)` | Bulk transfers. |
| `spi_bus_error()` | Number of nested-transaction violations since the last call (then reset). |

A device is described like this (the ENC28J60 one, from `enc28j60_hw.c`):

```c
static const spi_dev_t s_enc_dev =
    SPI_DEV(ENC_CS_PORT, ENC_CS_DDR, ENC_CS_BIT, SPI_MODE0 | SPI_DIV2, 0);
```

Modes: `SPI_MODE0..3`. Clock dividers: `SPI_DIV2 .. SPI_DIV128` (relative to
`F_CPU`). Flag: `SPI_DEV_RELEASE_MISO` (needed for SD cards, see below).

### Rules (required)

1. **One transaction = `spi_begin()` … `spi_end()`**, finished before any
   other device is used. Transactions must not be nested.
2. **No ISR may use SPI.** An ISR only sets a `volatile` flag; the main loop
   does the actual work. (The only ISR in this project is the `millis` timer,
   which does not touch SPI.)
3. **Every device's CS must be driven HIGH (`spi_dev_init()`) before the
   first transaction to ANY device on the bus** — otherwise a floating CS
   pin can make an extra device listen to another device's traffic. Today
   `enc_init()` does this for the ENC28J60; once more devices exist, call
   `spi_bus_init()` and every device's `spi_dev_init()` in `main()` *before*
   `enc_init()`.

### What changed compared to the old driver

- The SPI setup, `spi_xfer` and the CS handling used to be `static`
  functions inside `enc28j60_hw.c`. They now live in `spi_bus`; the ENC28J60
  driver only owns its device descriptor. The public API in
  `enc28j60_hw.h` is unchanged, and no upper layer (ARP/IP/UDP/TCP/DHCP/NTP)
  touches SPI, so nothing above the driver changed.
- The old `cli()` in `chip_select()` and unconditional `sei()` in
  `chip_deselect()` were **removed**. No ISR uses SPI, so they protected
  nothing, the unconditional `sei()` could re-enable interrupts a caller had
  disabled on purpose, and blocking interrupts during a long transfer of
  another device (e.g. a 512-byte SD block at a slow clock) would make
  `millis()` lose ticks. If you ever suspect this change, build with
  `-DSPI_BUS_DISABLE_IRQ=1` to get interrupt blocking back (the status
  register is saved and restored, so it is safe even when the caller already
  disabled interrupts).
- ENC28J60 timing is unchanged: SPI mode 0, fosc/2 (8 MHz at a 16 MHz
  clock). Do not slow it down.

### Adding another SPI device

1. Choose a free pin for its CS and define a `spi_dev_t` with the mode and
   clock from its datasheet.
2. Call `spi_dev_init(&dev)` for it in `main()` *before* `enc_init()`.
3. Wrap every access in `spi_begin(&dev)` … `spi_end(&dev)`. Never write
   `SPCR`/`SPSR` yourself.

Notes for the devices planned next (drivers not written yet):

- **SD card** — initialise at ≤ 400 kHz (`SPI_DIV64` = 250 kHz or
  `SPI_DIV128` = 125 kHz at 16 MHz), then switch the descriptor to
  `SPI_DIV2`. Set `SPI_DEV_RELEASE_MISO`: many cards keep driving MISO until
  they get extra clocks with CS high, which would collide with the ENC28J60.
  RAM is the real constraint (see below), and a block write can take tens to
  hundreds of milliseconds, during which no packets are read — write in
  batches, ideally when no packet is pending, because TCP retransmission is
  not implemented.
- **SPI LCD 16x2 (74HC595 backpack)** — write-only, so it cannot collide on
  MISO. A 74HC595 has no real chip-select: it shifts on *every* SCK edge,
  and its outputs change only on the rising edge of the latch pin. Use the
  latch pin as "CS": HIGH when idle, LOW while shifting, HIGH at the end.
  The HD44780 has no busy-flag readback through the backpack, so the driver
  needs delays (init ≈ 40–50 ms once at boot, `clear` ≈ 1.6 ms): update it
  row by row and avoid clearing in the main loop.

Memory: with the measured 968 bytes of RAM already used, a 512-byte SD
sector buffer is a tight fit. Move the `uart_print("...")` string literals
in `main.c` to flash (`PSTR`) first — on AVR plain string literals are
copied into SRAM. A full FatFs/SdFat does not fit next to this network
stack; use raw sequential sector logging, or Petit FatFs with a
pre-allocated file.

## Build & flash

```bash
pio run                 # compile-check (env "uno")
pio run --target upload # compile + flash to the board (Arduino Uno bootloader)
pio device monitor -b 38400  # watch the UART debug log
```

You need a board with an **ATmega328** chip and the standard Arduino Uno
bootloader (`board = uno` in `platformio.ini` — this only affects how the
chip is flashed/fused; the code itself is still pure AVR-C, not the Arduino
framework — see the comments in `platformio.ini`).

## Using the prebuilt firmware, without compiling

Every time a release tag (`v1.0.0`, etc.) is pushed to this repo, GitHub
Actions (`.github/workflows/release.yml`) compiles automatically and
attaches a ready-to-flash `.hex` file to the repo's **Releases** page — just
download it, no need to install PlatformIO or compile anything:

1. Open the **Releases** tab of this GitHub repo.
2. Download `enc28j60_stack-vX.Y.Z-uno.hex`.
3. Flash it with `avrdude` directly, or through PlatformIO:
   ```bash
   avrdude -c arduino -p atmega328p -P COM3 -b 115200 -U flash:w:enc28j60_stack-vX.Y.Z-uno.hex:i
   ```
   (replace `COM3` with your board's serial port — `/dev/ttyUSB0` on
   Linux/Mac)

Every ordinary push/PR (not a tag) only runs the compile-check
(`.github/workflows/build.yml`) — its `.hex` output is also stored as a CI
build artifact (not a Release), to check the build result per commit.

## Serial debug & protocol for other applications (e.g. ESP32)

UART TX-only, default **38400 baud, 8N1**. The UART code is in
`lib/uart` (`uart.h` / `uart.c`); it only knows how to send text to the TX
pin (PD1) and knows nothing about the network or SPI. The `NET:`/`UDP:`/`TCP:`
line formats below are built in `src/main.c`, `lib/uart` is just the sender.

| Function | What it does |
|---|---|
| `uart_init()` | Enable the transmitter (8N1). Call once at the start of `main()`. |
| `uart_putc(c)` / `uart_print(s)` | Send one character / a C string (no automatic newline). Blocking. |
| `uart_print_ip(ip)` / `uart_print_ipport(ip, port)` | Print `a.b.c.d` / `a.b.c.d:port`. |
| `uart_print_data(data, len)` | Print raw payload bytes, capped at `UART_DATA_PRINT_MAX`, with `...(dipotong)` when cut. |

The baud rate and the payload cap can be overridden from `platformio.ini`
with `build_flags = -DUART_BAUD=115200UL -DUART_DATA_PRINT_MAX=32`.
`uart.c` needs `F_CPU` (already provided by `board = uno`) and stops the
build with an error if it is missing, instead of silently using a wrong
baud rate. Sending is blocking: at 38400 baud one character takes about
260 µs, so a long line stalls the main loop for a few milliseconds — print
only what you need.

There are 2 kinds of lines in the output:

- **Human log lines** (`[boot] ...`, `[status] ...`, `[TCP] koneksi baru
  ...`, etc.) — for people to read only; the format may change at any time,
  other programs must NOT parse them.
- **Machine-readable data lines** — 1 line = 1 message, always starting with
  a fixed prefix, safe to parse with `startsWith()` + split. Any line other
  than these 3 kinds can simply be ignored by the parser.

| Prefix | When it appears | Example |
|---|---|---|
| `NET:` | Once as soon as an IP is obtained (DHCP done / static IP ready), and again whenever the IP changes | `NET:ip=192.168.8.184;mask=255.255.255.0;gw=192.168.8.1;dns=192.168.8.1` |
| `UDP:` | For every UDP datagram arriving on the echo port (see `DEMO_UDP_ECHO_PORT`) | `UDP:halo!` |
| `TCP:` | For every chunk of TCP data arriving on the echo port (see `DEMO_TCP_ECHO_PORT`) | `TCP:tes` |

Example parsing on an ESP32 (Arduino framework), no extra library needed:

```cpp
void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("NET:")) {
      // split "ip=...;mask=...;gw=...;dns=..." by ';' and then by '='
    } else if (line.startsWith("UDP:")) {
      String payload = line.substring(4);
    } else if (line.startsWith("TCP:")) {
      String payload = line.substring(4);
    }
    // any other line (human log) is skipped automatically if it doesn't match a prefix above
  }
}
```

**Limitations to keep in mind:** the data (`UDP:`/`TCP:` payload) must NOT
contain `\r`/`\n` bytes itself (they cut the line short) — fine for short
text, not for arbitrary binary data. The printed payload length is also
capped by `UART_DATA_PRINT_MAX` (48 bytes) in `lib/uart/include/uart.h` so
the serial port doesn't get flooded.

## Implementation status

- [x] SPI driver + ENC28J60 registers (enc28j60_hw.c)
- [x] Shared SPI bus (`lib/spi_bus`) — ENC28J60 driver moved onto it
      (host syntax-check only; hardware re-test pending)
- [x] UART debug output split out of `main.c` into `lib/uart` (functions
      moved unchanged; host syntax-check only; hardware re-test pending)
- [x] Ethernet frame + ARP (eth_arp.c) + net_state.c (shared my_ip/my_mac)
- [x] IPv4 + ICMP (ip_icmp.c)
- [x] UDP (udp.c)
- [x] DHCP client (dhcp.c) - can be disabled, switch to a static IP with
      `NET_USE_DHCP` in net_config.h
- [x] DHCP waits for the physical link before the first DISCOVER, falls
      back to the static IP if no lease arrives, and picks up a lease as
      soon as the cable is plugged in (see
      [DHCP fallback to a static IP](#dhcp-fallback-to-a-static-ip));
      verified on hardware
- [x] NTP client (ntp.c, on top of UDP)
- [x] TCP (tcp.c) - client + server, 2 sessions, no data retransmission
- [x] main.c demo (UART debug, UDP+TCP echo ports changeable through
      `DEMO_UDP_ECHO_PORT`/`DEMO_TCP_ECHO_PORT`)
- [x] Machine-readable serial protocol (`NET:`/`UDP:`/`TCP:`) for other
      devices to consume (ESP32, etc.)
- [x] PlatformIO compile-check (passed, see the RAM/Flash numbers above)
- [x] GitHub Actions CI (build on every push/PR + auto-release on every tag)
- [x] **Tested on real ENC28J60 hardware** (before the SPI bus refactor) -
      DHCP obtains an IP, UDP echo and TCP echo all verified working (not
      just compile-checked)
- [ ] SD card driver for data logging (planned)
- [ ] SPI LCD 16x2 (74HC595) driver (planned)

## Bugs found & fixed through real hardware testing

Recorded here so it is clear what went wrong and why the fix looks the way
it does - useful if similar odd behaviour shows up later:

1. **`millis.c` included `<avr/atomic.h>` (does not exist)** — it should be
   `<util/atomic.h>`. A fatal compile error, caught in the very first
   compile-check.
2. **LTO (Link-Time Optimization) + a custom library made the linker fail**
   ("undefined reference" to ALL our functions) — fix: `build_unflags =
   -flto` in `platformio.ini`.
3. **DHCP never got an IP** even though the physical link was up — root
   cause: the "broadcast" bit in the DHCP header (`flags`, offset 10-11) was
   never set, so the DHCP server unicast the OFFER/ACK reply to an IP we
   didn't have yet, and that packet was DROPPED by our own `eth_input()`
   filter. Fix: set that `0x8000` bit in `dhcp_send()`.
4. **The first TCP connection to the device often failed ("Could not
   connect")** from a new client that was not yet "known" — root cause: the
   first SYN-ACK/UDP reply to a new host had to wait one ARP round-trip
   first (cache miss), and if the client-side timeout was shorter than that,
   the client gave up. Fix: `eth_arp_learn()` — as soon as a UDP/TCP packet
   arrives, the ARP cache is filled straight from the sender's MAC that was
   JUST received (no separate ARP request needed anymore).

## DHCP fallback to a static IP

**Symptom that led to this:** after power-up the device sometimes had no IP
until the reset button was pressed once or twice.

**Cause (found by reading `dhcp.c`):** the first DHCPDISCOVER used to be sent
on the very first loop iteration after boot, without checking the physical
link. Right after `enc_init()` the PHY is still negotiating the link (about
1–3 s, longer on some switches), so that frame was simply lost — and the
next attempt only came 10 s later (`DHCP_TIMEOUT_MS`). Pressing reset before
those 10 s passed started the same cycle again.

**What changed:**
1. `dhcp_poll()` holds DISCOVER until `enc_link_up()` is true. When the link
   comes up *later* (cable plugged in), it waits `NET_DHCP_LINK_SETTLE_MS`
   (1 s) so the PHY/switch can settle, then searches immediately.
2. The fallback timer counts from `dhcp_start()` (boot), not from the first
   DISCOVER. If no lease is obtained within `NET_DHCP_FALLBACK_MS` (default
   15 s) — router silent **or** cable never plugged in — the device switches
   to `NET_STATIC_IP` / `NET_STATIC_NETMASK` / `NET_STATIC_GATEWAY`. Serial
   shows `[NET] fallback static`, then the usual `NET:` line.
3. While searching, DISCOVER is resent every 3 s (same transaction id) inside
   the 10 s window, so one lost frame no longer costs 10 s.
4. DISCOVER and the first REQUEST are sent with source IP 0.0.0.0 (RFC 2131),
   even while the static IP is active; the static IP is restored right after
   sending. Renewals keep the real source IP.
5. After the fallback, DHCP is retried in the background every
   `NET_DHCP_RETRY_AFTER_FALLBACK_MS` (default 30 s; `0` = never). A link-up
   event (cable plugged in) skips that wait and searches right away. The
   static IP is never wiped during a search. If a lease arrives, it replaces
   the static IP, serial shows `[NET] DHCP ok`, and `NET:` is printed again.
   Open TCP sessions on the old IP are lost when that happens. If DHCP keeps
   failing, the device simply stays on the static IP.

All of it is set in `net_config.h`:

```c
#define NET_STATIC_IP       {192, 168, 8, 200}   // must match YOUR subnet and be
#define NET_STATIC_NETMASK  {255, 255, 255, 0}   // outside the router's DHCP pool
#define NET_STATIC_GATEWAY  {192, 168, 8, 1}
#define NET_DHCP_FALLBACK_STATIC   1              // 0 = old behaviour (retry forever)
#define NET_DHCP_FALLBACK_MS       15000UL        // counted from boot
#define NET_DHCP_RETRY_AFTER_FALLBACK_MS  30000UL
#define NET_DHCP_LINK_SETTLE_MS    1000UL         // wait after a NEW link-up
```

**Verified on hardware (serial logs):** cold boot with cable → lease within
seconds; boot without cable → `[NET] fallback static` after 15 s; cable
plugged in afterwards → `[NET] DHCP ok` and `mode=dhcp/bound` without a
restart; cable unplugged/plugged while bound → lease kept; plugged into a
network without DHCP → stays on the static IP.

**Important:** the default static address follows the network seen in the
hardware log (192.168.8.x). If your network is different, change it — a
static IP in the wrong subnet cannot reach anything. Also choose an address
outside the router's DHCP range, otherwise it can clash with another device.
There is no IP-conflict (ARP probe) check yet.

**Status line:** `[status] link=..., ip=..., mode=dhcp|static-fb/<state>`.
`dhcp/bound` = lease from DHCP; `static-fb/...` = static fallback active.

**If problems persist even with a good link,** look at the power supply: the
ENC28J60 draws about 160 mA and a weak 3.3V rail can make the chip fail to
initialise (serial shows `ENC28J60 TIDAK terdeteksi`, and the watchdog
reboots the MCU). A 10–100 µF capacitor close to the module is a cheap thing
to try.

## IP stays valid when the physical link flaps

Verified from the real hardware log: the ENC28J60 link did flicker
(`link=down` briefly appeared between `link=up` lines, most likely because of
a weak 3.3V power supply, not a firmware bug) — and `ip=OK` **never
dropped** during that time. This is the correct behaviour: a DHCP lease is
time-based on the server side, not link-state-based, so a momentary link
flicker does not invalidate the IP.

There was one gap, which is now closed: if the link is down for **long
enough** (not just a momentary flicker — the default threshold
`NET_LINK_DOWN_RENEW_MS` = 3000 ms in `net_config.h`), the cable may have
been moved to another network/switch, or our IP may have been given to
another device by the server while we were "gone". To be safe, once the link
recovers after being down that long, the firmware automatically calls
`dhcp_force_renew()` — asking the server to confirm the lease again
(DHCPREQUEST, not a DISCOVER from scratch), without throwing away the IP
currently in use unless the server really rejects it (NAK). If the IP turns
out to change after this, the `NET:` line on serial is printed again
automatically (not just once at boot).

Link flickers shorter than that threshold (the power-supply case above) are
deliberately NOT allowed to trigger anything — to avoid spamming
DHCPREQUEST to the server every time the link flickers momentarily.

## Protocol hardening (RX checksum verification + MSS)

Found through code review (not from a symptom on hardware) — the v1 stack
only **computed** checksums when sending, but never **verified** the
checksum of **incoming** packets. This has been fixed:

1. **RX checksum verification** — the IP header, ICMP, UDP (unless the
   sender deliberately sent `0x0000`, meaning the checksum is not used, per
   RFC 768) and TCP are now validated as soon as a packet arrives. A packet
   whose checksum doesn't match (e.g. corrupted by noise on the cable) is
   silently dropped and never processed or answered.
2. **Packet length guard vs. header claim** — `ip_input()` now compares the
   bytes ACTUALLY received against the length claimed by the IP header. A
   truncated frame (larger than `NET_BUF_SIZE`, see above) is now dropped
   instead of being processed partially (which previously risked reading
   leftover data of an old packet still sitting in the buffer).
3. **TCP now advertises MSS** (Maximum Segment Size) in the SYN/SYN-ACK
   options, automatically following `NET_BUF_SIZE`. This closes #2 from the
   sender's side: an RFC-compliant peer will never send a segment larger
   than we can hold, so the "truncated" case above should ideally never
   happen with a normal client at all — the guard in point 2 stays as a
   safety net for clients that ignore MSS.

Other points that were considered but NOT changed, with the reasons:

- **1-slot destination ARP cache** — if several hosts with different IPs
  "talk" to the device almost at the same time, the cache is overwritten
  by each of them (a small re-ARP delay, not data loss — `eth_arp_learn()`
  still refills it from the received frame). This is a deliberate RAM design
  decision, not a bug — left as it was.
- **DHCP broadcast flag during renewal** — re-checked, and it turned out to
  have no effect (the device already holds a valid IP, so we can receive the
  server's reply whether it is broadcast or unicast). Not changed.
- **TCP data retransmission** — NOT implemented yet. This is not a small
  fix: it needs a copy of the data buffered per TCP session for retries
  (roughly `NET_MAX_TCP_SESSIONS` × MSS extra bytes of RAM, 400-700+ bytes
  depending on the scheme), which on this 2 KB ATmega328 is a trade-off
  decision that has to be discussed first, not added silently.

## Still to verify (not done yet)

- **Hardware re-test after the shared SPI bus and UART refactors**: build with
  `pio run` (watch for a linker error `undefined reference to
  spi_bus_init` — it would point to the library link order), then check
  ENC28J60 detection (`[boot] ENC28J60 rev ... terdeteksi`), DHCP, ping, UDP
  echo (port 5000) and TCP echo (port 8080), and that the serial log still
  appears at 38400 baud (a linker error mentioning `uart_*` would also point
  to the library link order). If anything misbehaves, try
  `-DSPI_BUS_DISABLE_IRQ=1` first to rule out the removal of `cli()`/`sei()`.
- Re-measure RAM/Flash with `pio run` (the numbers at the top predate the
  refactor).
- A long-running test of DHCP lease renewal (normal, not from link recovery)
  has not been tried - only a short test so far.
- `dhcp_force_renew()` on link recovery (see the section "IP stays valid
  when the physical link flaps" above) has not been tested in the scenario
  where the cable is really unplugged and then plugged into another network
  - it has only been verified by reading the code against the existing
  link-flap log.
- Change `MY_MAC` in `src/main.c` if you build more than one device (avoid
  a MAC clash on the same network).

## ENC28J60 wiring (default `net_config.h`)

| ENC28J60 module pin | Connect to ATmega328 (Arduino Uno style) | Notes |
|---|---|---|
| VCC | **3.3V** | The ENC28J60 chip is 3.3V, not 5V - check the module's datasheet if it has an on-board regulator |
| GND | GND | Common ground is mandatory |
| SCK | D13 (PB5) | Hardware SPI pin, cannot be moved |
| SI / MOSI | D11 (PB3) | Hardware SPI pin, cannot be moved |
| SO / MISO | D12 (PB4) | Hardware SPI pin, cannot be moved |
| CS / SS | D10 (PB2) | Software-defined, can be changed (see below) |
| INT | not connected | This driver polls, it is not interrupt-driven |
| RESET (if present) | not connected | Reset is done in software (`OP_SOFT_RESET`) |

Change the CS pin through these 3 lines in
`lib/enc28j60_net/include/net_config.h`:
```c
#define ENC_CS_DDR      DDRB
#define ENC_CS_PORT     PORTB
#define ENC_CS_BIT      PB2   // <- change to match the new pin
```

Other SPI devices (SD card, LCD, ...) share SCK, MOSI and MISO with the
ENC28J60 and only need their own CS pin — see
[Shared SPI bus](#shared-spi-bus-libspi_bus). Keep PB2 as an output even if
it is not used as a CS: if the hardware SS pin becomes an input and reads
LOW, the SPI module silently switches to slave mode. SD cards are 3.3V
devices; use a module with level shifting if the ATmega runs at 5V.