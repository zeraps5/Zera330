/* Gera um trace instrução-por-instrução (só PC, uma por linha) pra
 * comparar diretamente contra um trace real do MAME (ver
 * third_party/mame/trace_uni.log) e achar onde nossa execução diverge da
 * do hardware real. Uso: trace_boot <dir_cartucho> <dir_bios> <num_passos>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("uso: %s <dir_cartucho> <dir_bios> <num_passos>\n", argv[0]);
        return 1;
    }
    int steps = atoi(argv[3]);

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

    u32 vblank_acc = 0;
    for (int i = 0; i < steps; i++) {
        u32 pc_before = ng.m68k.pc;
        u32 used = m68k_core_step(&ng.m68k);
        printf("%06X\n", pc_before);

        /* mesma lógica de vblank/z80 do neogeo_run_slice, só que passo a
         * passo (pra não perder o efeito do IRQ periódico durante o trace) */
        vblank_acc += used;
        ng.master_cycles += used;
        ng.cycles_since_vblank += used;
        if (ng.cycles_since_vblank >= 202752u) {
            ng.cycles_since_vblank -= 202752u;
            ng.irq_pending |= NEOGEO_IRQACK_VBLANK;
            m68k_core_set_irq(&ng.m68k, 1);
            ng.vblank_count_for_rtc++;
            if (ng.vblank_count_for_rtc >= 30u) {
                ng.vblank_count_for_rtc = 0;
                ng.rtc_pulse ^= 1u;
            }
        }
        if (used == 0) break;
    }

    neogeo_romset_free(&rom);
    return 0;
}
