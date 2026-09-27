// millis.h — pengganti Arduino millis(), ditulis dari nol pakai Timer0.
//
// Kenapa perlu: kita gak pakai Arduino framework, jadi gak ada millis()
// gratis. DHCP timeout, retry ARP, TCP timeout — semua butuh referensi
// waktu berjalan dalam milidetik.
//
// Cara kerja: Timer0 mode CTC, interrupt tiap 1ms persis (dihitung dari
// F_CPU), increment counter 32-bit di dalam ISR.

#ifndef MILLIS_H
#define MILLIS_H

#include <stdint.h>

// Panggil sekali di awal main(), sebelum sei() global (fungsi ini yang
// akan mengaktifkan interrupt Timer0; global interrupt tetap harus di-
// enable manual lewat sei() setelah semua init selesai).
void millis_init(void);

// Nilai milidetik sejak millis_init() dipanggil. Overflow (wrap ke 0)
// setelah ~49.7 hari uptime — kalau ada pengurangan waktu, selalu pakai
// unsigned arithmetic (mis. "if (millis_now() - start > timeout_ms)"),
// itu otomatis benar walau kena wrap.
uint32_t millis_now(void);

#endif // MILLIS_H