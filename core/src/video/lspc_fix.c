/* Renderização da camada fix (texto/HUD) do LSPC2 — início da Fase 4.
 *
 * Fontes e nível de confiança (ver docs/ARCHITECTURE.md):
 *   - Mapa de tiles em VRAM $7000-$74FF (40x32 words, um por tile,
 *     bits15-12=paleta bits11-0=código do tile): confirmado na NeoGeo
 *     Development Wiki, página "Fix layer".
 *   - ORDEM DOS ENDEREÇOS (Achado #15, ver docs/ROADMAP.md): o mapa é
 *     COLUMN-MAJOR, não row-major! Tínhamos `map_idx = row*40+col` (como um
 *     framebuffer raster comum) — ERRADO. A wiki ("Displaying text",
 *     exemplo de código) usa `FIXMAP + Y + ((X+1)*32)`, ou seja
 *     `map_idx = col*32 + row` (32 linhas alocadas por coluna, só 28
 *     visíveis — os outros 4 words de cada coluna de 32 nunca aparecem na
 *     tela). Com a fórmula errada, o CONTEÚDO de cada tile saía certo (o
 *     código de tile lido era válido) mas a CÉLULA da tela ficava trocada
 *     — o sintoma visual era texto/glifos reconhecíveis mas espalhados
 *     pela tela em vez de formar uma mensagem/título coerente. Descoberto
 *     comparando visualmente contra o MAME real depois do Achado #14
 *     destravar o boot o bastante pra desenhar texto de verdade.
 *   - Formato do tile de 32 bytes na S ROM (Achado #17, ver
 *     docs/ROADMAP.md): a página "Fix graphics format" da wiki é vaga
 *     demais pra reconstruir sem ambiguidade (texto resumido por
 *     ferramentas de IA, não a página original), e nossa primeira tentativa
 *     (`byte = (x/2)*8 + y`, nibble par=baixo/ímpar=alto) dava um efeito de
 *     "eco"/glifo quase encaixando mas duplicado (visto em screenshot,
 *     reportado pelo usuário). Em vez de adivinhar de novo, usamos o
 *     `charlayout` REAL do driver `neogeo.cpp` do MAME (fonte primária,
 *     não resumo) pra fix tiles 8x8/4bpp: xoffset (em nibbles, não bits) =
 *     {33,32,49,48,1,0,17,16} pros 8 pixels x=0..7, yoffset (em bits) =
 *     {0,8,16,24,32,40,48,56} (múltiplos de 8 = 1 byte por linha). Convertendo:
 *     os 4 "pares de pixel" (byte-colunas) ficam armazenados na ordem
 *     x{4,5}, x{6,7}, x{0,1}, x{2,3} — ou seja, a METADE DIREITA do tile
 *     (x4-7) vem ANTES da metade esquerda (x0-3) no endereço, confirmando o
 *     "H: half (0=direita,1=esquerda)" mencionado na wiki. Essa metade da
 *     conclusão bateu depois com o render REAL do MAME (`pix_offsets[] =
 *     {0x10,0x18,0x00,0x08}`, ver Achado #28) e está CERTA.
 *   - CORREÇÃO (Achado #28, ver docs/ROADMAP.md): a OUTRA metade da
 *     conclusão do Achado #17 estava ERRADA. Tínhamos deduzido do
 *     `charlayout` que "x par = nibble ALTO, x ímpar = baixo" — dedução
 *     nossa em cima de uma tabela de offsets, não código de render. O
 *     render real (`draw_fixed_layer_2pixels` em `neogeo_spr.cpp`) diz o
 *     contrário: x PAR = nibble BAIXO, x ÍMPAR = nibble ALTO. Fórmula
 *     final: byte_col = (x<4?1:0)*2 + (x%4)/2; byte_off = byte_col*8 + y;
 *     nibble BAIXO se x par, ALTO se x ímpar.
 *   - Seleção de banco de paleta (PALBANK0/1 @ 0x3A000F/0x3A001F) e o
 *     recorte vertical exato de overscan (32 linhas de tile só ~28
 *     visíveis): NÃO implementados ainda — banco de paleta fixo em 0,
 *     recorte assumido nas primeiras 28 linhas sem offset. TODO validar.
 */

#include "neocore/neogeo.h"
#include <string.h>

#define FIX_MAP_BASE   0x7000u
#define FIX_MAP_COLS   40u
#define FIX_TILE_PX    8u
#define FIX_TILE_BYTES 32u

