/* Ferramenta headless de screenshot: roda o sistema real (neogeo_run_slice)
 * até uma contagem de vblank dada e salva o framebuffer renderizado como BMP
 * — sem depender do frontend SDL2 (que tem se mostrado instável/lento com
 * NEOGEO_MAX_FRAMES grande neste ambiente, ver docs/ROADMAP.md). Escreve o
 * BMP com a mesma rotina usada em platform/sdl2/main.c.
 * Uso: debug_screenshot <dir_cartucho> <dir_bios> <num_vblanks> <saida.bmp>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_bmp(const char *path, const u8 *rgb888, u32 w, u32 h) {
    u32 row_bytes = ((w * 3u + 3u) & ~3u);
    u32 img_size = row_bytes * h;
    u32 file_size = 54u + img_size;
    FILE *f = fopen(path, "wb");
    if (!f) return;

    u8 header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    memcpy(&header[2], &file_size, 4);
    u32 offset = 54;
    memcpy(&header[10], &offset, 4);
    u32 info_size = 40;
    memcpy(&header[14], &info_size, 4);
    memcpy(&header[18], &w, 4);
    s32 signed_h = (s32)h;
    memcpy(&header[22], &signed_h, 4);
    u16 planes = 1, bpp = 24;
    memcpy(&header[26], &planes, 2);
    memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &img_size, 4);
    fwrite(header, 1, 54, f);

    u8 pad[3] = {0};
    for (u32 y = 0; y < h; y++) {
        u32 src_row = h - 1u - y; /* BMP e bottom-up */
        const u8 *row = rgb888 + (size_t)src_row * w * 3u;
        for (u32 x = 0; x < w; x++) {
            u8 bgr[3] = { row[x * 3u + 2u], row[x * 3u + 1u], row[x * 3u] };
            fwrite(bgr, 1, 3, f);
        }
        u32 pad_bytes = row_bytes - w * 3u;
        if (pad_bytes) fwrite(pad, 1, pad_bytes, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <num_vblanks> <saida.bmp> [press_from_vblank]\n", argv[0]);
        printf("  press_from_vblank: opcional — a partir desse vblank, simula A+B+C\n");
        printf("  pressionados (REG_P1CNT) ate o final, pra destravar telas de\n");
        printf("  self-test da BIOS que esperam essa combinacao (Achado #19).\n");
        return 1;
    }
    int target_vb = atoi(argv[3]);
    int press_from_vb = (argc >= 6) ? atoi(argv[5]) : -1;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);

    const char *backup_file = getenv("NEOGEO_BACKUP_RAM_FILE");
    if (backup_file) {
        FILE *bf = fopen(backup_file, "rb");
        if (bf) {
            u8 raw[NEOGEO_BACKUP_RAM_SIZE];
            size_t n = fread(raw, 1, sizeof(raw), bf);
            fclose(bf);
            for (size_t i = 0; i + 1 < n; i += 2) {
                ng.backup_ram[i] = raw[i + 1];
                ng.backup_ram[i + 1] = raw[i];
            }
        }
    }

    printf("backup_ram[0x10..0x20)=");
    for (int k = 0x10; k < 0x20; k++) printf("%02x ", ng.backup_ram[k]);
    printf("\n");
    printf("backup_ram[0x47]=%02x (limite do scan em C11D5E)\n", ng.backup_ram[0x47]);
    printf("backup_ram[0x124..0x134)=");
    for (int k = 0x124; k < 0x134; k++) printf("%02x ", ng.backup_ram[k]);
    printf("\n");

    neogeo_reset(&ng);

    const char *hold = getenv("NEOGEO_HOLD_BUTTONS");
    if (hold) ng.p1_buttons = (u8)strtoul(hold, NULL, 16);

    /* Achado #20 (ver docs/ROADMAP.md): experimento — forcar
     * backup_ram[0x124..0x125] != 0 bem no inicio do boot (checagem real
     * acontece por volta da instrucao ~5.5 milhoes, dentro do primeiro
     * vblank) pra ver se isso evita a entrada no self-test, igual (a
     * gente acha) o MAME faz com o mesmo arquivo de save. */
    if (getenv("NEOGEO_FORCE_BKRAM_NONZERO")) {
        u32 done = 0;
        while (done < 202752u * 3u) { /* ~3 vblanks de margem */
            ng.backup_ram[0x124] = 0x00u;
            ng.backup_ram[0x125] = 0x82u;
            u32 d = neogeo_run_slice(&ng, 1000u);
            done += d;
            if (d == 0) break;
        }
    }

    /* Achado #26 (ver docs/ROADMAP.md): a tela "CABINET No./SLOT" exige
     * as 4 direções (cima/baixo/esquerda/direita) TODAS marcadas em
     * wram[$10FEEA] (bea,A5) bits4-7 pra confirmar — mas segurar as 4 de
     * uma vez leva a um estado de erro depois (parece um "watchdog kick"
     * infinito). Testando: cada direção parece marcar seu próprio bit de
     * forma "sticky" (persiste mesmo depois de soltar) — então apertamos
     * uma de cada vez em sequência, soltando entre elas, igual um
     * jogador de verdade testando o analógico. */
    /* Lista separada por vírgula de vblanks em que confirmar CABINET/SLOT
     * (4 direções por 5 vblanks) — útil quando o boot reinicia sozinho e
     * mostra a tela de novo (ver Achado #35, docs/ROADMAP.md). */
    const char *seq = getenv("NEOGEO_DIRECTION_SEQUENCE");
    int seq_starts[16]; int n_seq_starts = 0;
    if (seq) {
        char buf[256]; snprintf(buf, sizeof(buf), "%s", seq);
        char *tok = strtok(buf, ",");
        while (tok && n_seq_starts < 16) { seq_starts[n_seq_starts++] = atoi(tok); tok = strtok(NULL, ","); }
    }

    /* Navegação de menu (BIOS operator menu): N toques discretos de DOWN
     * (aperta-solta, não segurado) a partir de um vblank dado, seguidos de
     * um toque de A pra confirmar a opção selecionada. */
    const char *menu_start_s = getenv("NEOGEO_MENU_START");
    const char *menu_taps_s = getenv("NEOGEO_MENU_DOWN_TAPS");
    int menu_start = menu_start_s ? atoi(menu_start_s) : -1;
    int menu_taps = menu_taps_s ? atoi(menu_taps_s) : 0;
    /* Achado #36 (ver docs/ROADMAP.md): simula P1 START — agora no lugar
     * certo (REG_STATUS_B bit0, ativo-baixo), via
     * `ng.debug_force_start_frames`. `NEOGEO_START_FROM_VB` = vblank do
     * primeiro toque; `NEOGEO_START_REPEAT` (opcional) = de quantos em
     * quantos vblanks repetir o toque (simula jogador insistindo no
     * START). Cada toque segura por 10 vblanks e solta. */
    const char *start_s = getenv("NEOGEO_START_FROM_VB");
    int start_from_vb = start_s ? atoi(start_s) : -1;
    const char *start_rep_s = getenv("NEOGEO_START_REPEAT");
    int start_repeat = start_rep_s ? atoi(start_rep_s) : 0;
    /* Achado #34: mesma ideia, mas simulando moeda (COIN1, STATUS_A bit0) —
     * sem crédito, START sozinho não faz o jogo começar de verdade. */
    const char *coin_s = getenv("NEOGEO_COIN_FROM_VB");
    int coin_from_vb = coin_s ? atoi(coin_s) : -1;
    /* Cada "toque" ocupa uma janela de PRESS_LEN pressionado + REL_LEN solto. */
    const int TAP_PRESS_LEN = 10, TAP_REL_LEN = 20, TAP_LEN = 30;

    for (int i = 0; i < target_vb; i++) {
        if (press_from_vb >= 0 && i >= press_from_vb - 30 && i < press_from_vb + 60) {
            /* Simulacao mais realista de aperta-e-solta (nao "grudado" pra
             * sempre): solto antes, pressionado por uma janela curta,
             * solto de novo depois — ver Achado #19 no docs/ROADMAP.md. */
            bool pressing = (i >= press_from_vb && i < press_from_vb + 10);
            ng.p1_buttons = pressing ? 0x8Fu : 0xFFu;
            ng.wram[0xF396] = 0x70u; /* (a96,A5): sempre "solto" p/ satisfazer entrada */
            ng.wram[0xF395] = pressing ? 0x00u : 0x70u; /* (a95,A5): borda real */
        }
        for (int s = 0; s < n_seq_starts; s++) {
            int seq_start = seq_starts[s];
            if (seq_start >= 0 && i >= seq_start && i < seq_start + 5) {
                /* segura as 4 direcoes por so 5 vblanks (borda de confirmacao),
                 * depois solta de vez — ver se isso evita o "watchdog kick"
                 * visto ao segurar constante (Achado #26). */
                ng.p1_buttons = 0xF0u;
            } else if (seq_start >= 0 && i == seq_start + 5) {
                ng.p1_buttons = 0xFFu;
            }
        }
        if (menu_start >= 0 && i >= menu_start) {
            int rel = i - menu_start;
            int total_taps_window = menu_taps * TAP_LEN;
            if (rel < total_taps_window) {
                int in_tap = rel % TAP_LEN;
                ng.p1_buttons = (in_tap < TAP_PRESS_LEN) ? 0xFDu /* DOWN */ : 0xFFu;
            } else {
                int rel2 = rel - total_taps_window;
                ng.p1_buttons = (rel2 < TAP_PRESS_LEN) ? 0xEFu /* A */ : 0xFFu;
            }
        }
        /* CORREÇÃO: (a97,A5) é deslocamento $0A97 (3 dígitos, não 2) — offset
         * de wram correto é $FD97, não $F397 (erro da primeira tentativa,
         * ver docs/ROADMAP.md Achado #32). */
        const char *force_idx_s = getenv("NEOGEO_FORCE_IDX_AT_1927E");
        if (force_idx_s && ng.m68k.pc == 0xC19282u) {
            int idxval = atoi(force_idx_s);
            ng.wram[0xFCD8] = (u8)(idxval >> 8);
            ng.wram[0xFCD9] = (u8)idxval;
            printf("[force_idx] disparou no vblank %d, idx forcado pra %d\n", i, idxval);
        }
        /* CORREÇÃO (ver docs/ROADMAP.md, Achado #32): a versão anterior
         * forçava o bit por uma JANELA DE VBLANK checada aqui fora — não
         * confiável, porque um "slice" inteiro de vblank (202752 ciclos)
         * roda a instrução de poll de `$C190E2` num instante arbitrário lá
         * dentro, sem garantia de bater. Agora só armamos o campo
         * `debug_force_start_frames` (o hook de verdade, por PC exato,
         * mora dentro de `neogeo_run_slice`, em `neogeo.c`). */
        const char *hold_env = getenv("NEOGEO_HOLD_LEN");
        int hold_len = hold_env ? atoi(hold_env) : 10;
        if (start_from_vb >= 0 && i >= start_from_vb) {
            if (i == start_from_vb ||
                (start_repeat > 0 && ((i - start_from_vb) % start_repeat) == 0)) {
                ng.debug_force_start_frames = (u32)hold_len;
            }
        }
        if (coin_from_vb >= 0 && i == coin_from_vb) {
            ng.debug_force_coin_frames = (u32)hold_len;
        }
        /* NEOGEO_INPUTS="de-ate:coin,sys,p1,p2;..." (hex, ativo-baixo): segura
         * essas entradas nos vblanks [de, ate]. Ex.: moeda 2 = "700-709:FD,FF,FF,FF". */
        {
            const char *in = getenv("NEOGEO_INPUTS");
            u8 coin = 0xFFu, sys = 0xFFu, p1 = 0xFFu, p2 = 0xFFu;
            while (in && *in) {
                int a, b; unsigned c, s, x, y;
                if (sscanf(in, "%d-%d:%x,%x,%x,%x", &a, &b, &c, &s, &x, &y) == 6 && i >= a && i <= b) {
                    coin &= (u8)c; sys &= (u8)s; p1 &= (u8)x; p2 &= (u8)y;
                }
                in = strchr(in, ';');
                if (in) in++;
            }
            if (getenv("NEOGEO_INPUTS")) {
                ng.coin_buttons = coin; ng.sys_buttons = sys; ng.p1_buttons = p1; ng.p2_buttons = p2;
            }
        }
        neogeo_run_until_vblank(&ng); /* Achado #54 */
    }

    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3u];
    /* NEOGEO_SCANLINE=1: render linha a linha (Achado #68) — desenha o
     * ÚLTIMO frame intercalando CPU e varredura, em vez de tirar uma foto
     * do estado no vblank. */
    if (getenv("NEOGEO_SCANLINE"))
        neogeo_run_frame_render(&ng, fb);
    else
        neogeo_render_frame(&ng, fb);
    write_bmp(argv[4], fb, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
    printf("ok: %d vblanks, salvo em %s (pc=%06x) a5=%06x wram[f395]=%02x wram[f396]=%02x idx=%02x%02x master_cycles=%llu\n",
           target_vb, argv[4], ng.m68k.pc, ng.m68k.a[5], ng.wram[0xF395], ng.wram[0xF396],
           ng.wram[0xFCD8], ng.wram[0xFCD9], (unsigned long long)ng.master_cycles);
    printf("Achado #27 debug: use_cart_fix=%u rom.sfix_size=%u rom.s_size=%u\n",
           ng.use_cart_fix, ng.rom.sfix_size, ng.rom.s_size);

    /* DEBUG TEMPORARIO: dump da vizinhanca do fix map em torno da area do
     * cursor "flecha" do teste de som (col~11-14, row~10-13), pra achar o
     * bug real (ver Achado #22, docs/ROADMAP.md). */
    if (getenv("NEOGEO_DUMP_FIXMAP")) {
        int row_lo = 4, row_hi = 14, col_lo = 4, col_hi = 26;
        const char *rows_env = getenv("NEOGEO_DUMP_FIXMAP_ROWS");
        const char *cols_env = getenv("NEOGEO_DUMP_FIXMAP_COLS");
        if (rows_env) sscanf(rows_env, "%d,%d", &row_lo, &row_hi);
        if (cols_env) sscanf(cols_env, "%d,%d", &col_lo, &col_hi);
        for (int row = row_lo; row < row_hi; row++) {
            printf("row%2d: ", row);
            for (int col = col_lo; col < col_hi; col++) {
                u32 idx2 = 0x7000u + (u32)col * 32u + (u32)row;
                u16 w = ng.vram[idx2];
                u32 code = w & 0x0FFFu;
                char c = (code >= 0x20 && code < 0x7F) ? (char)code : '.';
                printf("%04x(%c) ", w, c);
            }
            printf("\n");
        }
    }

    if (getenv("NEOGEO_DUMP_SPRITES")) {
        int nonzero_count = 0;
        for (int s = 0; s < 381; s++) {
            u16 scb3 = ng.vram[0x8200 + s];
            if ((scb3 & 0x3Fu) != 0) {
                nonzero_count++;
                if (nonzero_count <= 20) {
                    u16 scb4 = ng.vram[0x8400 + s];
                    u16 scb2 = ng.vram[0x8000 + s];
                    u16 scb1_0 = ng.vram[s * 64];
                    u16 scb1_1 = ng.vram[s * 64 + 1];
                    printf("sprite[%d]: altura=%u y_raw=%u x=%u shrink=%04x tile0=%04x/%04x\n",
                           s, scb3 & 0x3Fu, (scb3 >> 7) & 0x1FFu, scb4 & 0x1FFu, scb2, scb1_0, scb1_1);
                }
            }
        }
        printf("total de sprites com altura!=0: %d\n", nonzero_count);
    }

    if (getenv("NEOGEO_DUMP_TILE")) {
        u32 tile_code = (u32)strtoul(getenv("NEOGEO_DUMP_TILE"), NULL, 16);
        u32 tile_off = tile_code * 32u;
        /* Achado #27: respeita a mesma escolha SFIX-da-BIOS/S1-do-cartucho
         * usada de verdade pelo render (ver lspc_fix.c), senão este dump
         * mente sobre qual ROM está sendo lida. */
        const u8 *rom_s = (ng.use_cart_fix || ng.rom.sfix_size == 0) ? ng.rom.s : ng.rom.sfix;
        u32 rom_s_size = (ng.use_cart_fix || ng.rom.sfix_size == 0) ? ng.rom.s_size : ng.rom.sfix_size;
        printf("tile %03x (fonte=%s) bytes brutos: ", tile_code, (rom_s == ng.rom.sfix) ? "BIOS SFIX" : "cart S1");
        for (u32 k = 0; k < 32u; k++) {
            u32 idx3 = (tile_off + k) % rom_s_size;
            printf("%02x ", rom_s[idx3]);
        }
        printf("\n");
        printf("tile %03x nibbles (grade 8x8, formula Achado #17):\n", tile_code);
        for (u32 y = 0; y < 8u; y++) {
            printf("  y=%u: ", y);
            for (u32 x = 0; x < 8u; x++) {
                u32 half = (x < 4u) ? 1u : 0u;
                u32 col_in_half = (x % 4u) / 2u;
                u32 byte_col = half * 2u + col_in_half;
                u32 byte_off = byte_col * 8u + y;
                u32 idx3 = (tile_off + byte_off) % rom_s_size;
                u8 byte = rom_s[idx3];
                /* Achado #28: x par = nibble BAIXO, x impar = ALTO. */
                u8 nib = (x & 1u) ? (u8)((byte >> 4) & 0xFu) : (u8)(byte & 0xFu);
                printf("%x", nib);
            }
            printf("\n");
        }
    }

    if (getenv("NEOGEO_DUMP_PALETTE")) {
        for (int bank = 0; bank < 3; bank++) {
            printf("paleta banco %d: ", bank);
            for (int c = 0; c < 16; c++) printf("%04x ", ng.palette[bank * 16 + c]);
            printf("\n");
        }
    }

    /* NEOGEO_DUMP_PAL_ROW=<indice>: dump cru (banco ATIVO) de uma linha de
     * paleta especifica (16 cores), pra investigar flicker/"fantasma". */
    const char *pal_row_s = getenv("NEOGEO_DUMP_PAL_ROW");
    if (pal_row_s) {
        int row = atoi(pal_row_s);
        printf("paleta linha %d (banco ativo=%u): ", row, ng.palette_bank ? 1u : 0u);
        for (int c = 0; c < 16; c++) printf("%04x ", ng.palette[ng.palette_bank + (u32)row * 16 + (u32)c]);
        printf("\n");
    }

    /* NEOGEO_DUMP_PAL_FULL=<arquivo>: despeja o banco ATIVO inteiro (4096
     * words) em binario, pra diff entre dois frames com um script externo. */
    const char *pal_full_path = getenv("NEOGEO_DUMP_PAL_FULL");
    if (pal_full_path) {
        FILE *pf = fopen(pal_full_path, "wb");
        if (pf) {
            fwrite(&ng.palette[ng.palette_bank], sizeof(u16), NEOGEO_PALETTE_WORDS, pf);
            fclose(pf);
        }
    }

    neogeo_romset_free(&rom);
    return 0;
}
