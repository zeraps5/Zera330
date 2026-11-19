/* YM2610 — Fase 5 (áudio). Implementados, nesta ordem: ADPCM-A (samples e
 * vozes), timers A/B (o que faz a música ANDAR), síntese FM (4 canais de 4
 * operadores — a música da maioria dos jogos) e SSG (3 ondas quadradas +
 * ruído). Falta ADPCM-B.
 *
 * ADPCM-A veio primeiro porque é ele que dá a "cara" sonora do Neo-Geo,
 * toca direto das V ROMs que o carregador já lê, e é um decodificador
 * fechado e verificável — bem mais simples que a parte FM.
 *
 * FONTE (algoritmo e mapa de registradores): `ymfm` de Aaron Giles
 * (github.com/aaronsgiles/ymfm), arquivos `src/ymfm_adpcm.cpp` e
 * `src/ymfm_adpcm.h` — código de referência, não resumo. Mesma metodologia
 * dos achados de vídeo: copiar a tabela/fórmula exata em vez de deduzir.
 *
 * Mapa de registradores (banco B do chip, portas $06/$07 do Z80):
 *   $00       KEY ON/OFF: bit7 LIMPO = liga, SETADO = desliga;
 *             bits 0-5 = máscara dos 6 canais
 *   $01       volume mestre (bits 0-5), invertido
 *   $08-$0D   por canal: bit7 = pan esquerdo, bit6 = pan direito,
 *             bits 0-4 = volume do canal (invertido)
 *   $10-$15   endereço inicial, byte baixo   (unidade = 256 bytes)
 *   $18-$1D   endereço inicial, byte alto
 *   $20-$25   endereço final, byte baixo     (inclusivo)
 *   $28-$2D   endereço final, byte alto
 *
 * Taxas: o YM2610 roda a 8 MHz. A saída de FM sai a 8e6/144 ≈ 55555 Hz e o
 * ADPCM-A a 8e6/432 ≈ 18518 Hz — exatamente 1/3. Geramos no ritmo de 55555
 * e avançamos o ADPCM a cada 3 amostras.
 */

#include "neocore/neogeo.h"
#include <string.h>
#include <stdlib.h>

#define ADPCM_A_CHANNELS 6u

/* Tabela de passos do ADPCM (49 entradas) — `s_steps` em ymfm_adpcm.cpp. */
static const u16 adpcm_steps[49] = {
     16,  17,   19,   21,   23,   25,   28,
     31,  34,   37,   41,   45,   50,   55,
     60,  66,   73,   80,   88,   97,  107,
    118, 130,  143,  157,  173,  190,  209,
    230, 253,  279,  307,  337,  371,  408,
    449, 494,  544,  598,  658,  724,  796,
    876, 963, 1060, 1166, 1282, 1411, 1552
};

/* Ajuste do índice de passo por nibble — `s_step_inc` em ymfm_adpcm.cpp. */
static const s8 adpcm_step_inc[8] = { -1, -1, -1, -1, 2, 5, 7, 9 };