/* Achado #18 (ver docs/ROADMAP.md): formato da word de paleta estava
 * ERRADO — tínhamos assumido 3 campos de 5 bits contíguos (bit15=dark,
 * bits14-10=R, bits9-5=G, bits4-0=B). O formato real, confirmado na NeoGeo
 * Development Wiki ("Colors"): `CrgbRRRRGGGGBBBB` — bit15=C(dark/comum),
 * bit14=r(LSB do vermelho), bit13=g(LSB do verde), bit12=b(LSB do azul),
 * bits11-8=RRRR (4 bits mais significativos do vermelho), bits7-4=GGGG,
 * bits3-0=BBBB. Ou seja, o LSB de cada componente de cor fica ISOLADO no
 * topo da word, longe dos outros 4 bits do mesmo componente — bem
 * diferente do que tínhamos. Achado depois de ver a tela de teste de cor
 * da BIOS (rótulos "RED"/"GREEN"/"BLUE"/"WHITE") mostrando caixas com as
 * cores erradas. */
/* Achado #57: a conversão de 5 bits pra 8 NÃO é linear, e o bit "dark" NÃO
 * é "metade do brilho".
 *
 * O DAC de vídeo do Neo-Geo é uma escada de resistores de 3900, 2200, 1000,
 * 470 e 220 ohms (bit0 -> bit4). O MAME modela exatamente isso
 * (`neogeo_base_state::create_rgb_lookups` em src/mame/snk/neogeo_v.cpp
 * chamando `compute_resistor_weights` de src/emu/video/resnet.cpp) e o bit
 * "dark" é um resistor de PULL-DOWN de 8200 ohms em paralelo — o que
 * escurece MUITO pouco, nada perto de dividir por dois. O "shadow" (bit0 do
 * latch de sistema e o pen de sombra dos sprites) é outro pull-down, de 150
 * ohms, esse sim quase pela metade.
 *
 * As quatro tabelas abaixo foram CALCULADAS com a mesma matemática do
 * resnet.cpp (mesmo autoscale a partir da rede sem pull-down), não
 * estimadas. Antes disso a nossa rampa linear errava até 5 níveis por
 * canal, o que aparecia como ~6400 pixels diferentes do MAME numa tela
 * estática que no resto estava pixel a pixel idêntica — a geometria estava
 * certa, a COR é que estava. */
static const u8 palette_lut[4][32] = {
    /* normal */
    {   0,   8,  14,  22,  30,  38,  44,  52,  65,  73,  79,  86,  95, 103, 109, 117,
      138, 146, 152, 160, 169, 176, 182, 190, 203, 211, 217, 225, 233, 241, 247, 255 },
    /* dark (pull-down de 8200) */
    {   0,   8,  14,  21,  30,  38,  44,  51,  64,  71,  77,  85,  94, 101, 107, 115,
      136, 144, 150, 158, 166, 174, 180, 188, 200, 208, 214, 221, 230, 238, 244, 251 },
    /* shadow (pull-down de 150) */
    {   0,   4,   8,  12,  17,  21,  25,  29,  36,  40,  44,  48,  53,  57,  61,  65,
       77,  81,  85,  89,  94,  98, 102, 106, 113, 117, 121, 125, 130, 134, 138, 142 },
    /* dark + shadow */
    {   0,   4,   8,  12,  17,  21,  24,  29,  36,  40,  43,  48,  53,  57,  60,  65,
       76,  81,  84,  88,  93,  98, 101, 105, 112, 116, 120, 124, 129, 133, 137, 141 }
};

/* Formato da word de paleta (Achado #18, confirmado também no MAME):
 * `CrgbRRRRGGGGBBBB` — bit15 = dark, bit14/13/12 = LSB de R/G/B,
 * bits11-8 = RRRR, bits7-4 = GGGG, bits3-0 = BBBB. */
void neogeo_color_to_rgb888_ex(u16 color, bool shadow, u8 *r, u8 *g, u8 *b) {
    u32 r5 = (u32)(((color >> 8) & 0x0Fu) << 1) | ((color >> 14) & 1u);
    u32 g5 = (u32)(((color >> 4) & 0x0Fu) << 1) | ((color >> 13) & 1u);
    u32 b5 = (u32)(((color >> 0) & 0x0Fu) << 1) | ((color >> 12) & 1u);
    u32 t = ((color >> 15) & 1u) | (shadow ? 2u : 0u);
    *r = palette_lut[t][r5];
    *g = palette_lut[t][g5];
    *b = palette_lut[t][b5];
}

void neogeo_color_to_rgb888(u16 color, u8 *r, u8 *g, u8 *b) {
    neogeo_color_to_rgb888_ex(color, false, r, g, b);
}

