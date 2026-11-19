/* Ferramenta de comparação direta: roda o sistema real (neogeo_run_slice)
 * até um conjunto fixo de contagens de vblank e imprime o índice da máquina
 * de estados de boot (wram[$FCD8..$FCD9], relativo a A5=$10F300 -> endereço
 * absoluto $10FCD8) e flags relacionadas — pensado pra comparar linha a
 * linha com os dumps de WRAM reais tirados do MAME (ver docs/ROADMAP.md).
 * Uso: debug_checkpoints <dir_cartucho> <dir_bios>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("uso: %s <dir_cartucho> <dir_bios>\n", argv[0]);
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

    int checkpoints[] = {200,300,500,800,1100,1500,2000,3000,4000,4500,5000,6000,8000,10000};
    int ncp = (int)(sizeof(checkpoints) / sizeof(checkpoints[0]));
    int done_vb = 0;
    clock_t t_prev = clock();
    for (int i = 0; i < ncp; i++) {
        int target = checkpoints[i];
        while (done_vb < target) {
            neogeo_run_slice(&ng, 202752u); /* NEOGEO_CYCLES_PER_FRAME, definido em neogeo.c */
            done_vb++;
        }
        clock_t t_now = clock();
        double secs = (double)(t_now - t_prev) / CLOCKS_PER_SEC;
        t_prev = t_now;
        u16 idx = (u16)((ng.wram[0xFCD8] << 8) | ng.wram[0xFCD9]);
        printf("vblank=%d idx=%04x bdd=%02x bdc=%02x a94=%02x a9a=%02x pc=%06x z80_pc=%04x dt=%.2fs\n",
               target, idx, ng.wram[0xFEDD], ng.wram[0xFEDC], ng.wram[0xFD94],
               ng.wram[0xFD9A], ng.m68k.pc, ng.z80.pc, secs);
        fflush(stdout);
    }

    neogeo_romset_free(&rom);
    return 0;
}
