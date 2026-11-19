/* Achado #83: a família CMC50 / NEO-PCM2 / NEO-PVC (2000-2003).
 *
 * Todo o CMC50 (gráfico + M1) já está em prot_sma_kof99.c (Achado #81). Aqui
 * ficam os pedaços que faltavam, cada um transcrito de fonte primária do MAME:
 *
 *   - `prot_kof2k2.cpp`: embaralhamento em blocos de 512KB da P ROM
 *     (kof2002, samsho5, samsh5sp);
 *   - `prot_pvc.cpp`: descriptografia da P ROM dos cartuchos NEO-PVC
 *     (mslug5, svc, kof2003) — XOR + troca de bits + troca de blocos;
 *   - `prot_pcm2.cpp`: as V ROMs (amostras ADPCM) cifradas pelo NEO-PCM2,
 *     nas duas variantes (`decrypt` com permutação de palavras, e `swap`, a
 *     versão mais nova com XOR e endereço embaralhado).
 *
 * ENDIANNESS (a parte traiçoeira): o MAME guarda a região do 68000 na ordem
 * NATIVA do host e usa `BYTE_XOR_LE(i)` pra acessar "o byte certo" dos dois
 * jeitos. Como o nosso `rom.p` é SEMPRE big-endian (byte par = byte alto da
 * word), a tradução é a mesma de uma build do MAME num host big-endian:
 *     BYTE_XOR_LE(a)  ->  (a ^ 1)
 *     rom[i]          ->  p[i]
 * Isso vale tanto pra endereço quanto pro índice da tabela de XOR — conferido
 * que a expressão dá o mesmo resultado nos dois tipos de host. */

#include "neocore/neogeo.h"
#include <stdlib.h>
#include <string.h>

#define BXLE(a) ((a) ^ 1u)

/* mesmo sentido do bitswap<N> do MAME: 1º índice alimenta o bit mais alto */
static u32 bswap_n(u32 v, const int *bits, int n) {
    u32 r = 0;
    for (int k = 0; k < n; k++)
        if ((v >> bits[k]) & 1u) r |= 1u << (n - 1 - k);
    return r;
}

/* ===================== kof2002 / samsho5 / samsh5sp ======================= */

void neogeo_kof2002_decrypt_68k(u8 *cpurom) {
    static const u32 sec[] = { 0x100000,0x280000,0x300000,0x180000,0x000000,0x380000,0x200000,0x080000 };
    u8 *src = cpurom + 0x100000u;
    u8 *dst = (u8 *)malloc(0x400000u);
    if (!dst) return;
    memcpy(dst, src, 0x400000u);
    for (int i = 0; i < 8; ++i) memcpy(src + (u32)i * 0x80000u, dst + sec[i], 0x80000u);
    free(dst);
}

static void block16_decrypt(u8 *src, const u32 *sec) {
    u8 *dst = (u8 *)malloc(0x800000u);
    if (!dst) return;
    memcpy(dst, src, 0x800000u);
    for (int i = 0; i < 16; ++i) memcpy(src + (u32)i * 0x80000u, dst + sec[i], 0x80000u);
    free(dst);
}

void neogeo_samsho5_decrypt_68k(u8 *cpurom) {
    static const u32 sec[] = { 0x000000,0x080000,0x700000,0x680000,0x500000,0x180000,0x200000,0x480000,
                               0x300000,0x780000,0x600000,0x280000,0x100000,0x580000,0x400000,0x380000 };
    block16_decrypt(cpurom, sec);
}

void neogeo_samsh5sp_decrypt_68k(u8 *cpurom) {
    static const u32 sec[] = { 0x000000,0x080000,0x500000,0x480000,0x600000,0x580000,0x700000,0x280000,
                               0x100000,0x680000,0x400000,0x780000,0x200000,0x380000,0x300000,0x180000 };
    block16_decrypt(cpurom, sec);
}

/* ===================== NEO-PVC: P ROM ===================================== */

typedef struct {
    u8  xor1[0x20];
    u8  xor2[0x20];
    int word_bits[16];      /* bitswap<16> das palavras da área bancada */
    int fixed_bits[8];      /* bitswap<8> dos blocos de 64KB do fixo */
    u32 banked_xor;         /* o `^ 0x00700` etc */
    int banked_bits[8];     /* bitswap<8> do nibble de endereço */
    bool kof2003_style;     /* kof2003: p3 + buffer de 9MB + ordem diferente */
} pvc_desc_t;