static inline s32 clamp_s32(s32 v, s32 lo, s32 hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* Um byte da V ROM (amostras ADPCM-A). Fora do ROM devolve 0 (silêncio). */
/* Achado #99: no MAME (`ym2610_device_base::device_start`) as V ROMs viram
 * um espaço de endereço de 24 bits (`install_rom(0, bytes-1)`); ler fora da
 * região devolve 0 (não dá a volta). */
static inline u8 adpcm_rom_byte(const neogeo_t *ng, u32 addr) {
    addr &= 0xFFFFFFu;
    if (ng->rom.v == NULL || addr >= ng->rom.v_size) return 0;
    return ng->rom.v[addr];
}

/* Avança um canal em UMA amostra de ADPCM-A (um nibble).
 * Espelha `adpcm_a_channel::clock()` do ymfm. */
static void adpcm_a_clock(neogeo_t *ng, u32 ch) {
    ym_adpcm_a_ch_t *c = &ng->ym.adpcm_a[ch];
    if (!c->playing) { c->accumulator = 0; return; }

    u8 data;
    if (c->cur_nibble == 0) {
        /* Fim: o endereço final é INCLUSIVO, então só paramos quando
         * estamos prestes a buscar a amostra seguinte a ele. Só os 20 bits
         * baixos entram na comparação no YM2610 (comentado no ymfm: afeta
         * jogos de verdade, ex. a música da tela de seleção do twinspri). */
        u32 end = (u32)((ng->ym.regs_b[0x20u + ch] | (ng->ym.regs_b[0x28u + ch] << 8)) + 1u) << 8;
        if (((c->cur_address ^ end) & 0xFFFFFu) == 0u) {
            c->playing = 0;
            c->accumulator = 0;
            ng->ym.eos_status |= (u8)(1u << ch); /* Achado #86: `result |= 1 << chnum` */
            return;
        }
        c->cur_byte = adpcm_rom_byte(ng, c->cur_address++);
        data = (u8)(c->cur_byte >> 4);
        c->cur_nibble = 1;
    } else {
        data = (u8)(c->cur_byte & 0x0Fu);
        c->cur_nibble = 0;
    }

    s32 delta = (s32)((2 * (data & 0x07u) + 1) * adpcm_steps[c->step_index] / 8);
    if (data & 0x08u) delta = -delta;

    /* O acumulador é de 12 bits e DÁ A VOLTA (igual ao MSM5205). */
    c->accumulator = (s16)((c->accumulator + delta) & 0xFFF);
    c->step_index = (u8)clamp_s32((s32)c->step_index + adpcm_step_inc[data & 0x07u], 0, 48);
}

/* Saída de um canal, com volume e pan aplicados — `adpcm_a_channel::output()`. */
static void adpcm_a_output(const neogeo_t *ng, u32 ch, s32 *out_l, s32 *out_r) {
    const ym_adpcm_a_ch_t *c = &ng->ym.adpcm_a[ch];
    if (!c->playing) return;

    u32 instrument_level = ng->ym.regs_b[0x08u + ch] & 0x1Fu;
    u32 total_level = ng->ym.regs_b[0x01u] & 0x3Fu;
    int vol = (int)(instrument_level ^ 0x1Fu) + (int)(total_level ^ 0x3Fu);
    if (vol >= 63) return; /* volume mínimo: não soma nada */

    s8 mul = (s8)(15 - (vol & 7));
    u8 shift = (u8)(4 + 1 + (vol >> 3));

    /* acumulador de 12 bits: desloca pra cima pra estender o sinal; o
     * deslocamento de volta já está embutido em `shift`. */
    s16 value = (s16)((((s16)(c->accumulator << 4)) * mul) >> shift);
    value = (s16)(value & ~3);

    if (ng->ym.regs_b[0x08u + ch] & 0x80u) *out_l += value;
    if (ng->ym.regs_b[0x08u + ch] & 0x40u) *out_r += value;
}

/* ============================ SÍNTESE FM ==============================
 *
 * 4 canais de 4 operadores (OPNB = "OPNA com 4 canais"). Toda tabela e toda
 * fórmula abaixo foram COPIADAS do ymfm (`ymfm_fm.ipp` + `ymfm_opn.cpp`),
 * não deduzidas — mesma metodologia dos achados de vídeo. As referências de
 * função ficam no comentário de cada bloco pra facilitar reconferir.
 *
 * Mapa de registradores FM (idêntico nos dois bancos; "c" = 1 ou 2):
 *   $22       LFO: bit3 = habilita, bits0-2 = taxa       (só banco A)
 *   $28       KEY ON/OFF: bits0-1 = canal, bit2 = +3,
 *             bits4-7 = máscara de operadores            (só banco A)
 *   $30+o     bits4-6 = detune, bits0-3 = multiple
 *   $40+o     bits0-6 = total level (atenuação)
 *   $50+o     bits6-7 = KSR, bits0-4 = attack rate
 *   $60+o     bit7 = AM, bits0-4 = decay rate
 *   $70+o     bits0-4 = sustain rate
 *   $80+o     bits4-7 = sustain level, bits0-3 = release rate
 *   $90+o     bit3 = habilita SSG-EG, bits0-2 = modo
 *   $A0+c     fnum baixo (aplica o latch de $A4+c)
 *   $A4+c     bits0-2 = block, bits3-5 = fnum alto (LATCHED)
 *   $B0+c     bits3-5 = feedback, bits0-2 = algoritmo
 *   $B4+c     bit7 = saída esquerda, bit6 = direita,
 *             bits4-5 = sens. AM, bits0-2 = sens. PM
 * Offsets de operador de um canal "c": c, c+8, c+4, c+12 (nessa ordem para
 * op[0..3] — a ordem "natural" do ymfm, que não é a ordem dos endereços).
 */

#define EG_ATTACK  0u
#define EG_DECAY   1u
#define EG_SUSTAIN 2u
#define EG_RELEASE 3u
#define EG_QUIET   0x380u /* Achado #99: `fm_operator::EG_QUIET` do ymfm (era 0x200) */
#define PHASE_STEP_DYNAMIC 0xFFFFFFFFu

/* `s_sin_table` — seno logarítmico 4.8 de 1/4 de período (ymfm_fm.ipp). */
static const u16 s_sin_table[256] = {
    0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
    0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
    0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
    0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
    0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
    0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
    0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
    0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
    0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
    0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
    0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
    0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
    0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
    0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
    0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
    0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
};

/* `s_power_table` — atenuação 5.8 -> volume linear 13 bits. O bit 0x400
 * implícito e o <<2 já vêm embutidos (truque de performance do ymfm). */
#define X(a) (u16)((((a) | 0x400) << 2))
static const u16 s_power_table[256] = {
    X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
    X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
    X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
    X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
    X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
    X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
    X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
    X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
    X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
    X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
    X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
    X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
    X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
    X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
    X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
    X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
    X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
    X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
    X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
    X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
    X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
    X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
    X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
    X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
    X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
    X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
    X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
    X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
    X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
    X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
    X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
    X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
};
#undef X

/* `s_increment_table` — incremento de atenuação por taxa (6 bits) e índice. */
static const u32 s_increment_table[64] = {
    0x00000000, 0x00000000, 0x10101010, 0x10101010,
    0x10101010, 0x10101010, 0x11101110, 0x11101110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x11111111, 0x21112111, 0x21212121, 0x22212221,
    0x22222222, 0x42224222, 0x42424242, 0x44424442,
    0x44444444, 0x84448444, 0x84848484, 0x88848884,
    0x88888888, 0x88888888, 0x88888888, 0x88888888
};

/* `s_detune_adjustment` — deslocamento de fase por keycode/detune. */
static const u8 s_detune_adjustment[32][4] = {
    { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },  { 0,  0,  1,  2 },
    { 0,  1,  2,  2 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },  { 0,  1,  2,  3 },
    { 0,  1,  2,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  4 },  { 0,  1,  3,  5 },
    { 0,  2,  4,  5 },  { 0,  2,  4,  6 },  { 0,  2,  4,  6 },  { 0,  2,  5,  7 },
    { 0,  2,  5,  8 },  { 0,  3,  6,  8 },  { 0,  3,  6,  9 },  { 0,  3,  7, 10 },
    { 0,  4,  8, 11 },  { 0,  4,  8, 12 },  { 0,  4,  9, 13 },  { 0,  5, 10, 14 },
    { 0,  5, 11, 16 },  { 0,  6, 12, 17 },  { 0,  6, 13, 19 },  { 0,  7, 14, 20 },
    { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 },  { 0,  8, 16, 22 }
};

/* Tabela dos 8 algoritmos de ligação (só os 8 do OPN; 8-11 são OPL3). */
#define ALGO(op2in, op3in, op4in, op1out, op2out, op3out) \
    (u16)((op2in) | ((op3in) << 1) | ((op4in) << 4) | \
          ((op1out) << 7) | ((op2out) << 8) | ((op3out) << 9))
static const u16 s_algorithm_ops[8] = {
    ALGO(1,2,3, 0,0,0), ALGO(0,5,3, 0,0,0), ALGO(0,2,6, 0,0,0), ALGO(1,0,7, 0,0,0),
    ALGO(1,0,3, 0,1,0), ALGO(1,1,1, 0,1,1), ALGO(1,0,0, 0,1,1), ALGO(0,0,0, 1,1,1)
};
#undef ALGO

/* Divisores do LFO (ymfm: `lfo_max_count`). */
static const u8 lfo_max_count[8] = { 109, 78, 72, 68, 63, 45, 9, 6 };

static inline u32 bf(u32 v, u32 start, u32 count) {
    return (v >> start) & ((1u << count) - 1u);
}

static inline u32 abs_sin_attenuation(u32 input) {
    if ((input >> 8) & 1u) input = ~input;
    return s_sin_table[input & 0xFFu];
}
static inline u32 attenuation_to_volume(u32 input) {
    return (u32)s_power_table[input & 0xFFu] >> (input >> 8);
}
static inline u32 attenuation_increment(u32 rate, u32 index) {
    return bf(s_increment_table[rate], 4u * index, 4u);
}
static inline s32 detune_adjustment(u32 detune, u32 keycode) {
    s32 result = (s32)s_detune_adjustment[keycode][detune & 3u];
    return ((detune >> 2) & 1u) ? -result : result;
}
static inline u32 effective_rate(u32 rawrate, u32 ksr) {
    if (rawrate == 0u) return 0u;
    u32 r = rawrate + ksr;
    return (r > 63u) ? 63u : r;
}

/* Banco de registradores de um canal FM: os canais 0,1 vivem no banco A e
 * 2,3 no banco B; dentro do banco o offset do canal é 1 ou 2. */
static inline const u8 *fm_regs(const neogeo_t *ng, u32 chan) {
    return (chan < 2u) ? ng->ym.regs_a : ng->ym.regs_b;
}
static inline u32 fm_choffs(u32 chan) { return (chan & 1u) + 1u; }
/* Offsets de registrador dos 4 operadores, na ordem natural op[0..3]. */
static inline u32 fm_opoffs(u32 chan, u32 opnum) {
    static const u32 add[4] = { 0u, 8u, 4u, 12u };
    return fm_choffs(chan) + add[opnum];
}

/* Registradores globais (sempre banco A). */
static inline u32 lfo_enable(const neogeo_t *ng)  { return bf(ng->ym.regs_a[0x22], 3, 1); }
static inline u32 lfo_rate(const neogeo_t *ng)    { return bf(ng->ym.regs_a[0x22], 0, 3); }
/* NÃO implementado: $27 bits6-7 = CSM / "multi frequency" do canal 3
 * (registradores $a8-$ac). Raro no Neo-Geo; se algum jogo soar errado num
 * canal só, é o primeiro suspeito. */

static inline u32 ch_block_freq(const neogeo_t *ng, u32 chan) {
    const u8 *r = fm_regs(ng, chan); u32 c = fm_choffs(chan);
    return ((u32)bf(r[0xa4 + c], 0, 6) << 8) | r[0xa0 + c];
}
static inline u32 ch_feedback(const neogeo_t *ng, u32 chan)   { return bf(fm_regs(ng,chan)[0xb0 + fm_choffs(chan)], 3, 3); }
static inline u32 ch_algorithm(const neogeo_t *ng, u32 chan)  { return bf(fm_regs(ng,chan)[0xb0 + fm_choffs(chan)], 0, 3); }
static inline u32 ch_output_any(const neogeo_t *ng, u32 chan)  { return bf(fm_regs(ng,chan)[0xb4 + fm_choffs(chan)], 6, 2); }
static inline u32 ch_output_l(const neogeo_t *ng, u32 chan)    { return bf(fm_regs(ng,chan)[0xb4 + fm_choffs(chan)], 7, 1); }
static inline u32 ch_output_r(const neogeo_t *ng, u32 chan)    { return bf(fm_regs(ng,chan)[0xb4 + fm_choffs(chan)], 6, 1); }
static inline u32 ch_lfo_am_sens(const neogeo_t *ng, u32 chan) { return bf(fm_regs(ng,chan)[0xb4 + fm_choffs(chan)], 4, 2); }
static inline u32 ch_lfo_pm_sens(const neogeo_t *ng, u32 chan) { return bf(fm_regs(ng,chan)[0xb4 + fm_choffs(chan)], 0, 3); }

static inline u32 op_detune(const neogeo_t *ng, u32 ch, u32 o)       { return bf(fm_regs(ng,ch)[0x30 + fm_opoffs(ch,o)], 4, 3); }
static inline u32 op_multiple(const neogeo_t *ng, u32 ch, u32 o)     { return bf(fm_regs(ng,ch)[0x30 + fm_opoffs(ch,o)], 0, 4); }
static inline u32 op_total_level(const neogeo_t *ng, u32 ch, u32 o)  { return bf(fm_regs(ng,ch)[0x40 + fm_opoffs(ch,o)], 0, 7); }
static inline u32 op_ksr(const neogeo_t *ng, u32 ch, u32 o)          { return bf(fm_regs(ng,ch)[0x50 + fm_opoffs(ch,o)], 6, 2); }
static inline u32 op_attack_rate(const neogeo_t *ng, u32 ch, u32 o)  { return bf(fm_regs(ng,ch)[0x50 + fm_opoffs(ch,o)], 0, 5); }
static inline u32 op_decay_rate(const neogeo_t *ng, u32 ch, u32 o)   { return bf(fm_regs(ng,ch)[0x60 + fm_opoffs(ch,o)], 0, 5); }
static inline u32 op_lfo_am_enable(const neogeo_t *ng, u32 ch, u32 o){ return bf(fm_regs(ng,ch)[0x60 + fm_opoffs(ch,o)], 7, 1); }
static inline u32 op_sustain_rate(const neogeo_t *ng, u32 ch, u32 o) { return bf(fm_regs(ng,ch)[0x70 + fm_opoffs(ch,o)], 0, 5); }
static inline u32 op_sustain_level(const neogeo_t *ng, u32 ch, u32 o){ return bf(fm_regs(ng,ch)[0x80 + fm_opoffs(ch,o)], 4, 4); }
static inline u32 op_release_rate(const neogeo_t *ng, u32 ch, u32 o) { return bf(fm_regs(ng,ch)[0x80 + fm_opoffs(ch,o)], 0, 4); }
static inline u32 op_ssg_eg_enable(const neogeo_t *ng, u32 ch, u32 o){ return bf(fm_regs(ng,ch)[0x90 + fm_opoffs(ch,o)], 3, 1); }
static inline u32 op_ssg_eg_mode(const neogeo_t *ng, u32 ch, u32 o)  { return bf(fm_regs(ng,ch)[0x90 + fm_opoffs(ch,o)], 0, 3); }

/* `compute_phase_step` do opn_registers_base. */
static u32 fm_compute_phase_step(const neogeo_t *ng, u32 chan, const ym_fm_op_t *op, s32 lfo_raw_pm) {
    u32 fnum = bf(op->block_freq, 0, 11) << 1;
    u32 pm_sens = ch_lfo_pm_sens(ng, chan);
    if (pm_sens != 0u) {
        /* `opn_lfo_pm_phase_adjustment` do ymfm: a tabela codifica DOIS
         * deslocamentos por byte (nibbles), que somados fazem uma
         * multiplicação barata dos 7 bits altos do fnum. */
        static const u8 s_lfo_pm_shifts[8][8] = {
            { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77 },
            { 0x77, 0x77, 0x77, 0x77, 0x72, 0x72, 0x72, 0x72 },
            { 0x77, 0x77, 0x77, 0x72, 0x72, 0x72, 0x17, 0x17 },
            { 0x77, 0x77, 0x72, 0x72, 0x17, 0x17, 0x12, 0x12 },
            { 0x77, 0x77, 0x72, 0x17, 0x17, 0x17, 0x12, 0x07 },
            { 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 },
            { 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 },
            { 0x77, 0x77, 0x17, 0x12, 0x07, 0x07, 0x02, 0x01 }
        };
        s32 fnum_bits = (s32)bf(op->block_freq, 4, 7);
        s32 abs_pm = (lfo_raw_pm < 0) ? -lfo_raw_pm : lfo_raw_pm;
        u32 shifts = s_lfo_pm_shifts[pm_sens][bf((u32)abs_pm, 0, 3)];
        s32 adjust = (fnum_bits >> bf(shifts, 0, 4)) + (fnum_bits >> bf(shifts, 4, 4));
        if (pm_sens > 5u) adjust <<= (pm_sens - 5u);
        adjust >>= 2;
        if (lfo_raw_pm < 0) adjust = -adjust;
        fnum = (u32)((s32)fnum + adjust);
        fnum &= 0xFFFu;
    }
    u32 block = bf(op->block_freq, 11, 3);
    u32 phase_step = (fnum << block) >> 2;
    phase_step = (u32)((s32)phase_step + op->detune);
    phase_step &= 0x1FFFFu;
    return (phase_step * op->multiple) >> 1;
}

/* `cache_operator_data` do opn_registers_base. */
static void fm_cache_op(const neogeo_t *ng, u32 chan, u32 opnum, ym_fm_op_t *op) {
    u32 block_freq = ch_block_freq(ng, chan);
    op->block_freq = block_freq;

    u32 keycode = bf(block_freq, 10, 4) << 1;
    keycode |= bf(0xfe80u, bf(block_freq, 7, 4), 1);

    op->detune = detune_adjustment(op_detune(ng, chan, opnum), keycode);

    op->multiple = op_multiple(ng, chan, opnum) * 2u;
    if (op->multiple == 0u) op->multiple = 1u;

    if (lfo_enable(ng) == 0u || ch_lfo_pm_sens(ng, chan) == 0u)
        op->phase_step = fm_compute_phase_step(ng, chan, op, 0);
    else
        op->phase_step = PHASE_STEP_DYNAMIC;

    op->total_level = op_total_level(ng, chan, opnum) << 3;

    u32 sus = op_sustain_level(ng, chan, opnum);
    sus |= (sus + 1u) & 0x10u;   /* 15 vale 31 */
    op->eg_sustain = sus << 5;

    u32 ksrval = keycode >> (op_ksr(ng, chan, opnum) ^ 3u);
    op->eg_rate[EG_ATTACK]  = (u8)effective_rate(op_attack_rate(ng, chan, opnum) * 2u, ksrval);
    op->eg_rate[EG_DECAY]   = (u8)effective_rate(op_decay_rate(ng, chan, opnum) * 2u, ksrval);
    op->eg_rate[EG_SUSTAIN] = (u8)effective_rate(op_sustain_rate(ng, chan, opnum) * 2u, ksrval);
    op->eg_rate[EG_RELEASE] = (u8)effective_rate(op_release_rate(ng, chan, opnum) * 4u + 2u, ksrval);
}

static void fm_start_attack(ym_fm_op_t *op, const neogeo_t *ng, u32 chan, u32 opnum, bool is_restart) {
    if (op->env_state == EG_ATTACK) return;
    op->env_state = EG_ATTACK;
    if (!is_restart)
        op->ssg_inverted = (u8)(op_ssg_eg_enable(ng, chan, opnum) &
                                bf(op_ssg_eg_mode(ng, chan, opnum), 2, 1));
    if (!is_restart) op->phase = 0;
    if (op->eg_rate[EG_ATTACK] >= 62u) op->env_att = 0;
}

static void fm_start_release(ym_fm_op_t *op) {
    if (op->env_state >= EG_RELEASE) return;
    op->env_state = EG_RELEASE;
    if (op->ssg_inverted) {
        op->env_att = (u16)((0x200u - op->env_att) & 0x3FFu);
        op->ssg_inverted = 0;
    }
}

/* `clock_ssg_eg_state` */
static void fm_clock_ssg_eg(ym_fm_op_t *op, const neogeo_t *ng, u32 chan, u32 opnum) {
    if (!((op->env_att >> 9) & 1u)) return;
    u32 mode = op_ssg_eg_mode(ng, chan, opnum);
    if (mode & 1u) { /* modos "hold" */
        op->ssg_inverted = (u8)(bf(mode, 2, 1) ^ bf(mode, 1, 1));
        if (op->env_state != EG_ATTACK)
            op->env_att = op->ssg_inverted ? 0x200u : 0x3FFu;
    } else {
        op->ssg_inverted ^= (u8)bf(mode, 1, 1);
        if (op->env_state == EG_DECAY || op->env_state == EG_SUSTAIN)
            fm_start_attack(op, ng, chan, opnum, true);
        if (bf(mode, 1, 1) == 0u) op->phase = 0;
    }
    if (op->env_state == EG_RELEASE) op->env_att = 0x3FF;
}

/* `clock_envelope` */
static void fm_clock_envelope(ym_fm_op_t *op, const neogeo_t *ng, u32 chan, u32 opnum, u32 env_counter) {
    if (op->env_state == EG_ATTACK && op->env_att == 0) op->env_state = EG_DECAY;
    if (op->env_state == EG_DECAY && op->env_att >= op->eg_sustain) op->env_state = EG_SUSTAIN;

    u32 rate = op->eg_rate[op->env_state];
    u32 rate_shift = rate >> 2;
    env_counter <<= rate_shift;
    if (bf(env_counter, 0, 11) != 0u) return;

    u32 relevant_bits = bf(env_counter, (rate_shift <= 11u) ? 11u : rate_shift, 3);
    u32 increment = attenuation_increment(rate, relevant_bits);

    if (op->env_state == EG_ATTACK) {
        /* Glitch real do chip: taxas 62/63 não incrementam aqui. */
        /* Achado #99: no ymfm `m_env_attenuation` é uint16_t e `~m_env_attenuation`
         * promove pra 32 bits: (~att * inc) >> 4 dá a volta e, truncado de
         * volta pra 16 bits, SUBTRAI ~(att+1)*inc/16 — o ataque desce a
         * atenuação até 0. A versão antiga (`~att & 0x3FF`) subia a atenuação:
         * depois que um operador chegava a 0x3FF nenhuma nota voltava a soar
         * (a música do doubledr calava no primeiro compasso). */
        if (rate < 62u)
            op->env_att = (u16)(op->env_att + ((~(u32)op->env_att * increment) >> 4));
    } else {
        if (!op_ssg_eg_enable(ng, chan, opnum)) op->env_att = (u16)(op->env_att + increment);
        else if (op->env_att < 0x200u)          op->env_att = (u16)(op->env_att + 4u * increment);
        if (op->env_att >= 0x400u) op->env_att = 0x3FF;
    }
}

static void fm_op_clock(neogeo_t *ng, u32 chan, u32 opnum, u32 env_counter, s32 lfo_raw_pm) {
    ym_fm_op_t *op = &ng->ym.fm[chan].op[opnum];

    if (op_ssg_eg_enable(ng, chan, opnum)) fm_clock_ssg_eg(op, ng, chan, opnum);
    else op->ssg_inverted = 0;

    if (bf(env_counter, 0, 2) == 0u) fm_clock_envelope(op, ng, chan, opnum, env_counter >> 2);

    u32 step = op->phase_step;
    if (step == PHASE_STEP_DYNAMIC) step = fm_compute_phase_step(ng, chan, op, lfo_raw_pm);
    op->phase += step;
}

/* `envelope_attenuation` */
static u32 fm_env_attenuation(const neogeo_t *ng, u32 chan, u32 opnum, u32 am_offset) {
    const ym_fm_op_t *op = &ng->ym.fm[chan].op[opnum];
    u32 result = op->env_att;
    if (op->ssg_inverted) result = (0x200u - result) & 0x3FFu;
    if (op_lfo_am_enable(ng, chan, opnum)) result += am_offset;
    result += op->total_level;
    return (result > 0x3FFu) ? 0x3FFu : result;
}

/* `compute_volume` */
static s32 fm_compute_volume(const neogeo_t *ng, u32 chan, u32 opnum, u32 phase, u32 am_offset) {
    const ym_fm_op_t *op = &ng->ym.fm[chan].op[opnum];
    if (op->env_att > EG_QUIET) return 0;
    u32 sin_att = abs_sin_attenuation(phase & 0x3FFu);
    u32 env_att = fm_env_attenuation(ng, chan, opnum, am_offset) << 2;
    return (s32)attenuation_to_volume((sin_att & 0x7FFFu) + env_att);
    /* Nota: no ymfm o sinal vem do bit15 de `sin_attenuation`, que só existe
     * nas formas de onda do OPL. No OPN a tabela é de 1/4 de período e o
     * sinal vem da metade negativa do seno — tratado no chamador. */
}

/* Volume COM sinal: a tabela cobre 1/4 de período; bit9 da fase indica a
 * metade negativa do seno. */
static s32 fm_op_output(const neogeo_t *ng, u32 chan, u32 opnum, u32 phase, u32 am_offset) {
    s32 v = fm_compute_volume(ng, chan, opnum, phase, am_offset);
    return ((phase >> 9) & 1u) ? -v : v;
}

/* `lfo_am_offset` */
static u32 fm_lfo_am_offset(const neogeo_t *ng, u32 chan) {
    u32 am_shift = (1u << (ch_lfo_am_sens(ng, chan) ^ 3u)) - 1u;
    return ((u32)ng->ym.lfo_am << 1) >> am_shift;
}

/* `clock_noise_and_lfo` (OPN não tem ruído). Devolve o PM cru. */
static s32 fm_clock_lfo(neogeo_t *ng) {
    if (!lfo_enable(ng)) {
        ng->ym.lfo_counter = 0;
        /* posição 0 dá AM 0x3f — importante: alguns drivers desligam o LFO
         * global mas deixam AM ligado por operador. */
        ng->ym.lfo_am = 0x3F;
        return 0;
    }
    u32 subcount = (u8)(ng->ym.lfo_counter++);
    if (subcount >= lfo_max_count[lfo_rate(ng)])
        ng->ym.lfo_counter += 0x101u - subcount;

    u32 am = bf(ng->ym.lfo_counter, 8, 6);
    if (bf(ng->ym.lfo_counter, 8 + 6, 1) == 0u) am ^= 0x3Fu;
    ng->ym.lfo_am = (u8)am;

    s32 pm = (s32)bf(ng->ym.lfo_counter, 10, 3);
    if (bf(ng->ym.lfo_counter, 10 + 3, 1)) pm ^= 7;
    return bf(ng->ym.lfo_counter, 10 + 4, 1) ? -pm : pm;
}

/* `output_4op` com rshift=1 e clipmax=32767 (é o que o ym2610 usa). */
static void fm_ch_output(neogeo_t *ng, u32 chan, s32 *out_l, s32 *out_r) {
    ym_fm_ch_t *c = &ng->ym.fm[chan];
    u32 am_offset = fm_lfo_am_offset(ng, chan);

    /* operador 1 com realimentação opcional */
    s32 opmod = 0;
    u32 feedback = ch_feedback(ng, chan);
    if (feedback != 0u) opmod = (c->feedback[0] + c->feedback[1]) >> (10 - feedback);

    s32 op1value = fm_op_output(ng, chan, 0,
                                (u32)((s32)(c->op[0].phase >> 10) + opmod), am_offset);
    c->feedback_in = op1value;

    if (ch_output_any(ng, chan) == 0u) return;

    u32 algo = ch_algorithm(ng, chan);
    u32 algorithm_ops = s_algorithm_ops[algo];

    s32 opout[8];
    opout[0] = 0;
    opout[1] = op1value;
    opout[4] = 0;

    opmod = opout[bf(algorithm_ops, 0, 1)] >> 1;
    opout[2] = fm_op_output(ng, chan, 1, (u32)((s32)(c->op[1].phase >> 10) + opmod), am_offset);
    opout[5] = opout[1] + opout[2];

    opmod = opout[bf(algorithm_ops, 1, 3)] >> 1;
    opout[3] = fm_op_output(ng, chan, 2, (u32)((s32)(c->op[2].phase >> 10) + opmod), am_offset);
    opout[6] = opout[1] + opout[3];
    opout[7] = opout[2] + opout[3];

    opmod = opout[bf(algorithm_ops, 4, 3)] >> 1;
    s32 result = fm_op_output(ng, chan, 3, (u32)((s32)(c->op[3].phase >> 10) + opmod), am_offset);
    result >>= 1;

    if (bf(algorithm_ops, 7, 1)) result = clamp_s32(result + (opout[1] >> 1), -32768, 32767);
    if (bf(algorithm_ops, 8, 1)) result = clamp_s32(result + (opout[2] >> 1), -32768, 32767);
    if (bf(algorithm_ops, 9, 1)) result = clamp_s32(result + (opout[3] >> 1), -32768, 32767);

    if (ch_output_l(ng, chan)) *out_l += result;
    if (ch_output_r(ng, chan)) *out_r += result;
}

/* Uma amostra de FM: atualiza cache, teclas, envelopes, fases e soma a
 * saída dos 4 canais. Devolve o env_counter (usado pro ADPCM-A). */
static u32 fm_clock_and_output(neogeo_t *ng, s32 *out_l, s32 *out_r) {
    /* `prepare`: recarrega o cache e processa a mudança de tecla — só quando
     * houve escrita ou a cada 4096 amostras, igual ao ymfm (Achado #99). */
    if (ng->ym.fm_modified || ng->ym.fm_prepare_count++ >= 4096u) {
        ng->ym.fm_active_mask = 0;
        for (u32 chan = 0; chan < 4u; chan++) {
            for (u32 o = 0; o < 4u; o++) {
                ym_fm_op_t *op = &ng->ym.fm[chan].op[o];
                fm_cache_op(ng, chan, o, op);
                u8 keystate = (u8)(op->keyon_live != 0u);
                if ((keystate ^ op->key_state) != 0u) {
                    op->key_state = keystate;
                    if (keystate) fm_start_attack(op, ng, chan, o, false);
                    else          fm_start_release(op);
                }
                /* `fm_operator::prepare`: ativo até ficar quieto no release */
                if (op->env_state != EG_RELEASE || op->env_att < EG_QUIET)
                    ng->ym.fm_active_mask |= (u8)(1u << chan);
            }
        }
        ng->ym.fm_modified = 0;
        ng->ym.fm_prepare_count = 0;
    }

    /* contador de envelope: x.2, divisor 3 (pula o valor 3 no ciclo baixo) */
    if (bf(++ng->ym.env_counter, 0, 2) == 3u) ng->ym.env_counter += 1u;

    s32 lfo_raw_pm = fm_clock_lfo(ng);

    for (u32 chan = 0; chan < 4u; chan++) {
        ym_fm_ch_t *c = &ng->ym.fm[chan];
        c->feedback[0] = c->feedback[1];
        c->feedback[1] = c->feedback_in;
        for (u32 o = 0; o < 4u; o++) fm_op_clock(ng, chan, o, ng->ym.env_counter, lfo_raw_pm);
    }

    for (u32 chan = 0; chan < 4u; chan++)
        if (ng->ym.fm_active_mask & (1u << chan)) fm_ch_output(ng, chan, out_l, out_r);
    return ng->ym.env_counter;
}

/* Escrita nos registradores FM (vale nos dois bancos). Devolve true se a
 * escrita foi um key on/off, preenchendo `chan`/`opmask`. */
static bool fm_reg_write(neogeo_t *ng, u32 bank, u8 index, u8 data, u32 *chan, u32 *opmask) {
    u8 *regs = bank ? ng->ym.regs_b : ng->ym.regs_a;

    /* $a0-$af: pares com latch. Escrever em $a4-$a7 só guarda os bits altos
     * ($b8/$b9 servem de esconderijo, igual ao ymfm); a escrita no byte
     * baixo é que comita os dois de uma vez — sem isso a frequência muda em
     * dois passos e sai um "glitch" audível em cada nota. */
    if ((index & 0xF0u) == 0xA0u) {
        if (bf(index, 0, 2) == 3u) return false;
        u8 latchindex = (u8)(0xB8u | bf(index, 3, 1));
        if (bf(index, 2, 1)) {
            regs[latchindex] = (u8)(data & 0x3Fu);
        } else {
            regs[index] = data;
            regs[index | 4u] = regs[latchindex];
        }
        return false;
    }
    if ((index & 0xF8u) == 0xB8u) return false; /* uso interno */

    regs[index] = data;

    if (bank == 0u && index == 0x28u) {
        u32 ch = bf(data, 0, 2);
        if (ch == 3u) return false;
        ch += bf(data, 2, 1) * 3u;
        *chan = ch;
        *opmask = bf(data, 4, 4);
        return true;
    }
    return false;
}

/* Mapeia o canal do chip (0-5, com 0 e 3 inexistentes no YM2610) pro nosso
 * índice 0-3. Devolve 0xFF se o canal não existe. */
static inline u32 fm_chan_from_chip(u32 chip_chan) {
    switch (chip_chan) {
        case 1: return 0u;
        case 2: return 1u;
        case 4: return 2u;
        case 5: return 3u;
        default: return 0xFFu;
    }
}

/* ============================= ADPCM-B ================================
 *
 * Um canal só, "delta-T": taxa de reprodução variável (delta-N), passo
 * adaptativo diferente do ADPCM-A e interpolação linear entre amostras.
 * Registradores $10-$1C do banco A (chamamos o offset local de 0-$0C):
 *   $00  bit7 executa, bit6 grava, bit5 externo (ROM), bit4 repete,
 *        bit3 alto-falante, bit0 reset
 *   $01  bit7 pan esquerdo, bit6 pan direito
 *   $02/$03  endereço inicial   (unidade = 256 bytes no YM2610)
 *   $04/$05  endereço final
 *   $09/$0A  delta-N (passo de reprodução, 0.16)
 *   $0B      volume
 *   $0C/$0D  endereço limite
 *
 * NOTA HONESTA: implementado a partir do `adpcm_b_channel` do ymfm, mas
 * ainda NÃO VALIDADO contra som real — nenhuma das ROMs que temos aqui usa
 * ADPCM-B (no MAME, nenhuma delas tem a região `ymsnd:adpcmb`). Quem usa
 * são jogos antigos como nam1975 e ridhero.
 */

#define ADPCM_B_STEP_MIN 127
#define ADPCM_B_STEP_MAX 24576
#define ADPCM_B_ST_EOS     0x01u
#define ADPCM_B_ST_BRDY    0x02u
#define ADPCM_B_ST_PLAYING 0x04u

static inline const u8 *breg(const neogeo_t *ng) { return &ng->ym.regs_a[0x10]; }
static inline u32 b_execute(const neogeo_t *ng)  { return (breg(ng)[0x00] >> 7) & 1u; }
static inline u32 b_record(const neogeo_t *ng)   { return (breg(ng)[0x00] >> 6) & 1u; }
static inline u32 b_external(const neogeo_t *ng) { return (breg(ng)[0x00] >> 5) & 1u; }
static inline u32 b_repeat(const neogeo_t *ng)   { return (breg(ng)[0x00] >> 4) & 1u; }
static inline u32 b_resetflag(const neogeo_t *ng){ return  breg(ng)[0x00] & 1u; }
static inline u32 b_pan_l(const neogeo_t *ng)    { return (breg(ng)[0x01] >> 7) & 1u; }
static inline u32 b_pan_r(const neogeo_t *ng)    { return (breg(ng)[0x01] >> 6) & 1u; }
static inline u32 b_start(const neogeo_t *ng)    { return breg(ng)[0x02] | ((u32)breg(ng)[0x03] << 8); }
static inline u32 b_end(const neogeo_t *ng)      { return breg(ng)[0x04] | ((u32)breg(ng)[0x05] << 8); }
static inline u32 b_delta_n(const neogeo_t *ng)  { return breg(ng)[0x09] | ((u32)breg(ng)[0x0A] << 8); }
static inline u32 b_level(const neogeo_t *ng)    { return breg(ng)[0x0B]; }
static inline u32 b_limit(const neogeo_t *ng)    { return breg(ng)[0x0C] | ((u32)breg(ng)[0x0D] << 8); }

/* No YM2610 o deslocamento de endereço é 8 (unidade de 256 bytes). */
#define ADPCM_B_ADDRSHIFT 8u

static inline bool b_at_end(const neogeo_t *ng) {
    return ng->ym.bcuraddress == (((b_end(ng) + 1u) << ADPCM_B_ADDRSHIFT) - 1u);
}
static inline bool b_at_limit(const neogeo_t *ng) {
    return ng->ym.bcuraddress == (((b_limit(ng) + 1u) << ADPCM_B_ADDRSHIFT) - 1u);
}

static inline u8 adpcm_b_rom_byte(const neogeo_t *ng, u32 addr) {
    /* Achado #99: sem região `adpcmb` (quase todo cartucho), o MAME mapeia o
     * ADPCM-B na MESMA V ROM do ADPCM-A (`else if (m_adpcm_a_region)`). */
    addr &= 0xFFFFFFu;
    if (ng->rom.vb != NULL && ng->rom.vb_size != 0)
        return (addr < ng->rom.vb_size) ? ng->rom.vb[addr] : 0;
    return adpcm_rom_byte(ng, addr);
}

static void adpcm_b_load_start(neogeo_t *ng) {
    ng->ym.bstatus = (u8)((ng->ym.bstatus & ~ADPCM_B_ST_EOS) | ADPCM_B_ST_PLAYING);
    ng->ym.bcuraddress = b_external(ng) ? (b_start(ng) << ADPCM_B_ADDRSHIFT) : 0u;
    ng->ym.bnibble = 0;
    ng->ym.bbyte = 0;
    ng->ym.bposition = 0;
    ng->ym.baccumulator = 0;
    ng->ym.bprev_accum = 0;
    ng->ym.bstep = ADPCM_B_STEP_MIN;
}

static void adpcm_b_reset(neogeo_t *ng) {
    ng->ym.bstatus = ADPCM_B_ST_BRDY;
    ng->ym.bnibble = 0; ng->ym.bbyte = 0; ng->ym.bdummy_read = 0;
    ng->ym.bposition = 0; ng->ym.bcuraddress = 0;
    ng->ym.baccumulator = 0; ng->ym.bprev_accum = 0;
    ng->ym.bstep = ADPCM_B_STEP_MIN;
}

/* `adpcm_b_channel::clock()` — roda uma vez por amostra de FM. */
static void adpcm_b_clock(neogeo_t *ng) {
    if (!b_execute(ng) || b_record(ng) || (ng->ym.bstatus & ADPCM_B_ST_PLAYING) == 0u) {
        ng->ym.bstatus &= (u8)~ADPCM_B_ST_PLAYING;
        return;
    }

    u32 position = (u32)ng->ym.bposition + b_delta_n(ng);
    ng->ym.bposition = (u16)position;
    if (position < 0x10000u) return; /* ainda não virou pra próxima amostra */

    if (ng->ym.bnibble == 0 && b_external(ng))
        ng->ym.bbyte = adpcm_b_rom_byte(ng, ng->ym.bcuraddress);

    u8 data = (u8)((u8)(ng->ym.bbyte << (4u * ng->ym.bnibble)) >> 4);
    ng->ym.bnibble ^= 1u;

    if (ng->ym.bnibble == 0 && b_external(ng)) {
        if (b_at_end(ng)) {
            if (b_repeat(ng)) {
                adpcm_b_load_start(ng);
            } else {
                ng->ym.baccumulator = 0;
                ng->ym.bprev_accum = 0;
                ng->ym.bstatus = (u8)((ng->ym.bstatus & ~ADPCM_B_ST_PLAYING) | ADPCM_B_ST_EOS);
                return;
            }
        } else if (b_at_limit(ng)) {
            ng->ym.bcuraddress = 0;
        } else {
            ng->ym.bcuraddress = (ng->ym.bcuraddress + 1u) & 0xFFFFFFu;
        }
    }

    ng->ym.bprev_accum = ng->ym.baccumulator;

    s32 delta = (s32)((2 * (data & 0x07u) + 1) * (u32)ng->ym.bstep / 8u);
    if (data & 0x08u) delta = -delta;
    ng->ym.baccumulator = clamp_s32(ng->ym.baccumulator + delta, -32768, 32767);

    /* escala do passo: 0.9, 0.9, 0.9, 0.9, 1.2, 1.6, 2.0, 2.4 */
    static const u8 b_step_scale[8] = { 57, 57, 57, 57, 77, 102, 128, 153 };
    ng->ym.bstep = clamp_s32((ng->ym.bstep * (s32)b_step_scale[data & 0x07u]) / 64,
                             ADPCM_B_STEP_MIN, ADPCM_B_STEP_MAX);
}

/* `adpcm_b_channel::output()` com rshift=1 (o que o ym2610 usa). */
static void adpcm_b_output(const neogeo_t *ng, s32 *out_l, s32 *out_r) {
    /* interpolação linear entre a amostra anterior e a atual */
    s32 result = (s32)((ng->ym.bprev_accum * (s32)(((u32)ng->ym.bposition ^ 0xFFFFu) + 1u)
                        + ng->ym.baccumulator * (s32)ng->ym.bposition) >> 16);
    result = (result * (s32)b_level(ng)) >> (8 + 1);
    if (b_pan_l(ng)) *out_l += result;
    if (b_pan_r(ng)) *out_r += result;
}

/* ============================== SSG ===================================
 *
 * A parte "YM2149" do YM2610: 3 canais de onda quadrada, um gerador de
 * ruído por LFSR e um envelope em rampa compartilhado. Registradores
 * $00-$0D do banco A. Fonte: `ymfm_ssg.cpp`/`ymfm_ssg.h`.
 *
 *   $00/$01, $02/$03, $04/$05  período do tom dos canais A/B/C (12 bits)
 *   $06       período do ruído (5 bits)
 *   $07       "mixer", com lógica INVERTIDA: bits0-2 = desliga tom A/B/C,
 *             bits3-5 = desliga ruído A/B/C
 *   $08-$0A   por canal: bit4 = usa o envelope, bits0-3 = volume
 *   $0B/$0C   período do envelope (16 bits)
 *   $0D       forma do envelope: bit3 continue, bit2 attack,
 *             bit1 alternate, bit0 hold — escrever aqui REINICIA a rampa
 *
 * Taxa: o reamostrador do ymfm pro YM2610 é 9 amostras de SSG pra cada 2 de
 * saída (= 250 kHz), com média ponderada. Copiado igual (`resample_2_9`).
 */

/* `s_amplitudes` do ymfm ("vindo do MAME, mas com viés pra que 0 seja 0"). */
static const s16 ssg_amplitudes[32] = {
        0,   32,   78,  141,  178,  222,  262,  306,
      369,  441,  509,  585,  701,  836,  965, 1112,
     1334, 1595, 1853, 2146, 2576, 3081, 3576, 4135,
     5000, 6006, 7023, 8155, 9963,11976,14132,16382
};

static inline u32 ssg_tone_period(const neogeo_t *ng, u32 ch) {
    return (u32)ng->ym.regs_a[0x00 + 2u * ch] | ((u32)(ng->ym.regs_a[0x01 + 2u * ch] & 0x0Fu) << 8);
}
static inline u32 ssg_noise_period(const neogeo_t *ng) { return bf(ng->ym.regs_a[0x06], 0, 5); }
static inline u32 ssg_env_period(const neogeo_t *ng) {
    return (u32)ng->ym.regs_a[0x0B] | ((u32)ng->ym.regs_a[0x0C] << 8);
}

static void ssg_clock(neogeo_t *ng) {
    /* Tom: a unidade do período é clock/16, mas rodamos a clock/8 — então
     * a troca de estado (50% de ciclo) acontece a cada período. */
    for (u32 ch = 0; ch < 3u; ch++) {
        ng->ym.ssg_tone_count[ch]++;
        if (ng->ym.ssg_tone_count[ch] >= ssg_tone_period(ng, ch)) {
            ng->ym.ssg_tone_state[ch] ^= 1u;
            ng->ym.ssg_tone_count[ch] = 0;
        }
    }

    /* Ruído: LFSR de 17 bits. Período 0 tem que soar igual a período 1 —
     * daí o `!= 1` (comentado no ymfm). */
    ng->ym.ssg_noise_count++;
    if ((ng->ym.ssg_noise_count >> 1) >= ssg_noise_period(ng) && ng->ym.ssg_noise_count != 1u) {
        ng->ym.ssg_noise_state ^= (bf(ng->ym.ssg_noise_state, 0, 1) ^ bf(ng->ym.ssg_noise_state, 3, 1)) << 17;
        ng->ym.ssg_noise_state >>= 1;
        ng->ym.ssg_noise_count = 0;
    }

    ng->ym.ssg_env_count++;
    if (ng->ym.ssg_env_count >= ssg_env_period(ng)) {
        ng->ym.ssg_env_state++;
        ng->ym.ssg_env_count = 0;
    }
}

static void ssg_output(neogeo_t *ng, s32 *out3) {
    u8 shape = ng->ym.regs_a[0x0D];
    u32 e_cont = bf(shape, 3, 1), e_attack = bf(shape, 2, 1);
    u32 e_alt = bf(shape, 1, 1), e_hold = bf(shape, 0, 1);

    u32 env_volume;
    if ((e_hold | (e_cont ^ 1u)) && ng->ym.ssg_env_state >= 32u) {
        ng->ym.ssg_env_state = 32u;
        env_volume = ((e_attack ^ e_alt) & e_cont) ? 31u : 0u;
    } else {
        u32 attack = e_attack;
        if (e_alt) attack ^= bf(ng->ym.ssg_env_state, 5, 1);
        env_volume = (ng->ym.ssg_env_state & 31u) ^ (attack ? 0u : 31u);
    }

    for (u32 ch = 0; ch < 3u; ch++) {
        /* Lógica invertida do $07: o bit SETADO DESLIGA a fonte. */
        u32 noise_on = bf(ng->ym.regs_a[0x07], 3u + ch, 1) | ng->ym.ssg_noise_state;
        u32 tone_on  = bf(ng->ym.regs_a[0x07], 0u + ch, 1) | ng->ym.ssg_tone_state[ch];

        u32 volume;
        if ((noise_on & tone_on) == 0u) {
            volume = 0;
        } else if (bf(ng->ym.regs_a[0x08 + ch], 4, 1)) {
            volume = env_volume;
        } else {
            /* volume 15 do canal equivale ao 31 do envelope */
            volume = bf(ng->ym.regs_a[0x08 + ch], 0, 4) * 2u;
            if (volume != 0u) volume |= 1u;
        }
        out3[ch] = ssg_amplitudes[volume & 31u];
    }
}

/* `resample_2_9`: 9 passos de SSG a cada 2 amostras de saída, com os
 * extremos valendo meio passo. Devolve o valor MONO do SSG. */
static s32 ssg_render_sample(neogeo_t *ng) {
    s32 sum[3] = { 0, 0, 0 };
    s32 cur[3];

    if (ng->ym.ssg_sampindex & 1u)
        for (u32 c = 0; c < 3u; c++) sum[c] += ng->ym.ssg_last[c];

    for (u32 rep = 0; rep < 4u; rep++) {
        ssg_clock(ng); ssg_output(ng, cur);
        for (u32 c = 0; c < 3u; c++) { ng->ym.ssg_last[c] = cur[c]; sum[c] += 2 * cur[c]; }
    }
    if ((ng->ym.ssg_sampindex & 1u) == 0u) {
        ssg_clock(ng); ssg_output(ng, cur);
        for (u32 c = 0; c < 3u; c++) { ng->ym.ssg_last[c] = cur[c]; sum[c] += cur[c]; }
    }
    ng->ym.ssg_sampindex++;

    /* O YM2610 do ymfm mistura os 3 canais num só (fator 2/3 pra não
     * estourar) e o MAME manda esse mono pros dois alto-falantes. */
    return (sum[0] + sum[1] + sum[2]) * 2 / (3 * 9);
}

/* Períodos dos timers, em amostras de FM (ver comentário em neogeo.h). */
static s32 timer_a_period(const neogeo_t *ng) {
    u32 ta = ((u32)ng->ym.regs_a[0x24] << 2) | (ng->ym.regs_a[0x25] & 0x03u);
    return (s32)(1024u - ta);
}
static s32 timer_b_period(const neogeo_t *ng) {
    return (s32)(16u * (256u - ng->ym.regs_a[0x26]));
}

/* Reavalia a linha de IRQ pro Z80: fica ativa enquanto houver um flag de
 * timer setado E a IRQ daquele timer estiver habilitada em $27. */
static void ym_update_irq(neogeo_t *ng) {
    u8 ctrl = ng->ym.regs_a[0x27];
    u8 active = (u8)(ng->ym.timer_flags & (((ctrl >> 2) & 0x03u)));
    u8 want = active ? 1u : 0u;
    if (want != ng->ym.irq_line) {
        ng->ym.irq_line = want;
        Z80_IRQ(&ng->z80, want ? true : false);
    }
}

/* Avança os timers em `fm_samples` amostras. Chamado pelo laço de CPU (não
 * pelo render de áudio), pra que a música ande mesmo em ferramentas que não
 * geram som. */
void neogeo_ym2610_timer_tick(neogeo_t *ng, u32 fm_samples) {
    u8 ctrl = ng->ym.regs_a[0x27];

    if (ctrl & 0x01u) { /* timer A ligado */
        ng->ym.timer_a_count -= (s32)fm_samples;
        while (ng->ym.timer_a_count <= 0) {
            ng->ym.timer_a_count += timer_a_period(ng);
            ng->ym.timer_flags |= 0x01u;
        }
    }
    if (ctrl & 0x02u) { /* timer B ligado */
        ng->ym.timer_b_count -= (s32)fm_samples;
        while (ng->ym.timer_b_count <= 0) {
            ng->ym.timer_b_count += timer_b_period(ng);
            ng->ym.timer_flags |= 0x02u;
        }
    }
    ym_update_irq(ng);
}

/* Registrador de status (portas $04/$06 do Z80). */
u8 neogeo_ym2610_read_status(const neogeo_t *ng) {
    return ng->ym.timer_flags; /* bit0 = A estourou, bit1 = B estourou; bit7 (busy) vem de neogeo.c */
}

/* Achado #86: `ym2610::read_status_hi` = m_eos_status & m_flag_mask. */
u8 neogeo_ym2610_read_status_hi(const neogeo_t *ng) {
    return (u8)(ng->ym.eos_status & ng->ym.eos_flag_mask);
}

void neogeo_ym2610_write_reg(neogeo_t *ng, u32 bank, u8 reg, u8 val) {
    /* Faixas que NÃO são FM (ver ym2610::write_data / write_data_hi do ymfm):
     *   banco A: $00-$0D SSG, $10-$1C ADPCM-B  -> FM começa em $1D
     *   banco B: $00-$2F ADPCM-A               -> FM começa em $30
     * O resto vai pro banco de registradores FM, que tem o latch de $a0-$af
     * e o key on/off de $28. */
    bool is_fm = bank ? (reg >= 0x30u) : (reg >= 0x1Du);
    if (is_fm) {
        u8 prev27 = ng->ym.regs_a[0x27];
        u32 chip_chan = 0, opmask = 0;
        /* `fm_engine_base::write`: $27 (REG_MODE) vai direto pros timers e
         * NÃO marca os canais como modificados. */
        if (!(bank == 0u && reg == 0x27u)) ng->ym.fm_modified = 1;
        bool keyon = fm_reg_write(ng, bank, reg, val, &chip_chan, &opmask);
        if (keyon) {
            u32 chan = fm_chan_from_chip(chip_chan);
            if (chan != 0xFFu)
                for (u32 o = 0; o < 4u; o++)
                    ng->ym.fm[chan].op[o].keyon_live = (u8)bf(opmask, o, 1);
        }
        /* $27 (modo/timers) é FM na faixa acima, então trata aqui também. */
        if (bank == 0u && reg == 0x27u) {
            if (val & 0x10u) ng->ym.timer_flags &= (u8)~0x01u;
            if (val & 0x20u) ng->ym.timer_flags &= (u8)~0x02u;
            if ((val & 0x01u) && !(prev27 & 0x01u)) ng->ym.timer_a_count = timer_a_period(ng);
            if ((val & 0x02u) && !(prev27 & 0x02u)) ng->ym.timer_b_count = timer_b_period(ng);
            ym_update_irq(ng);
        }
        return;
    }

    /* Achado #86: $1C do banco A NÃO é ADPCM-B — é o reset dos flags de fim
     * de amostra (`ym2610::write_data`: "1C: EOS flag reset"). */
    if (bank == 0u && reg == 0x1Cu) {
        ng->ym.regs_a[reg] = val;
        ng->ym.eos_flag_mask = (u8)(~val & 0xBFu);
        ng->ym.eos_status &= (u8)~(val & 0xBFu);
        return;
    }
    /* $10-$1B do banco A = ADPCM-B. */
    if (bank == 0u && reg >= 0x10u && reg <= 0x1Bu) {
        ng->ym.regs_a[reg] = val;
        if (reg == 0x10u) {
            if (b_execute(ng)) adpcm_b_load_start(ng);
            else ng->ym.bstatus &= (u8)~ADPCM_B_ST_EOS;
            if (b_resetflag(ng)) adpcm_b_reset(ng);
            if (b_external(ng)) ng->ym.bdummy_read = 2;
        }
        return;
    }

    if (bank == 0u && reg <= 0x0Du) {
        ng->ym.regs_a[reg] = val;
        /* Escrever a forma do envelope REINICIA a rampa — é assim que o
         * driver dispara cada nota de SSG. */
        if (reg == 0x0Du) ng->ym.ssg_env_state = 0;
        return;
    }

    if (bank == 0u) {
        u8 prev = ng->ym.regs_a[0x27];
        ng->ym.regs_a[reg] = val;

        if (reg == 0x27u) {
            /* bits 4/5 limpam os flags de A/B */
            if (val & 0x10u) ng->ym.timer_flags &= (u8)~0x01u;
            if (val & 0x20u) ng->ym.timer_flags &= (u8)~0x02u;
            /* borda de subida em "liga" recarrega o contador */
            if ((val & 0x01u) && !(prev & 0x01u)) ng->ym.timer_a_count = timer_a_period(ng);
            if ((val & 0x02u) && !(prev & 0x02u)) ng->ym.timer_b_count = timer_b_period(ng);
            ym_update_irq(ng);
        }
        return;
    }

    ng->ym.regs_b[reg] = val;

    /* $00 = KEY ON/OFF do ADPCM-A. bit7 LIMPO liga, SETADO desliga. */
    if (reg == 0x00u) {
        u8 mask = (u8)(val & 0x3Fu);
        for (u32 ch = 0; ch < ADPCM_A_CHANNELS; ch++) {
            if (!(mask & (1u << ch))) continue;
            ym_adpcm_a_ch_t *c = &ng->ym.adpcm_a[ch];
            if (val & 0x80u) {
                c->playing = 0;
                c->accumulator = 0;
            } else {
                u32 start = (u32)(ng->ym.regs_b[0x10u + ch] | (ng->ym.regs_b[0x18u + ch] << 8)) << 8;
                c->cur_address = start;
                c->cur_nibble = 0;
                c->cur_byte = 0;
                c->accumulator = 0;
                c->step_index = 0;
                c->playing = 1;
            }
        }
    }
}

void neogeo_ym2610_reset(neogeo_t *ng) {
    memset(&ng->ym, 0, sizeof(ng->ym));
    /* Achado #99: `fm_operator::reset` (att 0x3FF, estado RELEASE) e
     * `opn_registers_base::reset` (saída L+R ligada em $B4-$B6 dos 2 bancos). */
    for (u32 ch = 0; ch < 4u; ch++)
        for (u32 o = 0; o < 4u; o++) {
            ng->ym.fm[ch].op[o].env_att = 0x3FF;
            ng->ym.fm[ch].op[o].env_state = EG_RELEASE;
        }
    ng->ym.regs_a[0xB4] = ng->ym.regs_a[0xB5] = ng->ym.regs_a[0xB6] = 0xC0;
    ng->ym.regs_b[0xB4] = ng->ym.regs_b[0xB5] = ng->ym.regs_b[0xB6] = 0xC0;
    ng->ym.fm_modified = 1;
    ng->ym.ssg_noise_state = 1; /* o LFSR não pode começar zerado */
    ng->ym.eos_flag_mask = 0xBFu; /* Achado #86: EOS_FLAGS_MASK no reset do ymfm */
    adpcm_b_reset(ng);
}

/* Gera `num_samples` quadros estéreo intercalados (L,R) em 16 bits, na taxa
 * nativa de saída do chip (NEOGEO_AUDIO_RATE). */
/* Achado #88: o chip agora avança no TEMPO EMULADO. Antes, FM/ADPCM/SSG só
 * andavam quando o frontend pedia áudio (`neogeo_audio_render`), no ritmo da
 * placa de som do PC — então o estado que o Z80 LÊ do chip (fim de amostra
 * do ADPCM, porta $06) dependia de quando o SDL pediu áudio, e nas
 * ferramentas sem som (debug_screenshot) o ADPCM simplesmente nunca tocava.
 * No MAME, `m_stream->update()` roda antes de cada leitura do chip, então o
 * estado é sempre o do instante emulado. Aqui: uma amostra a cada 216 ciclos
 * do 68000 (= 144 clocks do YM2610 a 8 MHz), gerada dentro do laço das CPUs
 * e guardada num buffer circular que o frontend só esvazia. */
static void ym_generate_one(neogeo_t *ng, s16 *out_l, s16 *out_r) {
    const bool mute_fm = ng->ym_mute_fm, mute_adpcm = ng->ym_mute_adpcm, mute_ssg = ng->ym_mute_ssg;
    s32 l = 0, r = 0;

    s32 fm_l = 0, fm_r = 0;
    u32 env_counter = fm_clock_and_output(ng, &fm_l, &fm_r);
    if (!mute_fm) { l += fm_l; r += fm_r; }

    if (bf(env_counter, 0, 2) == 0u)
        for (u32 ch = 0; ch < ADPCM_A_CHANNELS; ch++) adpcm_a_clock(ng, ch);

    if (!mute_adpcm)
        for (u32 ch = 0; ch < ADPCM_A_CHANNELS; ch++) adpcm_a_output(ng, ch, &l, &r);

    /* ADPCM-B roda uma vez por amostra de FM. */
    adpcm_b_clock(ng);
    {   /* Achado #86: EOS visível do ADPCM-B (bit 7), copiado do ymfm: o bit 6
         * guarda o último valor "ao vivo" e só quando ele MUDA o bit 7 muda. */
        u8 live_eos = (ng->ym.bstatus & ADPCM_B_ST_EOS) ? 0x40u : 0x00u;
        if (((live_eos ^ ng->ym.eos_status) & 0x40u) != 0u)
            ng->ym.eos_status = (u8)((ng->ym.eos_status & ~0xC0u) | live_eos | (live_eos << 1));
    }
    if (!mute_adpcm) adpcm_b_output(ng, &l, &r);

    /* Achado #99: `ym2610::clock_fm_and_adpcm` faz clamp16 de FM+ADPCM ANTES
     * da mixagem; o MAME (`neogeo.cpp`) soma 0.98 disso com 0.84 do SSG
     * (mono, nos dois lados). */
    l = clamp_s32(l, -32768, 32767) * 98 / 100;
    r = clamp_s32(r, -32768, 32767) * 98 / 100;
    s32 ssg = ssg_render_sample(ng);
    if (!mute_ssg) { ssg = ssg * 84 / 100; l += ssg; r += ssg; }

    *out_l = (s16)clamp_s32(l, -32768, 32767);
    *out_r = (s16)clamp_s32(r, -32768, 32767);
}

void neogeo_ym2610_generate_sample(neogeo_t *ng) {
    s16 l, r;
    ym_generate_one(ng, &l, &r);
    u32 w = ng->audio_wr;
    ng->audio_ring[w * 2u] = l;
    ng->audio_ring[w * 2u + 1u] = r;
    ng->audio_wr = (w + 1u) & (NEOGEO_AUDIO_RING - 1u);
    if (ng->audio_wr == ng->audio_rd) /* cheio: descarta a mais antiga */
        ng->audio_rd = (ng->audio_rd + 1u) & (NEOGEO_AUDIO_RING - 1u);
    ng->audio_last_l = l; ng->audio_last_r = r;
}

u32 neogeo_audio_available(const neogeo_t *ng) {
    return (ng->audio_wr - ng->audio_rd) & (NEOGEO_AUDIO_RING - 1u);
}

/* Entrega as amostras JÁ geradas no tempo emulado. Se faltar (o frontend
 * pediu mais do que a emulação produziu), repete a última amostra em vez de
 * inventar chip — isso não altera o estado do YM. */
void neogeo_audio_render(neogeo_t *ng, s16 *out, u32 num_samples) {
    for (u32 i = 0; i < num_samples; i++) {
        if (ng->audio_rd != ng->audio_wr) {
            out[i * 2u]      = ng->audio_ring[ng->audio_rd * 2u];
            out[i * 2u + 1u] = ng->audio_ring[ng->audio_rd * 2u + 1u];
            ng->audio_rd = (ng->audio_rd + 1u) & (NEOGEO_AUDIO_RING - 1u);
        } else {
            out[i * 2u] = ng->audio_last_l;
            out[i * 2u + 1u] = ng->audio_last_r;
        }
    }
}
