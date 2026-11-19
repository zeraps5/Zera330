/* Renderização de sprites do LSPC2 — Fase 4b.
 *
 * AVISO DE CONFIANÇA (leia antes de depurar um sprite errado na tela):
 * A estrutura GERAL (SCB1 = tabela de tiles por sprite, 64 words/sprite;
 * SCB2 = shrink; SCB3 = Y+altura+sticky; SCB4 = X) e o formato do C ROM
 * (16x16 4bpp, 128 bytes/tile, chip ímpar = planos 0-1, chip par = planos
 * 2-3) vêm da NeoGeo Development Wiki e têm confiança razoável. MAS os
 * detalhes finos abaixo foram reconstruídos e podem estar errados —
 * ainda não validados 100% contra uma imagem real:
 *   - [RESOLVIDO no Achado #45] Posição dos bits de paleta/flip/tile-alto na
 *     word ímpar do SCB1: os valores "assumidos" aqui (tile alto=bits3-0,
 *     vflip=bit5, hflip=bit4) estavam TODOS errados. Ver o comentário do
 *     Achado #45 no corpo de `neogeo_render_sprites`.
 *   - Shrink vertical: usamos uma aproximação LINEAR (fração v_shrink/255),
 *     não a tabela real do chip (a "L0 ROM" mencionada na wiki, que tem uma
 *     curva não-linear específica) — visualmente parecido, não bit-exato
 *   - Shrink horizontal: idem, aproximação linear em vez da tabela real de
 *     16 entradas do chip (zoom_x_tables no MAME) — TODO se precisar exatidão
 *
 * Achado #42 (ver docs/ROADMAP.md): comparando contra
 * `neosprite_base_device::draw_sprites` (fonte real do MAME, nao resumo),
 * achamos DOIS bugs de posicionamento que afetavam TODO sprite na tela
 * (nao só o logo) e a causa exata da "faixa esquerda corrompida" reportada
 * pelo usuário (tanto em Double Dragon quanto em Neo Bomberman) e do logo
 * "NEO·GEO" (feito de sprites ENCADEADOS) nunca aparecer:
 *   1. X: liamos `x_word & 0x1FF` (bits baixos da SCB4). O real é
 *      `x_word >> 7` (bits ALTOS, 9 bits a partir do bit 7).
 *   2. Y: usávamos `496 - raw_y`. O valor real é `512 (0x200) - raw_y`.
 *   3. Bit de encadeamento ("sticky", bit6 da SCB3/y_control): quando
 *      setado, o sprite NÃO tem X/Y/altura próprios — ele continua a partir
 *      do sprite anterior: `x = (x_anterior + zoom_x_anterior + 1) & 0x1FF`,
 *      só troca o zoom_x. Sem isso, todo sprite encadeado colapsava pra
 *      x=0 (dados de SCB4 de um sprite encadeado nem chegam a ser usados
 *      como posição de verdade no hardware real) — exatamente a faixa
 *      vertical esquerda cheia de lixo visual que aparecia sempre.
 * Isso exige processar os sprites em ORDEM (0..380) carregando um estado
 * (x/y/zoom_x/zoom_y/altura) que persiste entre sprites encadeados, em vez
 * de tratar cada sprite isoladamente.
 *
 * Achado #21 (ver docs/ROADMAP.md): a organização em "4 blocos 8x8 com
 * ordem TR,BR,TL,BL" (resumo em texto da wiki) estava ERRADA — usamos ela
 * antes e o cursor "►" do menu de teste de som saía visivelmente
 * quebrado/serrilhado, com um pixel solto do lado (reportado pelo
 * usuário rodando a janela SDL2 ao vivo). Conferimos contra o código de
 * renderização REAL do MAME (`neosprite_regular_device::draw_pixel` em
 * `src/mame/snk/neogeo_spr.cpp`, fonte primária, não resumo) e descobrimos
 * que NÃO existem 4 blocos com reordenação — são só DUAS METADES
 * (esquerda x0-7, direita x8-15), cada uma cobrindo as 16 linhas
 * INTEIRAS de forma contígua (sem dividir em topo/base). Cada linha
 * ocupa 2 bytes por chip (plano baixo, plano alto — igual ao Achado #16,
 * isso já estava certo), mas a metade direita começa 32 bytes DEPOIS da
 * esquerda (16 linhas × 2 bytes), não 16 bytes como um bloco 8x8 sozinho
 * sugeriria. Se/quando tivermos mais capturas de referência, comparar
 * pixel a pixel de novo.
 */

