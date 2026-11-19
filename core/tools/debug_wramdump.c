/* Dump de WRAM completa em checkpoints fixos, pra comparar byte a byte
 * contra dumps equivalentes do MAME real (via `save` no debugger) e achar
 * o vblank EXATO onde nossa execução diverge (ver Achado #20/#23,
 * docs/ROADMAP.md — bpset/wpset com ação não funciona neste MAME, então
 * usamos só dump de memória, que é confiável).
 * Uso: debug_wramdump <dir_cartucho> <dir_bios> <prefixo_saida>
 * Gera <prefixo>_0001.bin, <prefixo>_0002.bin, etc, um por checkpoint.
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("uso: %s <dir_cartucho> <dir_bios> <prefixo_saida>\n", argv[0]);
        return 1;
    }
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

    neogeo_reset(&ng);

    int checkpoints[] = {300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800};
    int ncp = (int)(sizeof(checkpoints) / sizeof(checkpoints[0]));
    int done_vb = 0;
    for (int i = 0; i < ncp; i++) {
        int target = checkpoints[i];
        while (done_vb < target) {
            neogeo_run_slice(&ng, 202752u);
            done_vb++;
        }
        u16 idx = (u16)((ng.wram[0xFCD8] << 8) | ng.wram[0xFCD9]);
        printf("vblank=%d idx=%04x pc=%06x\n", target, idx, ng.m68k.pc);
    }

    neogeo_romset_free(&rom);
    return 0;
}