static void pvc_decrypt(u8 *rom, const pvc_desc_t *d) {
    const u32 rom_size = d->kof2003_style ? 0x900000u : 0x800000u;
    u8 *buf = (u8 *)malloc(rom_size);
    if (!buf) return;

    if (d->kof2003_style)
        for (u32 i = 0; i < 0x100000u; i++) rom[0x800000u + i] ^= rom[0x100002u | i];

    for (u32 i = 0; i < 0x100000u; i++) rom[i] ^= d->xor1[BXLE(i) % 0x20u];
    for (u32 i = 0x100000u; i < 0x800000u; i++) rom[i] ^= d->xor2[BXLE(i) % 0x20u];

    for (u32 i = 0x100000u; i < 0x800000u; i += 4u) {
        u16 rom16 = (u16)(rom[BXLE(i + 1u)] | (rom[BXLE(i + 2u)] << 8));
        rom16 = (u16)bswap_n(rom16, d->word_bits, 16);
        rom[BXLE(i + 1u)] = (u8)(rom16 & 0xffu);
        rom[BXLE(i + 2u)] = (u8)(rom16 >> 8);
    }

    if (!d->kof2003_style) {
        /* mslug5 / svc: lê de uma cópia e escreve no próprio rom */
        memcpy(buf, rom, rom_size);
        for (u32 i = 0; i < 0x100000u / 0x10000u; i++) {
            u32 ofst = (i & 0xf0u) + bswap_n(i & 0x0fu, d->fixed_bits, 8);
            memcpy(&rom[i * 0x10000u], &buf[ofst * 0x10000u], 0x10000u);
        }
        for (u32 i = 0x100000u; i < 0x800000u; i += 0x100u) {
            u32 ofst = (i & 0xf000ffu) + ((i & 0x000f00u) ^ d->banked_xor) +
                       (bswap_n((i & 0x0ff000u) >> 12, d->banked_bits, 8) << 12);
            memcpy(&rom[i], &buf[ofst], 0x100u);
        }
        memcpy(buf, rom, rom_size);
        memcpy(&rom[0x100000u], &buf[0x700000u], 0x100000u);
        memcpy(&rom[0x200000u], &buf[0x100000u], 0x600000u);
    } else {
        /* kof2003: lê do rom e escreve no buffer, depois remonta */
        for (u32 i = 0; i < 0x100000u / 0x10000u; i++) {
            u32 ofst = (i & 0xf0u) + bswap_n(i & 0x0fu, d->fixed_bits, 8);
            memcpy(&buf[i * 0x10000u], &rom[ofst * 0x10000u], 0x10000u);
        }
        for (u32 i = 0x100000u; i < 0x900000u; i += 0x100u) {
            u32 ofst = (i & 0xf000ffu) + ((i & 0x000f00u) ^ d->banked_xor) +
                       (bswap_n((i & 0x0ff000u) >> 12, d->banked_bits, 8) << 12);
            memcpy(&buf[i], &rom[ofst], 0x100u);
        }
        memcpy(&rom[0x000000u], &buf[0x000000u], 0x100000u);
        memcpy(&rom[0x100000u], &buf[0x800000u], 0x100000u);
        memcpy(&rom[0x200000u], &buf[0x100000u], 0x700000u);
    }
    free(buf);
}

void neogeo_mslug5_decrypt_68k(u8 *rom) {
    static const pvc_desc_t d = {
        { 0xc2,0x4b,0x74,0xfd,0x0b,0x34,0xeb,0xd7,0x10,0x6d,0xf9,0xce,0x5d,0xd5,0x61,0x29,
          0xf5,0xbe,0x0d,0x82,0x72,0x45,0x0f,0x24,0xb3,0x34,0x1b,0x99,0xea,0x09,0xf3,0x03 },
        { 0x36,0x09,0xb0,0x64,0x95,0x0f,0x90,0x42,0x6e,0x0f,0x30,0xf6,0xe5,0x08,0x30,0x64,
          0x08,0x04,0x00,0x2f,0x72,0x09,0xa0,0x13,0xc9,0x0b,0xa0,0x3e,0xc2,0x00,0x40,0x2b },
        { 15,14,13,12,10,11,8,9,6,7,4,5,3,2,1,0 },
        { 7,6,5,4,1,0,3,2 },
        0x00700u, { 5,4,7,6,1,0,3,2 }, false,
    };
    pvc_decrypt(rom, &d);
}