#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCB1_BASE 0x0000u
#define SCB1_WORDS_PER_SPRITE 64u
#define SCB2_BASE 0x8000u
#define SCB3_BASE 0x8200u
#define SCB4_BASE 0x8400u
#define NEOGEO_MAX_SPRITES 381u
#define TILE_BYTES_PER_PLANE_PAIR 64u /* por chip (odd ou even), por tile */

/* Decodifica o pixel (x,y) de um tile de 16x16, 4bpp, dado seu código.
 * Retorna 0-15 (0 = transparente). x,y já devem vir com flip aplicado por
 * quem chama (mais simples que espalhar o flip aqui). */
/* Achado #16 + #21 (ver docs/ROADMAP.md): dentro de cada metade (ver
 * aviso no topo do arquivo), os dois bitplanos de uma ROM (odd: planos
 * 0-1; even: planos 2-3) vêm INTERCALADOS por linha (2 bytes por linha:
 * plano baixo, depois plano alto) — isso já estava certo desde o Achado
 * #16. O que estava errado era a estrutura de "blocos": não são 4 blocos
 * 8x8 com reordenação, são 2 METADES (esquerda x0-7, direita x8-15) cada
 * uma cobrindo as 16 linhas inteiras — confirmado contra
 * `neosprite_regular_device::draw_pixel` (`neogeo_spr.cpp`) do MAME. */
static u8 sprite_tile_pixel(const neogeo_t *ng, u32 tile_code, u32 x, u32 y) {
    if (ng->rom.c_odd_size == 0 || ng->rom.c_even_size == 0) return 0;

    /* Achado #78: código de tile MAIOR que a C ROM carregada. O MAME não faz
     * `% tamanho_real` — ele arredonda a região PRA CIMA até a próxima
     * potência de 2 e mascara com ela (`get_region_mask`:
     * "get mask based on the length rounded up to the nearest power of 2"),
     * deixando a sobra ZERADA. Então o código dá a volta na potência de 2, e
     * o pedaço que cai além do ROM de verdade sai TRANSPARENTE — não repete
     * outro tile, como o `%` fazia. Só muda alguma coisa quando o conjunto de
     * C ROMs não é potência de 2 (ex.: 3 x 512KB = 1.5MB no maglord); quando é
     * (a maioria), `%` e `&` dão o mesmo resultado. */
    u32 tiles_real = ng->rom.c_odd_size / TILE_BYTES_PER_PLANE_PAIR;
    u32 tiles_pow2 = 1u;
    while (tiles_pow2 < tiles_real) tiles_pow2 <<= 1;
    u32 tile_idx = tile_code & (tiles_pow2 - 1u);
    if (tile_idx >= tiles_real) return 0; /* sobra do arredondamento: zeros */
    u32 tile_off = tile_idx * TILE_BYTES_PER_PLANE_PAIR;
    u32 odd_base = tile_off;
    u32 even_base = tile_off;
    if (even_base + TILE_BYTES_PER_PLANE_PAIR > ng->rom.c_even_size) return 0;

    /* Achado #43 (ver docs/ROADMAP.md): validado byte-a-byte contra um
     * snapshot REAL do MAME (`mame -debug -debugscript ... ; snap`),
     * decodificando os bytes crus em Python com hipóteses alternativas até
     * bater pixel-a-pixel com o "NEO·GEO" real. Dois bugs aqui:
     * 1) METADE trocada: a metade ESQUERDA da tela (x<8) usa a SEGUNDA
     *    metade dos 64 bytes do tile (bytes 32-63), não a primeira.
     *    Confirmado contra `neosprite_optimized_device::optimize_helper`
     *    (fonte real do MAME) — os offsets 0x40-0x7F (2a metade) alimentam
     *    os primeiros 8 pixels de saída (tela esquerda).
     * 2) Ordem de bit: LSB primeiro (bit = x&7), não MSB (7-(x&7)) — mesma
     *    fonte, `BIT(byte, x)` com x=0..7 sendo o próprio índice do pixel. */
    u32 half = (x < 8u) ? 1u : 0u; /* esquerda=1(2a metade), direita=0(1a metade) */
    u32 bit = x & 7u; /* LSB primeiro */

    u32 low_off  = half * 32u + y * 2u; /* y de 0-15 inteiro, sem dividir em topo/base */
    u32 high_off = low_off + 1u;

    u8 odd_low  = ng->rom.c_odd[odd_base + low_off];
    u8 odd_high = ng->rom.c_odd[odd_base + high_off];
    u8 even_low = ng->rom.c_even[even_base + low_off];
    u8 even_high= ng->rom.c_even[even_base + high_off];

    u8 p0 = (u8)((odd_low  >> bit) & 1u);
    u8 p1 = (u8)((odd_high >> bit) & 1u);
    u8 p2 = (u8)((even_low >> bit) & 1u);
    u8 p3 = (u8)((even_high>> bit) & 1u);

    return (u8)(p0 | (p1 << 1) | (p2 << 2) | (p3 << 3));
}