static inline u8 fix_tile_nibble(const neogeo_t *ng, u32 tile_code, u32 x, u32 y) {
    /* Achado #27 (ver docs/ROADMAP.md e comentário em neogeo.h sobre
     * `use_cart_fix`): a fix layer NÃO lê sempre da S ROM do cartucho — o
     * hardware real tem DUAS ROMs de fix graphics (a SFIX própria da BIOS e
     * a S1 do cartucho) e troca entre elas via um bit do latch de sistema
     * ($3A0000+, bit5). Sem isso, telas desenhadas pela BIOS (self-test,
     * memory card, CABINET/SLOT) que usam códigos de tile fora da faixa
     * ASCII básica liam gráficos do JOGO no lugar da fonte da BIOS —
     * gerando glifos completamente corrompidos (reportado visualmente pelo
     * usuário na tela "CABINET No./SLOT"). */
    const u8 *rom_s;
    u32 rom_s_size;
    if (ng->use_cart_fix || ng->rom.sfix_size == 0) {
        rom_s = ng->rom.s;
        rom_s_size = ng->rom.s_size;
    } else {
        rom_s = ng->rom.sfix;
        rom_s_size = ng->rom.sfix_size;
    }
    if (rom_s_size == 0) return 0;
    u32 tile_off = tile_code * FIX_TILE_BYTES;
    /* ver Achado #17 no topo do arquivo: ordem derivada do charlayout real
     * do MAME, não da wiki resumida. */
    u32 half = (x < 4u) ? 1u : 0u;         /* H: 0=metade direita(x>=4), 1=esquerda */
    u32 col_in_half = (x % 4u) / 2u;       /* C: 0 ou 1 dentro da metade */
    u32 byte_col = half * 2u + col_in_half; /* 0..3, ordem de armazenamento real */
    u32 byte_off = byte_col * 8u + y;
    u32 idx = (tile_off + byte_off) % rom_s_size; /* módulo: evita ler fora do buffer se o codigo de tile for invalido */
    u8 byte = rom_s[idx];
    /* Achado #28 (ver docs/ROADMAP.md): a ordem dos nibbles estava INVERTIDA.
     * O Achado #17 deduziu "x par = nibble alto" lendo o `charlayout` do
     * MAME — dedução errada. O código REAL de render da fix layer do MAME
     * (`neosprite_base_device::draw_fixed_layer_2pixels`, arquivo
     * `src/mame/snk/neogeo_spr.cpp`) é inequívoco: o primeiro pixel do par
     * (x PAR, o da esquerda) vem do nibble BAIXO e o segundo (x ÍMPAR) do
     * nibble ALTO:
     *     if (data & 0x0f) *pixel_addr = char_pens[data & 0x0f];
     *     pixel_addr++;
     *     if (data & 0xf0) *pixel_addr = char_pens[(data & 0xf0) >> 4];
     * Com a ordem trocada, cada PAR de pixels saía espelhado — glifo ainda
     * meio reconhecível, mas serrilhado/"com buracos" e com pixels soltos
     * do lado. Era exatamente o sintoma relatado nos Achados #22 e #27. */
    return (x & 1u) ? (u8)((byte >> 4) & 0xFu) : (u8)(byte & 0xFu); /* x par=nibble baixo, impar=alto */
}

/* Achado #68: versão por FAIXA de linhas (y1 exclusivo) — ver
 * neogeo_render_sprites_range. */
