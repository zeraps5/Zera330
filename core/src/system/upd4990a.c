/* Achado #85: o relógio µPD4990A de verdade (calendário + protocolo serial).
 *
 * Até aqui só existia o pino TP (Achados #23 e #65), como um oscilador de 1s
 * com a fase reiniciada a cada STB. A leitura de DATA/HORA nunca foi feita, e
 * o Achado #38 contornava isso zerando a data salva na backup RAM pra "bater"
 * com o nosso relógio sempre em 00-00-00. Só que o MAME lê a data REAL do
 * relógio do PC, cai no caminho "novo dia" da BIOS em `$C11E0A`, e nós no
 * "mesmo dia" — no s1945p isso termina num erro de endereço e a máquina
 * reinicia em loop.
 *
 * Transcrição de `src/devices/machine/upd1990a.cpp` (MAME), só a variante
 * 4990A em modo serial, que é como o Neo-Geo liga o chip (`neogeo.cpp`):
 *     m_upd4990a->cs_w(1); oe_w(1); c0_w(1); c1_w(1); c2_w(1);
 * Escrita em $380051 (bits 0/1/2 = DATA_IN/CLK/STB), leitura em STATUS_A:
 *     bit 6 = tp_r(), bit 7 = data_out_r().
 * Os timers do MAME viram prazos em ciclos do 68000 (12 MHz). */

#include "neocore/neogeo.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CPS 12000000ull /* ciclos do 68000 por segundo */

enum {
    MODE_REGISTER_HOLD = 0, MODE_SHIFT, MODE_TIME_SET, MODE_TIME_READ,
    MODE_TP_64HZ, MODE_TP_256HZ, MODE_TP_2048HZ, MODE_TP_4096HZ,
    MODE_TP_1S_INT, MODE_TP_10S_INT, MODE_TP_30S_INT, MODE_TP_60S_INT,
    MODE_INT_RESET_OUTPUT, MODE_INT_RUN_CLOCK, MODE_INT_STOP_CLOCK, MODE_TEST
};

static u8 to_bcd(int v) { return (u8)(((v / 10) << 4) | (v % 10)); }
static int from_bcd(u8 v) { return (v >> 4) * 10 + (v & 0x0f); }

/* Data inicial: relógio do PC (como o MAME), ou NEOGEO_RTC_DATE=
 * "AAAA-MM-DD HH:MM:SS" pra ter uma data FIXA em testes. */