/* Achado #68: render por FAIXA DE LINHAS. `y0`/`y1` delimitam as linhas de
 * tela a desenhar (y1 exclusivo). O render de frame inteiro é só o caso
 * y0=0, y1=224 — mas com a faixa dá pra desenhar linha a linha, intercalado
 * com a CPU, que é como o hardware realmente funciona (ver
 * neogeo_run_frame_render em neogeo.c). */
void neogeo_render_sprites_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1) {
    /* Achado #53 (ver docs/ROADMAP.md): `getenv()` estava sendo chamado
     * dentro do laço mais interno — por PIXEL, até ~1.3 milhões de vezes por
     * frame (381 sprites x até 224 linhas x até 16 colunas). Isso sozinho
     * derrubava o emulador de ~59fps pra uma fração disso. É o MESMO erro já
     * documentado antes com `getenv()` dentro de `neogeo_run_slice` (ver
     * Achado sobre desempenho no histórico) — reintroduzido aqui sem
     * perceber. Lidos UMA VEZ por frame agora. */
    const bool dbg_dump_range = getenv("NEOGEO_DUMP_RANGE") != NULL;
    const char *dbg_dump_range_val = dbg_dump_range ? getenv("NEOGEO_DUMP_RANGE") : NULL;
    const bool dbg_dump_chain = getenv("NEOGEO_DUMP_CHAIN") != NULL;
    const bool dbg_graypal = getenv("NEOGEO_DEBUG_GRAYPAL") != NULL;

    /* Achado #64: LIMITE DE 96 SPRITES POR LINHA DE VARREDURA.
     *
     * O LSPC monta, a cada linha, uma lista com no máximo 96 sprites — os 96
     * PRIMEIROS em ordem de índice que tocam aquela linha; do 97º em diante
     * simplesmente não são desenhados. É o famoso "sprite flicker" do
     * Neo-Geo: os jogos alternam a ordem dos sprites entre frames pra que o
     * que sobra pisque em vez de sumir de vez. Sem esse limite, cenas
     * cheias aparecem MAIS completas que no hardware real — e o efeito de
     * piscar que o jogo programou some, o que muda o visual.
     *
     * Fonte (`neosprite_base_device::parse_sprites`, neogeo_spr.cpp):
     *     for (sprite_number = 0; sprite_number < 381; sprite_number++) {
     *         y_control = videoram[0x8200 | sprite_number];
     *         if (BIT(~y_control, 6)) { y = 0x200 - (y_control >> 7);
     *                                   rows = y_control & 0x3f; }
     *         if (rows == 0) continue;
     *         if (!sprite_on_scanline(scanline, y, rows)) continue;
     *         ...
     *         if (++active_sprite_count == 96) break;
     *     }
     *
     * Como o nosso render é de frame inteiro (não linha a linha), fazemos a
     * mesma varredura ANTES, uma vez por linha visível, e guardamos o índice
     * do último sprite aceito naquela linha (`line_last_sprite`). Custo:
     * 224*381 iterações baratinhas por frame. */
    s32 line_last_sprite[NEOGEO_SCREEN_H];
    {
        /* Chave de depuração: NEOGEO_SPRITE_LIMIT=0 desliga o corte de 96
         * por linha, pra isolar se um sprite sumido é culpa do limite ou do
         * resto do render. */
        static int limit_cached = -1;
        if (limit_cached < 0) {
            const char *e = getenv("NEOGEO_SPRITE_LIMIT");
            limit_cached = (e && *e) ? atoi(e) : 96;
            if (limit_cached <= 0) limit_cached = (int)NEOGEO_MAX_SPRITES;
        }
        const u32 per_line_limit = (u32)limit_cached;
        for (u32 row = 0; row < NEOGEO_SCREEN_H; row++) line_last_sprite[row] = -1;
        for (u32 row = y0; row < y1; row++) {
            /* `scanline` do MAME é a linha CRUA de varredura. */
            s32 scanline = (s32)row + (s32)NEOGEO_VBEND_LINES;
            u32 count = 0;
            s32 py = 0; u32 prows = 0;
            for (u32 s = 0; s < NEOGEO_MAX_SPRITES; s++) {
                u16 yc = ng->vram[SCB3_BASE + s];
                if (((yc >> 6) & 1u) == 0u) {
                    py = 0x200 - (s32)((yc >> 7) & 0x1FFu);
                    prows = yc & 0x3Fu;
                }
                if (prows == 0u) continue;
                bool on = (prows >= 0x20u) ||
                          ((u32)((scanline - py) & 0x1FF) < prows * 0x10u);
                if (!on) continue;
                line_last_sprite[row] = (s32)s;
                if (++count == per_line_limit) break;
            }
        }
    }

    /* Achado #42: estado que persiste entre sprites encadeados (bit sticky). */
    s32 cur_x = 0, cur_y = 0;
    u32 cur_zoom_x = 0, cur_zoom_y = 0, cur_rows = 0;

    for (u32 s = 0; s < NEOGEO_MAX_SPRITES; s++) {
        u16 y_word = ng->vram[SCB3_BASE + s];       /* SCB3: y + altura + sticky */
        u16 shrink_word = ng->vram[SCB2_BASE + s];  /* SCB2: zoom x/y */

        u32 chained = (y_word >> 6) & 1u;
        if (chained) {
            /* Achado #42: sem X/Y/altura próprios — continua do anterior. */
            cur_x = (cur_x + (s32)cur_zoom_x + 1) & 0x1FF;
            cur_zoom_x = (shrink_word >> 8) & 0xFu;
        } else {
            /* Achado #44 (ver docs/ROADMAP.md): falta subtrair o offset de
             * NEOGEO_VBEND (16 linhas de vblank antes do início da área
             * visível) — o "y" do MAME é a linha de varredura CRUA (0..VTOTAL),
             * não a linha já recortada pra nossa tela 0-223. Confirmado
             * comparando contra um snapshot real do MAME (mame -debug + snap):
             * nosso "NEO·GEO" aparecia 16px mais baixo que o real. */
            cur_y = 0x200 - (s32)((y_word >> 7) & 0x1FFu) - (s32)NEOGEO_VBEND_LINES;
            u16 x_word = ng->vram[SCB4_BASE + s];
            cur_x = (s32)(x_word >> 7) & 0x1FF; /* Achado #42: bits altos, nao baixos */
            cur_zoom_y = shrink_word & 0xFFu;
            cur_zoom_x = (shrink_word >> 8) & 0xFu;
            cur_rows = y_word & 0x3Fu;
        }

        {
            const char *dr = dbg_dump_range_val;
            if (dr) {
                unsigned a=0,b=0; sscanf(dr, "%u-%u", &a, &b);
                if (s >= a && s <= b) {
                    printf("s=%u ch=%u x=%d y=%d zx=%u zy=%u rows=%u tiles=",
                           s, chained, cur_x, cur_y, cur_zoom_x, cur_zoom_y, cur_rows);
                    u16 wb = (u16)(s * SCB1_WORDS_PER_SPRITE);
                    for (u32 r = 0; r < cur_rows && r < 8u; r++) {
                        u16 ew = ng->vram[SCB1_BASE + wb + (u16)(r*2u)];
                        u16 at = ng->vram[SCB1_BASE + wb + (u16)(r*2u+1u)];
                        u32 tc = ew | (((u32)at << 12) & 0xF0000u);
                        printf("[%05x at=%04x] ", tc, at);
                    }
                    printf("\n");
                }
            }
        }
        if (dbg_dump_chain && s >= 19 && s <= 36) {
            printf("s=%u chained=%u cur_x=%d cur_zoom_x=%u cur_y=%d cur_rows=%u tiles=",
                   s, chained, cur_x, cur_zoom_x, cur_y, cur_rows);
            u16 wb = (u16)(s * SCB1_WORDS_PER_SPRITE);
            for (u32 r = 0; r < cur_rows && r < 8u; r++) {
                u16 ew = ng->vram[SCB1_BASE + wb + (u16)(r*2u)];
                u16 ow = ng->vram[SCB1_BASE + wb + (u16)(r*2u+1u)];
                u32 tc = ew | (((u32)ow & 0xFu) << 16);
                printf("[%05x pal=%u vf=%u hf=%u] ", tc, (ow>>8)&0xFFu, (ow>>5)&1u, (ow>>4)&1u);
            }
            printf("\n");
        }
        /* Achado #51 (ver docs/ROADMAP.md): sprite com `rows >= 0x20` (32-63)
         * aparece em TODAS as linhas de varredura — é assim que os jogos
         * fazem FUNDO de tela cheia. Regra vinda de
         * `neosprite_base_device::sprite_on_scanline`:
         *     return (rows == 0) || (rows >= 0x20) ||
         *            ((scanline - y) & 0x1ff) < (rows * 0x10);
         * ATENÇÃO: o `rows == 0` dessa função NUNCA dispara na prática —
         * quem monta a lista de sprites por linha (`parse_sprites`, a seleção
         * de verdade do hardware) descarta antes:
         *     if (rows == 0) continue;          // "skip sprites with 0 rows"
         * Tentei tratar `rows == 0` como "todas as linhas" e encheu a tela de
         * título do Double Dragon de lixo — conferido contra o MAME. */
        u32 height_tiles = cur_rows;
        if (height_tiles == 0u) continue;             /* parse_sprites: descarta */
        bool all_scanlines = (height_tiles >= 0x20u); /* sprite de fundo/tela cheia */
        if (all_scanlines) height_tiles = 32u;        /* a SCB1 tem 32 entradas de tile */

        /* Faixa 0x140-0x1F0 (320-496): totalmente fora da tela (mesma
         * checagem do MAME); fora dela, valores >0x1F0 sao wraparound
         * (sprite parcialmente visivel saindo pela esquerda). */
        if (cur_x >= 0x140 && cur_x <= 0x1F0) continue;
        s32 x_pos = (cur_x > 0x1F0) ? (cur_x - 512) : cur_x;
        s32 y_pos = cur_y;

        u32 h_shrink = cur_zoom_x;
        u32 v_shrink = cur_zoom_y;

        u32 full_h_px = height_tiles * 16u;
        u32 full_w_px = 16u;

        /* Fatores de escala como fracao (numerador/denominador) pra evitar
         * float aqui (nao essencial, mas mantem consistente com o resto do
         * core que e tudo inteiro). */
        u32 rend_h_px = (full_h_px * (v_shrink + 1u)) / 256u;
        u32 rend_w_px = (full_w_px * (h_shrink + 1u)) / 16u;
        if (dbg_dump_chain && s >= 19 && s <= 22) {
            printf("  s=%u x_pos=%d y_pos=%d v_shrink=%u h_shrink=%u full_h=%u rend_h=%u rend_w=%u\n",
                   s, x_pos, y_pos, v_shrink, h_shrink, full_h_px, rend_h_px, rend_w_px);
        }
        if (rend_h_px == 0 || rend_w_px == 0) continue;

        u16 word_base = (u16)(s * SCB1_WORDS_PER_SPRITE);

        /* Achado #50: com a ROM de zoom o hardware varre `rows*16` linhas e é
         * a própria tabela que decide onde a imagem "acaba" (o índice de tile
         * que ela devolve passa de `rows`) — é assim que o sprite encolhido
         * ocupa menos linhas na tela. Sem a ROM, caímos na aproximação antiga
         * e varremos só a altura já encolhida. */
        const bool have_zoomy = (ng->rom.zoomy_size >= 0x10000u);

        /* Achado #51 (posição) + #53 (ver docs/ROADMAP.md, desempenho): o
         * sprite precisa aparecer na linha de tela certa considerando o
         * wraparound de Y em 512 linhas (`sprite_on_scanline` do MAME) — mas
         * variar `screen_yu` de 0 a 223 PRA CADA SPRITE (achado #51 original)
         * gasta até 381*224 iterações por frame só de `continue`, a maioria
         * jogada fora, e foi isso que derrubou o fps de ~59 pra ~15-27.
         * `all_scanlines` (fundo de tela cheia) precisa mesmo varrer as 224
         * linhas — é raro (poucos sprites por frame). Os OUTROS (a imensa
         * maioria) têm uma altura visível pequena e conhecida; resolvemos o
         * wraparound testando só os 2 deslocamentos que podem colocar o
         * sprite dentro da tela (y_pos e y_pos-512 — y_pos já está limitado a
         * uns -15..496 pelo Achado #44, então nunca precisa de mais que 2) e
         * iterando só o intervalo de linhas que cai dentro de 0..223. */
        u32 visible_lines = all_scanlines ? 512u : (have_zoomy ? full_h_px : rend_h_px);
        s32 y_offsets[2] = { y_pos, y_pos - 512 };
        int n_offsets = all_scanlines ? 1 : 2; /* fundo cheio: um "off" já cobre tudo */
        if (all_scanlines) y_offsets[0] = 0; /* varre a tela inteira direto */

        for (int oi = 0; oi < n_offsets; oi++) {
            s32 off = y_offsets[oi];
            s32 sy_lo = (off < 0) ? -off : 0;
            s32 sy_hi = (s32)NEOGEO_SCREEN_H - off;
            if (sy_hi > (s32)visible_lines) sy_hi = (s32)visible_lines;
            if (sy_lo >= sy_hi) continue;

        for (s32 sy_s = sy_lo; sy_s < sy_hi; sy_s++) {
            u32 screen_yu = (u32)(off + sy_s);
            if (screen_yu < y0 || screen_yu >= y1) continue; /* fora da faixa (Achado #68) */
            /* Achado #64: além das 96 primeiras da linha, o hardware não
             * desenha (ver a varredura de `line_last_sprite` no topo). */
            if ((s32)s > line_last_sprite[screen_yu]) continue;
            /* Achado #77: `sy` é o `sprite_line` do MAME —
             *     const int sprite_line = (scanline - y) & 0x1ff;
             * ou seja, a linha da TELA menos o Y do sprite, com volta em 512.
             * No caminho normal `sy_s` já é isso (o laço de `y_offsets` sai de
             * `y_pos`). No caminho `all_scanlines` (sprite de fundo, rows>=32)
             * a gente varria com `off = 0`, o que fazia `sy` virar a linha da
             * TELA e ignorar o Y do sprite por completo — e aí o índice de
             * tile saía todo errado. Era por isso que fundo de tela cheia
             * sumia (ou virava listra) em vários jogos. */
            u32 sy = all_scanlines ? (u32)(((s32)screen_yu - y_pos) & 0x1FF)
                                   : (u32)sy_s;

            u32 tile_row, y_in_tile;
            if (have_zoomy) {
                /* Curva REAL do zoom vertical, da ROM "000-lo.lo" (região
                 * "spritegen:zoomy" no MAME). Mesma lógica de
                 * `neosprite_base_device::draw_sprites`:
                 *     zoom_line = sprite_line & 0xff;
                 *     invert    = BIT(sprite_line, 8);
                 *     if (invert) zoom_line ^= 0xff;
                 *     u8 v = zoomy[(zoom_y << 8) | zoom_line];
                 *     sprite_y = v & 0x0f;  tile = v >> 4;
                 *     if (invert) { sprite_y ^= 0x0f; tile ^= 0x1f; }
                 * O "invert" é como o chip cobre sprites de mais de 16 tiles:
                 * a tabela só chega no tile 15, e as linhas 256-511 espelham
                 * pra cobrir os tiles 16-31. */
                u32 zoom_line = sy & 0xFFu;
                u32 invert = (sy >> 8) & 1u;
                if (invert) zoom_line ^= 0xFFu;
                /* Achado #77: dobra em "vai e volta" pra sprite mais alto que
                 * 32 tiles, direto do `draw_sprites` do MAME:
                 *     if (rows > 0x20) {
                 *         zoom_line = zoom_line % ((zoom_y + 1) << 1);
                 *         if (zoom_line > zoom_y) {
                 *             zoom_line = ((zoom_y + 1) << 1) - 1 - zoom_line;
                 *             invert = !invert;
                 *         }
                 *     }
                 * Usa `cur_rows` CRU (33, 34...), não o valor já limitado a 32. */
                if (cur_rows > 0x20u) {
                    u32 period = (v_shrink + 1u) << 1;
                    zoom_line %= period;
                    if (zoom_line > v_shrink) {
                        zoom_line = period - 1u - zoom_line;
                        invert = !invert;
                    }
                }
                u8 v = ng->rom.zoomy[((v_shrink & 0xFFu) << 8) | zoom_line];
                y_in_tile = (u32)(v & 0x0Fu);
                tile_row  = (u32)(v >> 4);
                if (invert) { y_in_tile ^= 0x0Fu; tile_row ^= 0x1Fu; }
                /* Achado #108: NÃO descartar tile >= rows. O `draw_sprites` do
                 * MAME só testa `sprite_on_scanline` (linha < rows*16) e lê a
                 * entrada da SCB1 que a tabela de zoom mandar, seja qual for.
                 * Com zoom vertical a tabela devolve tiles além de `rows` nas
                 * últimas linhas — descartá-los cortava a base de sprites
                 * encolhidos (logo SNK do aof3). */
            } else {
                /* Sem a ROM de zoom: aproximação linear (comportamento antigo). */
                u32 src_y = (sy * full_h_px) / rend_h_px;
                if (src_y >= full_h_px) src_y = full_h_px - 1u;
                tile_row = src_y / 16u;
                y_in_tile = src_y % 16u;
            }

            if (dbg_dump_range_val) {
                unsigned a=0,b=0; sscanf(dbg_dump_range_val, "%u-%u", &a, &b);
                if (s >= a && s <= b && screen_yu < 48u) {
                    printf("  [row %3u] sy=%3u tile_row=%2u y_in_tile=%2u\n",
                           screen_yu, sy, tile_row, y_in_tile);
                }
            }
            u16 even_w = ng->vram[SCB1_BASE + word_base + (u16)(tile_row * 2u)];
            u16 attr   = ng->vram[SCB1_BASE + word_base + (u16)(tile_row * 2u + 1u)];
            /* Achado #45 (ver docs/ROADMAP.md): os bits do "attr" (word ÍMPAR
             * da SCB1) estavam TODOS errados menos a paleta — eram os valores
             * "assumidos, nao validados" que o aviso no topo deste arquivo
             * marcava. Confirmado contra `neosprite_base_device::draw_sprites`
             * (fonte real do MAME):
             *   code = ((attr << 12) & 0xf0000) | <word par>   -> bits 4-7!
             *   if (BIT(attr, 3)) auto-animacao de 3 bits
             *   else if (BIT(attr, 2)) auto-animacao de 2 bits
             *   if (BIT(attr, 1)) vflip
             *   if (BIT(attr, 0)) hflip
             *   paleta = attr >> 8                              (essa ja estava certa) */
            u32 tile_code = even_w | (((u32)attr << 12) & 0xF0000u);
            u32 palette = (attr >> 8) & 0xFFu;
            u32 vflip = (attr >> 1) & 1u;
            u32 hflip = attr & 1u;

            /* Auto-animacao: o hardware substitui os bits BAIXOS do codigo do
             * tile por um contador que anda sozinho a cada N frames (usado por
             * fogo, agua, luzes piscando...). Sem isso, esses sprites ficam
             * travados num quadro so — ou, pior, com os bits 2-3 do attr
             * entrando no codigo do tile como se fossem endereco. */
            if (!ng->auto_anim_disabled) {
                if ((attr >> 3) & 1u)      tile_code = (tile_code & ~0x07u) | (ng->auto_anim_counter & 0x07u);
                else if ((attr >> 2) & 1u) tile_code = (tile_code & ~0x03u) | (ng->auto_anim_counter & 0x03u);
            }

            u32 py = vflip ? (15u - y_in_tile) : y_in_tile;

            /* Achado #50 (parte horizontal): o chip NÃO escala na horizontal
             * por regra de três — ele usa uma máscara de 16 bits que diz
             * QUAIS das 16 colunas de origem são desenhadas. Tabela copiada
             * de `zoom_x_tables[]` (`neogeo_spr.cpp`, "verified from real
             * hardware"). O destino só avança quando o bit está setado, então
             * a largura final = quantidade de bits 1 = zoom_x+1. */
            static const u16 zoom_x_tables[16] = {
                0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
                0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff
            };
            u16 zx_tbl = zoom_x_tables[h_shrink & 0xFu];
            u32 dst_x = 0;
            for (u32 col = 0; col < 16u; col++) {
                if (!((zx_tbl >> (15u - col)) & 1u)) continue; /* coluna descartada pelo shrink */
                u32 sx = dst_x++;
                s32 screen_x = x_pos + (s32)sx;
                if (screen_x < 0 || screen_x >= (s32)NEOGEO_SCREEN_W) continue;

                u32 px = hflip ? (15u - col) : col;

                u8 nibble = sprite_tile_pixel(ng, tile_code, px, py);
                if (nibble == 0) continue; /* transparente */

                u32 color_idx = palette * 16u + nibble;
                if (color_idx >= NEOGEO_PALETTE_WORDS) continue;

                u8 r, g, b;
                /* NEOGEO_DEBUG_GRAYPAL=1: ignora a paleta e pinta o índice do
                 * pixel (0-15) em escala de cinza — serve pra separar
                 * "decode do tile errado" de "cor/paleta errada". */
                if (dbg_graypal) {
                    r = g = b = (u8)(nibble * 17u);
                } else {
                    neogeo_color_to_rgb888_ex(ng->palette[ng->palette_bank + color_idx],
                                              ng->screen_shadow != 0, &r, &g, &b);
                }

                u8 *out = out_rgb888 + (screen_yu * NEOGEO_SCREEN_W + (u32)screen_x) * 3u;
                out[0] = r; out[1] = g; out[2] = b;
            }
        }
        } /* fecha for(oi...) — os 2 deslocamentos de wraparound de Y */
    }
}

