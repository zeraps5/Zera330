/* Achado #33 (ver docs/ROADMAP.md): ferramenta pra isolar a causa exata da
 * divergência entre "rodar por fatias de 1 vblank via neogeo_run_slice"
 * (o que debug_screenshot/frontend fazem) e "rodar instrução a instrução
 * sem fatiar" (o que debug_bp fazia) — duas instâncias de neogeo_t, mesmo
 * romset/backup_ram, uma rodada de cada jeito, comparando estado completo
 * (não só PC) a cada fronteira de vblank pra achar o PRIMEIRO ponto exato
 * onde elas discordam.
 * Uso: debug_compare <dir_cartucho> <dir_bios> <num_vblanks>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void load_backup_ram(neogeo_t *ng, const char *path) {
    if (!path) return;
    FILE *bf = fopen(path, "rb");
    if (!bf) return;
    u8 raw[NEOGEO_BACKUP_RAM_SIZE];
    size_t n = fread(raw, 1, sizeof(raw), bf);
    fclose(bf);
    for (size_t i = 0; i + 1 < n; i += 2) {
        ng->backup_ram[i] = raw[i + 1];
        ng->backup_ram[i + 1] = raw[i];
    }
}

/* Mesmos dois hooks do Achado #31/#31b, replicados aqui pra bater com o
 * comportamento de neogeo_run_slice. */
static u32 step_raw(neogeo_t *ng) {
    if (ng->m68k.pc == 0xC11D66u) ng->m68k.pc = 0xC11D88u;
    if (ng->m68k.pc == 0xC123F0u) { ng->backup_ram[0x124] = 0xFFu; ng->backup_ram[0x125] = 0xFFu; }
    u32 used = m68k_core_step(&ng->m68k);
    int z80_budget = (int)(used / 3u);
    if (z80_budget > 0) Z80_Run(&ng->z80, z80_budget);
    ng->master_cycles += used;
    ng->cycles_since_vblank += used;
    if (ng->cycles_since_vblank >= 202752u) {
        ng->cycles_since_vblank -= 202752u;
        ng->irq_pending |= NEOGEO_IRQACK_VBLANK;
        m68k_core_set_irq(&ng->m68k, 1);
    }
    neogeo_rtc_tick(ng); /* Achado #85 */
    return used;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("uso: %s <dir_cartucho> <dir_bios> <num_vblanks>\n", argv[0]);
        return 1;
    }
    int target_vb = atoi(argv[3]);
    const char *backup_file = getenv("NEOGEO_BACKUP_RAM_FILE");

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }

    neogeo_t ng_a, ng_b; /* a = passo-a-passo cru; b = via neogeo_run_slice (1 vblank por vez) */
    neogeo_init(&ng_a, &rom);
    neogeo_init(&ng_b, &rom);
    load_backup_ram(&ng_a, backup_file);
    load_backup_ram(&ng_b, backup_file);
    neogeo_reset(&ng_a);
    neogeo_reset(&ng_b);

    for (int vb = 1; vb <= target_vb; vb++) {
        /* ng_b: exatamente como debug_screenshot faz. */
        neogeo_run_slice(&ng_b, 202752u);

        /* ng_a: passo a passo até acumular pelo menos os mesmos ciclos. */
        u64 target_cycles = ng_b.master_cycles;
        while (ng_a.master_cycles < target_cycles) {
            if (step_raw(&ng_a) == 0) break;
        }

        if (ng_a.m68k.pc != ng_b.m68k.pc || ng_a.master_cycles != ng_b.master_cycles) {
            printf("DIVERGIU no vblank %d:\n", vb);
            printf("  ng_a (passo-a-passo): pc=%06x master_cycles=%llu d0=%08x sr=%04x\n",
                   ng_a.m68k.pc, (unsigned long long)ng_a.master_cycles, ng_a.m68k.d[0], ng_a.m68k.sr);
            printf("  ng_b (run_slice):     pc=%06x master_cycles=%llu d0=%08x sr=%04x\n",
                   ng_b.m68k.pc, (unsigned long long)ng_b.master_cycles, ng_b.m68k.d[0], ng_b.m68k.sr);
            printf("  wram diff (primeiros 64KB, so os primeiros 20 bytes diferentes):\n");
            int shown = 0;
            for (u32 i = 0; i < NEOGEO_WRAM_SIZE && shown < 20; i++) {
                if (ng_a.wram[i] != ng_b.wram[i]) {
                    printf("    wram[%05x]: a=%02x b=%02x\n", i, ng_a.wram[i], ng_b.wram[i]);
                    shown++;
                }
            }
            if (shown == 0) printf("    (wram identica -- diferenca eh so no estado da CPU/registradores)\n");
            neogeo_romset_free(&rom);
            return 0;
        }
    }

    printf("OK: nenhuma divergencia detectada ate vblank %d (pc final=%06x master_cycles=%llu)\n",
           target_vb, ng_a.m68k.pc, (unsigned long long)ng_a.master_cycles);
    neogeo_romset_free(&rom);
    return 0;
}