void neogeo_rtc_init(neogeo_t *ng) {
    int year, month, day, dow, hour, minute, second;
    const char *e = getenv("NEOGEO_RTC_DATE");
    if (e && sscanf(e, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
        struct tm t; memset(&t, 0, sizeof(t));
        t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day; t.tm_hour = 12;
        mktime(&t);
        dow = t.tm_wday;
    } else {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        year = t->tm_year + 1900; month = t->tm_mon + 1; day = t->tm_mday; dow = t->tm_wday;
        hour = t->tm_hour; minute = t->tm_min; second = t->tm_sec;
    }
    /* `rtc_clock_updated`: day_of_week do MAME é 1..7 (1 = domingo) e grava
     * `day_of_week - 1`, ou seja, o nosso tm_wday (0 = domingo) direto. */
    ng->rtc_time[0] = to_bcd(second);
    ng->rtc_time[1] = to_bcd(minute);
    ng->rtc_time[2] = to_bcd(hour);
    ng->rtc_time[3] = to_bcd(day);
    ng->rtc_time[4] = (u8)((month << 4) | (dow & 0x0f));
    ng->rtc_time[5] = to_bcd(year % 100);

    memset(ng->rtc_shift, 0, sizeof(ng->rtc_shift));
    ng->rtc_cmd = 0; ng->rtc_clk = 0; ng->rtc_stb = 0; ng->rtc_data_in = 0;
    ng->rtc_data_out = 0; ng->rtc_pulse = 0; ng->rtc_testmode = 0;
    ng->rtc_clock_enabled = 1;
    ng->rtc_sec_next = ng->master_cycles + CPS;
    ng->rtc_tp_half = 0;  ng->rtc_tp_next = 0;   /* timer_alloc sem adjust: parado */
    ng->rtc_do_half = 0;  ng->rtc_do_next = 0;
}

static int days_in_month(int month, int year) {
    static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (month == 2 && (year % 4) == 0) return 29;
    return d[(month - 1) % 12];
}

/* avanço de 1 segundo (`advance_seconds` do device_rtc_interface) */
static void rtc_advance_second(neogeo_t *ng) {
    int s = from_bcd(ng->rtc_time[0]) + 1, mi = from_bcd(ng->rtc_time[1]), h = from_bcd(ng->rtc_time[2]);
    int d = from_bcd(ng->rtc_time[3]), mo = ng->rtc_time[4] >> 4, dow = ng->rtc_time[4] & 0x0f;
    int y = from_bcd(ng->rtc_time[5]);
    if (s >= 60) { s = 0; mi++; }
    if (mi >= 60) { mi = 0; h++; }
    if (h >= 24) {
        h = 0; d++; dow = (dow + 1) % 7;
        if (mo < 1 || mo > 12) mo = 1;
        if (d > days_in_month(mo, y)) { d = 1; mo++; }
        if (mo > 12) { mo = 1; y = (y + 1) % 100; }
    }
    ng->rtc_time[0] = to_bcd(s); ng->rtc_time[1] = to_bcd(mi); ng->rtc_time[2] = to_bcd(h);
    ng->rtc_time[3] = to_bcd(d); ng->rtc_time[4] = (u8)((mo << 4) | dow); ng->rtc_time[5] = to_bcd(y);
}

/* `timer->adjust(attotime::zero, 0, período)`: dispara JÁ (inverte agora) e
 * depois a cada período.
 *
 * Achado #91: os prazos ficam em PONTO FIXO 16.16 (ciclos << 16). O TP de
 * 4096 Hz tem meio-período de 1464,84375 ciclos; arredondado pra 1464, em
 * ~18 mil inversões a paridade virava — e o nível do TP no instante do
 * próximo comando saía invertido. No garou isso fazia a BIOS esperar uma
 * borda a mais depois do reset por watchdog: 0,5 s de atraso medido contra o
 * MAME (comando 7 do relógio em +42.591.770 ciclos contra +36.591.809). */
#define FP(x) ((u64)(x) << 16)
static void start_tp(neogeo_t *ng, u64 half_fp) {
    ng->rtc_pulse ^= 1u;
    ng->rtc_tp_half = half_fp;
    ng->rtc_tp_next = FP(ng->master_cycles) + half_fp;
}
static void start_do(neogeo_t *ng, u64 half_fp) {
    ng->rtc_data_out ^= 1u;
    ng->rtc_do_half = half_fp;
    ng->rtc_do_next = FP(ng->master_cycles) + half_fp;
}

static void rtc_stb_w(neogeo_t *ng, u8 state) {
    if (!ng->rtc_stb && state) {
        u8 c = ng->rtc_shift[6];            /* modo serial: comando = registrador de 4 bits */
        ng->rtc_cmd = c;
        if (getenv("NEOGEO_TRACE_RTC")) {
            printf("[rtc] comando %u em ciclos68k=%llu (tp=%u)\n", c,
                   (unsigned long long)ng->master_cycles, ng->rtc_pulse);
            fflush(stdout);
        }

        if (c == MODE_REGISTER_HOLD || (c >= MODE_TP_64HZ && c < MODE_TEST)) {
            ng->rtc_clock_enabled = 1;
            ng->rtc_testmode = 0;
        }
        switch (c) {
        case MODE_REGISTER_HOLD:
            start_do(ng, FP(CPS / 2u));     /* from_hz((32768/32768)*2) */
            start_tp(ng, FP(CPS / 128u));   /* from_hz((32768/512)*2) */
            break;
        case MODE_SHIFT:
            if (!ng->rtc_testmode) ng->rtc_clock_enabled = 1;
            ng->rtc_do_half = 0;
            ng->rtc_data_out = ng->rtc_shift[0] & 1u;
            break;
        case MODE_TIME_SET:
            ng->rtc_clock_enabled = 0;
            ng->rtc_do_half = 0;
            ng->rtc_data_out = ng->rtc_shift[0] & 1u;
            for (int i = 0; i < 6; i++) ng->rtc_time[i] = ng->rtc_shift[i];
            break;
        case MODE_TIME_READ:
            if (!ng->rtc_testmode) ng->rtc_clock_enabled = 1;
            for (int i = 0; i < 6; i++) ng->rtc_shift[i] = ng->rtc_time[i];
            start_do(ng, FP(CPS / 2u));     /* 4990A: div 32768 -> from_hz(2) */
            break;
        case MODE_TP_64HZ: case MODE_TP_256HZ: case MODE_TP_2048HZ: case MODE_TP_4096HZ: {
            static const u64 div[4] = { 512u, 128u, 16u, 8u };
            /* from_hz((32768/div)*2) -> meio período = CPS*div/65536 ciclos,
             * que em 16.16 é exatamente CPS*div (sem arredondar). */
            start_tp(ng, CPS * div[c - MODE_TP_64HZ]);
            break;
        }
        case MODE_TP_1S_INT: case MODE_TP_10S_INT: case MODE_TP_30S_INT: case MODE_TP_60S_INT: {
            static const u64 mul[4] = { 1u, 10u, 30u, 60u };
            start_tp(ng, FP((CPS * mul[c - MODE_TP_1S_INT]) / 2u));
            break;
        }
        case MODE_TEST:
            ng->rtc_clock_enabled = 0;
            ng->rtc_do_half = 0;
            ng->rtc_testmode = 1;           /* contador de teste: não emulado (nenhum jogo usa) */
            break;
        default:
            break;
        }
    }
    ng->rtc_stb = state;
}

static void rtc_clk_w(neogeo_t *ng, u8 state) {
    if (!ng->rtc_clk && state) {
        u8 in = ng->rtc_shift[6] & 1u;
        ng->rtc_shift[6] = (u8)((ng->rtc_shift[6] >> 1) | (ng->rtc_data_in << 3));
        if (ng->rtc_cmd == MODE_SHIFT) {
            for (int i = 0; i < 6; i++) {
                ng->rtc_shift[i] >>= 1;
                if (i == 5) ng->rtc_shift[i] |= (u8)(in << 7);
                else        ng->rtc_shift[i] |= (u8)((ng->rtc_shift[i + 1] << 7) & 0x80);
            }
            ng->rtc_data_out = ng->rtc_shift[0] & 1u;
        }
    }
    ng->rtc_clk = state;
}

/* `io_control_w` case 0x28: data_in_w(bit0); clk_w(bit1); stb_w(bit2) —
 * nessa ORDEM, que importa (o CLK usa o DATA_IN recém-escrito). */
void neogeo_rtc_write(neogeo_t *ng, u8 val) {
    ng->rtc_data_in = val & 1u;
    rtc_clk_w(ng, (u8)((val >> 1) & 1u));
    rtc_stb_w(ng, (u8)((val >> 2) & 1u));
}

void neogeo_rtc_tick(neogeo_t *ng) {
    while (ng->rtc_clock_enabled && ng->master_cycles >= ng->rtc_sec_next) {
        rtc_advance_second(ng);
        ng->rtc_sec_next += CPS;
    }
    if (!ng->rtc_clock_enabled) ng->rtc_sec_next = ng->master_cycles + CPS;
    u64 now_fp = FP(ng->master_cycles);
    while (ng->rtc_tp_half && now_fp >= ng->rtc_tp_next) {
        ng->rtc_pulse ^= 1u;
        ng->rtc_tp_next += ng->rtc_tp_half;
    }
    while (ng->rtc_do_half && now_fp >= ng->rtc_do_next) {
        ng->rtc_data_out ^= 1u;
        ng->rtc_do_next += ng->rtc_do_half;
    }
}