void neogeo_svc_decrypt_68k(u8 *rom) {
    static const pvc_desc_t d = {
        { 0x3b,0x6a,0xf7,0xb7,0xe8,0xa9,0x20,0x99,0x9f,0x39,0x34,0x0c,0xc3,0x9a,0xa5,0xc8,
          0xb8,0x18,0xce,0x56,0x94,0x44,0xe3,0x7a,0xf7,0xdd,0x42,0xf0,0x18,0x60,0x92,0x9f },
        { 0x69,0x0b,0x60,0xd6,0x4f,0x01,0x40,0x1a,0x9f,0x0b,0xf0,0x75,0x58,0x0e,0x60,0xb4,
          0x14,0x04,0x20,0xe4,0xb9,0x0d,0x10,0x89,0xeb,0x07,0x30,0x90,0x50,0x0e,0x20,0x26 },
        { 15,14,13,12,10,11,8,9,6,7,4,5,3,2,1,0 },
        { 7,6,5,4,2,3,0,1 },
        0x00a00u, { 4,5,6,7,1,0,3,2 }, false,
    };
    pvc_decrypt(rom, &d);
}

void neogeo_kof2003_decrypt_68k(u8 *rom) {
    static const pvc_desc_t d = {
        { 0x3b,0x6a,0xf7,0xb7,0xe8,0xa9,0x20,0x99,0x9f,0x39,0x34,0x0c,0xc3,0x9a,0xa5,0xc8,
          0xb8,0x18,0xce,0x56,0x94,0x44,0xe3,0x7a,0xf7,0xdd,0x42,0xf0,0x18,0x60,0x92,0x9f },
        { 0x2f,0x02,0x60,0xbb,0x77,0x01,0x30,0x08,0xd8,0x01,0xa0,0xdf,0x37,0x0a,0xf0,0x65,
          0x28,0x03,0xd0,0x23,0xd3,0x03,0x70,0x42,0xbb,0x06,0xf0,0x28,0xba,0x0f,0xf0,0x7a },
        { 15,14,13,12,5,4,7,6,9,8,11,10,3,2,1,0 },
        { 7,6,5,4,0,1,2,3 },
        0x00800u, { 4,5,6,7,1,0,3,2 }, true,
    };
    pvc_decrypt(rom, &d);
}

/* ===================== NEO-PCM2: V ROMs =================================== */

/* `pcm2_prot_device::decrypt`: permuta a posição das PALAVRAS de 16 bits
 * dentro de blocos de `value` bytes. Só move palavras inteiras — não depende
 * de endianness. */
void neogeo_pcm2_decrypt(u8 *ymrom, u32 ymsize, int value) {
    const u32 wv = (u32)value / 2u;
    u8 *buffer = (u8 *)malloc((u32)value);
    if (!buffer) return;
    for (u32 i = 0; i < ymsize / 2u; i += wv) {
        memcpy(buffer, &ymrom[2u * i], (u32)value);
        for (u32 j = 0; j < wv; j++)
            memcpy(&ymrom[2u * (i + j)], &buffer[2u * (j ^ ((u32)value / 4u))], 2u);
    }
    free(buffer);
}

/* `pcm2_prot_device::swap`: a versão dos jogos mais novos. Trabalha byte a
 * byte sobre 16MB fixos. */
void neogeo_pcm2_swap(u8 *ymrom, int value) {
    static const u32 addrs[7][2] = {
        {0x000000,0xa5000}, {0xffce20,0x01000}, {0xfe2cf6,0x4e001}, {0xffac28,0xc2000},
        {0xfeb2c0,0x0a000}, {0xff14ea,0xa7001}, {0xffb440,0x02000} };
    static const u8 xordata[7][8] = {
        {0xf9,0xe0,0x5d,0xf3,0xea,0x92,0xbe,0xef},
        {0xc4,0x83,0xa8,0x5f,0x21,0x27,0x64,0xaf},
        {0xc3,0xfd,0x81,0xac,0x6d,0xe7,0xbf,0x9e},
        {0xc3,0xfd,0x81,0xac,0x6d,0xe7,0xbf,0x9e},
        {0xcb,0x29,0x7d,0x43,0xd2,0x3a,0xc2,0xb4},
        {0x4b,0xa4,0x63,0x46,0xf0,0x91,0xea,0x62},
        {0x4b,0xa4,0x63,0x46,0xf0,0x91,0xea,0x62} };
    static const int bits[24] = { 23,22,21,20,19,18,17,0,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,16 };
    u8 *buf = (u8 *)malloc(0x1000000u);
    if (!buf) return;
    memcpy(buf, ymrom, 0x1000000u);
    for (u32 i = 0; i < 0x1000000u; i++) {
        u32 j = bswap_n(i, bits, 24);
        j ^= addrs[value][1];
        u32 d = (i + addrs[value][0]) & 0xffffffu;
        ymrom[j] = buf[d] ^ xordata[value][j & 0x7u];
    }
    free(buf);
}