void neogeo_render_sprites(const neogeo_t *ng, u8 *out_rgb888) {
    neogeo_render_sprites_range(ng, out_rgb888, 0u, NEOGEO_SCREEN_H);
}

void neogeo_render_frame_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1) {
    if (y1 > NEOGEO_SCREEN_H) y1 = NEOGEO_SCREEN_H;
    if (y0 >= y1) return;

    /* Achado #63: o fundo NÃO é preto — é a cor do último pen do banco de
     * paleta ativo ("backdrop"). No MAME (`neogeo_base_state::set_pens` e
     * `screen_update`): `m_bg_pen = pen_base + 0xfff;` seguido de
     * `bitmap.fill(*m_bg_pen, cliprect)`. Jogos que usam fundo colorido
     * (menus, telas de transição, fades) ficavam com moldura preta em
     * volta do que os sprites cobriam. */
    u8 bg_r, bg_g, bg_b;
    neogeo_color_to_rgb888_ex(ng->palette[ng->palette_bank + 0xFFFu],
                              ng->screen_shadow != 0, &bg_r, &bg_g, &bg_b);
    for (u32 y = y0; y < y1; y++) {
        for (u32 x = 0; x < NEOGEO_SCREEN_W; x++) {
            u8 *p = out_rgb888 + (y * NEOGEO_SCREEN_W + x) * 3u;
            p[0] = bg_r; p[1] = bg_g; p[2] = bg_b;
        }
    }
    /* DEBUG TEMPORARIO: isolar camadas pra achar se o cursor "flecha"
     * vem de sprite ou fix layer (ver Achado #21/#22, docs/ROADMAP.md).
     * NEOGEO_DEBUG_LAYER=sprites|fix|both (default: both) */
    const char *layer = getenv("NEOGEO_DEBUG_LAYER");
    if (!layer || strcmp(layer, "fix") != 0) neogeo_render_sprites_range(ng, out_rgb888, y0, y1);
    if (!layer || strcmp(layer, "sprites") != 0) neogeo_render_fix_layer_range(ng, out_rgb888, y0, y1);
}

void neogeo_render_frame(const neogeo_t *ng, u8 *out_rgb888) {
    neogeo_render_frame_range(ng, out_rgb888, 0u, NEOGEO_SCREEN_H);
}
