# ERD / Data structure — enc28j60_stack

## Entities

Firmware ini bukan pengolah data persisten (bukan database), tapi punya
beberapa struktur data inti yang saling berkaitan lewat satu buffer paket
bersama (pola yang diambil dari desain EtherCard's `net.h`):

- **Packet buffer** — satu buffer statis (`uint8_t net_buf[NET_BUF_SIZE]`)
  dipakai bersama untuk RX dan TX. Semua layer baca/tulis field lewat
  offset byte tetap (didefinisikan di `net_offsets.h`), bukan struct
  per-layer, supaya hemat RAM (tidak ada copy antar layer).
- **ARP entry** — MAC address gateway (`gw_mac[6]`) dan MAC address 1 host
  tujuan aktif (`dest_mac[6]`), plus status pending/resolved. Bukan tabel
  ARP penuh — cuma 2 slot (gateway + 1 tujuan), sesuai keterbatasan RAM.
- **Net config** — `my_ip[4]`, `netmask[4]`, `gw_ip[4]`, `dns_ip[4]`
  (kalau dipakai), `mac[6]` — didapat dari DHCP, disimpan di RAM (bukan
  EEPROM, karena bisa berubah tiap boot/lease renewal).
- **DHCP state** — state machine (`INIT/SELECTING/REQUESTING/BOUND/
  RENEWING`), `xid` (transaction id), `lease_start`, `lease_time`.
- **TCP session** — sampai N sesi TCP aktif (N ditentukan saat
  implementasi tcp.c, mengacu bit-packing port ala EtherCard supaya hemat
  RAM): `state` (CLOSED/SYN_SENT/ESTABLISHED/CLOSING dst), `seq`, `ack`,
  `remote_ip`, `remote_port`, `local_port`.
- **UDP listener** — daftar port yang di-listen + callback handler
  (jumlah slot dibatasi kecil, misal 2-4, bukan 8 seperti EtherCard karena
  scope kita lebih sempit).

## Relationships

```
Packet buffer (shared)
   |
   +-- dibaca/ditulis oleh: Ethernet/ARP layer, IP/ICMP layer, UDP layer, TCP layer
   |
ARP entry <---- dipakai oleh IP layer untuk resolve MAC tujuan sebelum kirim
   |
Net config <---- diisi oleh DHCP state machine, dipakai oleh IP layer (src IP)
   |
DHCP state <---- jalan di atas UDP layer (port 67/68)
   |
TCP session (N slot) <---- masing-masing independen, dibedakan lewat port
UDP listener (N slot) <---- masing-masing independen, dibedakan lewat port
```

## Notes
- Tidak ada penyimpanan flash/EEPROM di v1 — semua state di RAM, hilang
  saat reboot (device selalu DHCP ulang saat boot, sesuai constraint PRD).
- Jumlah slot TCP session dan UDP listener final ditentukan saat coding
  tcp.c/udp.c, disesuaikan sisa RAM setelah packet buffer dan driver
  ENC28J60 dialokasikan.