void neogeo_render_fix_layer_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1) {
    /* Não limpa o buffer: pixel transparente (nibble 0) mantém o que já
     * estava desenhado (sprites, se neogeo_render_frame chamou nessa ordem).
     * Quem quiser só a fix layer isolada (como o teste) deve limpar antes. */
    if (y1 > NEOGEO_SCREEN_H) y1 = NEOGEO_SCREEN_H;
    if (y0 >= y1) return;
    u32 row_first = y0 / FIX_TILE_PX;
    u32 row_last  = (y1 - 1u) / FIX_TILE_PX;   /* inclusivo */

    /* Achado #79: fix layer EM BANCO (garou, mslug3, kof2000). A S ROM desses
     * cartuchos tem 512KB (4 bancos de 0x1000 tiles), e o banco de cada tile
     * é escolhido por dados que o próprio jogo escreve na VRAM da fix layer.
     * Transcrito de `neosprite_base_device::draw_fixed_layer`:
     *   banked = m_fixed_layer_source && (addr_mask > 0x1ffff);
     *   GAROU:   tabela `garouoffsets` montada de $7500/$7580, e
     *            code += 0x1000 * (garouoffsets[(y - 2) & 31] ^ 3);
     *   KOF2000: code += 0x1000 * (((vram[0x7500 + ((y-1)&31) + 32*(x/6)]
     *                                >> (5 - (x%6))*2) & 3) ^ 3);
     * onde `y` é a linha CRUA do tilemap (scanline>>3). */
    const bool fix_banked = ng->use_cart_fix && ng->rom.s_size > 0x20000u && ng->rom.fix_bank_type != 0;
    int garouoffsets[34] = {0};
    if (fix_banked && ng->rom.fix_bank_type == 1) {
        int garoubank = 0, k = 0, yy = 0;
        while (yy < 32) {
            if (ng->vram[0x7500 + k] == 0x0200 && (ng->vram[0x7580 + k] & 0xff00) == 0xff00) {
                garoubank = ng->vram[0x7580 + k] & 3;
                garouoffsets[yy++] = garoubank;
            }
            garouoffsets[yy++] = garoubank;
            k += 2;
        }
    }

    for (u32 row = row_first; row <= row_last; row++) {
        for (u32 col = 0; col < FIX_MAP_COLS; col++) {
            /* column-major: 32 words alocados por coluna (só 28 visíveis) —
             * ver comentário do Achado #15 no topo do arquivo.
             *
             * Achado #47 (ver docs/ROADMAP.md): a linha do tilemap NÃO é a
             * linha da tela — é a linha de VARREDURA crua:
             *     video_data = &videoram[0x7000 | (scanline >> 3)];
             * (`neosprite_base_device::draw_fixed_layer`, fonte real do MAME).
             * Como a área visível começa na linha crua NEOGEO_VBEND (0x10=16,
             * confirmado em `neogeo_spr.h`), o topo da tela usa a linha
             * 16>>3 = 2 do tilemap, não a 0. Sem isso TODA a fix layer saía
             * 16px mais baixa E as linhas 28-29 (onde muitos jogos põem o
             * placar/"CREDIT", ex: o "LEVEL-4  CREDIT 00" do Double Dragon)
             * nunca eram desenhadas. Mesmo offset do Achado #44, que já tinha
             * corrigido o lado dos SPRITES. */
            u32 map_idx = FIX_MAP_BASE + col * 32u + row + (NEOGEO_VBEND_LINES / FIX_TILE_PX);
            if (map_idx >= NEOGEO_VRAM_WORDS) continue;
            u16 word = ng->vram[map_idx];
            u32 tile_code = word & 0x0FFFu;
            u32 palette_num = (word >> 12) & 0xFu;
            if (fix_banked) {
                int y = (int)(row + (NEOGEO_VBEND_LINES / FIX_TILE_PX)); /* linha crua */
                int x = (int)col;
                if (ng->rom.fix_bank_type == 1) {
                    tile_code += 0x1000u * (u32)(garouoffsets[(y - 2) & 31] ^ 3);
                } else {
                    u16 v = ng->vram[0x7500 + ((y - 1) & 31) + 32 * (x / 6)];
                    tile_code += 0x1000u * (u32)(((v >> ((5 - (x % 6)) * 2)) & 3) ^ 3);
                }
            }

            for (u32 y = 0; y < FIX_TILE_PX; y++) {
                u32 py = row * FIX_TILE_PX + y;
                if (py < y0 || py >= y1) continue;
                for (u32 x = 0; x < FIX_TILE_PX; x++) {
                    u32 px = col * FIX_TILE_PX + x;
                    if (px >= NEOGEO_SCREEN_W) continue;

                    u8 nibble = fix_tile_nibble(ng, tile_code, x, y);
                    if (nibble == 0) continue; /* transparente: mantem o fundo (preto por ora) */

                    u32 color_idx = palette_num * 16u + nibble;
                    if (color_idx >= NEOGEO_PALETTE_WORDS) continue;

                    u8 r, g, b;
                    neogeo_color_to_rgb888_ex(ng->palette[ng->palette_bank + color_idx],
                                              ng->screen_shadow != 0, &r, &g, &b);

                    u8 *px_out = out_rgb888 + (py * NEOGEO_SCREEN_W + px) * 3u;
                    px_out[0] = r; px_out[1] = g; px_out[2] = b;
                }
            }
        }
    }
}

void neogeo_render_fix_layer(const neogeo_t *ng, u8 *out_rgb888) {
    neogeo_render_fix_layer_range(ng, out_rgb888, 0u, NEOGEO_SCREEN_H);
}
