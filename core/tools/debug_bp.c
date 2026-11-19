/* Breakpoint de verdade: roda 1 instrução por vez (sem perder nenhuma, ao
 * contrário das ferramentas anteriores que amostravam por intervalo de
 * ciclos) e para assim que o PC bater num dos endereços-alvo, imprimindo o
 * estado completo. Réplica a lógica de timing de vblank/Z80 da
 * neogeo_run_slice real, só que passo a passo.
 * Uso: debug_bp <dir_cartucho> <dir_bios> <max_instrucoes> <endereco_hex...>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <max_instrucoes> <endereco_hex> [mais_enderecos...]\n", argv[0]);
        return 1;
    }
    long max_instr = atol(argv[3]);

    u32 targets[16];
    int n_targets = 0;
    for (int i = 4; i < argc && n_targets < 16; i++) {
        targets[n_targets++] = (u32)strtoul(argv[i], NULL, 16);
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

    /* EXPERIMENTO Achado #31 (ver docs/ROADMAP.md): testando se aumentar o
     * limite de scan em backup_ram[0x47] (lido em $C11D5E) faz o boot achar
     * a entrada nao-zero que c11db0 desloca pra offset 0x12C, tomando o
     * atalho $C11D88 (jmp c11028) que pula self-test E CABINET/SLOT. */
    const char *scanlimit = getenv("NEOGEO_HACK_SCANLIMIT");
    if (scanlimit) ng.backup_ram[0x47] = (u8)strtoul(scanlimit, NULL, 0);
    const char *entry124 = getenv("NEOGEO_HACK_ENTRY124");
    if (entry124) {
        u32 v = (u32)strtoul(entry124, NULL, 0);
        ng.backup_ram[0x124] = (u8)(v >> 8);
        ng.backup_ram[0x125] = (u8)v;
    }

    const char *hold = getenv("NEOGEO_HOLD_BUTTONS");
    if (hold) ng.p1_buttons = (u8)strtoul(hold, NULL, 16);

    u32 prev_pcs[64] = {0};
    int prev_idx = 0;
    u8 prev_fe8c = ng.wram[0xFE8C];
    int fe8c_changes = 0;
    for (long i = 0; i < max_instr; i++) {
        if (getenv("NEOGEO_TRACE_FE8C") && ng.wram[0xFE8C] != prev_fe8c) {
            fe8c_changes++;
            if (fe8c_changes <= 40) {
                printf("[fe8c] instrucao %ld: %02x -> %02x (pc=%06x master_cycles=%llu)\n",
                       i, prev_fe8c, ng.wram[0xFE8C], ng.m68k.pc, (unsigned long long)ng.master_cycles);
                fflush(stdout);
            }
            prev_fe8c = ng.wram[0xFE8C];
        }
        /* Achados #31/#31b REVERTIDOS aqui também (ver docs/ROADMAP.md,
         * Achado #35) — levavam pro subsistema de self-test/diagnóstico,
         * não pro jogo. */
        static int force_start_hits = 0;
        const char *force_idx_s = getenv("NEOGEO_FORCE_IDX_AT_1927E");
        if (force_idx_s && ng.m68k.pc == 0xC19282u) {
            int idxval = atoi(force_idx_s);
            ng.wram[0xFCD8] = (u8)(idxval >> 8);
            ng.wram[0xFCD9] = (u8)idxval;
        }
        if (getenv("NEOGEO_FORCE_START") && ng.m68k.pc == 0xC190E2u) {
            /* CORREÇÃO: (a97,A5) é deslocamento $0A97 (3 dígitos), não $97 —
             * endereço absoluto = A5($10F300)+$A97 = $10FD97, offset de wram
             * (mascarado em 16 bits) = $FD97, não $F397 (erro da primeira
             * tentativa, ver docs/ROADMAP.md Achado #32). */
            ng.wram[0xFD97] |= 0x10u;
            force_start_hits++;
            if (force_start_hits <= 3) printf("[force_start] hit #%d, wram[0xFD97] agora = %02x\n", force_start_hits, ng.wram[0xFD97]);
        } /* Achado #32: experimento */
        u32 pc = ng.m68k.pc;
        for (int t = 0; t < n_targets; t++) {
            if (pc == targets[t]) {
                u16 ret_hi = ng.m68k.read16(ng.m68k.user, ng.m68k.a[7]);
                u16 ret_lo = ng.m68k.read16(ng.m68k.user, ng.m68k.a[7] + 2u);
                u32 ret_addr = ((u32)ret_hi << 16) | ret_lo;
                printf("BREAKPOINT em %06X na instrucao %ld: d0=%08X d1=%08X a5=%06X a7=%06X sr=%04X endereco_retorno(topo_pilha)=%06X master_cycles=%llu\n",
                       pc, i, ng.m68k.d[0], ng.m68k.d[1], ng.m68k.a[5], ng.m68k.a[7], ng.m68k.sr, ret_addr,
                       (unsigned long long)ng.master_cycles);
                printf("  regs: ");
                for (int r = 0; r < 8; r++) printf("d%d=%08X ", r, ng.m68k.d[r]);
                for (int r = 0; r < 8; r++) printf("a%d=%08X ", r, ng.m68k.a[r]);
                printf("p_bank_offset=%06X\n", ng.p_bank_offset);
                printf("  PCs anteriores (mais antigo primeiro): ");
                for (int k = 0; k < 64; k++) printf("%06X ", prev_pcs[(prev_idx + k) % 64]);
                printf("\n");
                printf("  backup_ram[0x120..0x130)=");
                for (int k = 0x120; k < 0x130; k++) printf("%02x ", ng.backup_ram[k]);
                printf("\n");
                printf("  backup_ram[0x40..0x50)=");
                for (int k = 0x40; k < 0x50; k++) printf("%02x ", ng.backup_ram[k]);
                printf("\n");
                /* Achado #38: a comparação de data em $C11E0A — (ad2,A5) é a
                 * data lida do RTC, $D00104 é a data do último boot. */
                printf("  RTC (ad2,A5)=wram[FDD2..FDD8)=");
                for (int k = 0xFDD2; k < 0xFDD8; k++) printf("%02x ", ng.wram[k]);
                printf(" | backup_ram[0x104..0x10A)=");
                for (int k = 0x104; k < 0x10A; k++) printf("%02x ", ng.backup_ram[k]);
                printf("\n");
                printf("  wram[0xFD00..0xFD10) (A3)=");
                for (int k = 0xFD00; k < 0xFD10; k++) printf("%02x ", ng.wram[k]);
                printf("\n  d4=%08x wram[bea/beb=FEEA/FEEB]=%02x %02x p1_buttons=%02x use_cart_vectors=%u wram[FE8C]=%02x\n",
                       ng.m68k.d[4], ng.wram[0xFEEA], ng.wram[0xFEEB], ng.p1_buttons,
                       ng.use_cart_vectors, ng.wram[0xFE8C]);
                fflush(stdout);
            }
        }
        prev_pcs[prev_idx % 64] = pc;
        prev_idx++;

        ng.m68k.host_time = ng.master_cycles; ng.m68k.vpa_sync = 1u; /* Achado #101 */
        u32 used = m68k_core_step(&ng.m68k);

        int z80_budget = (int)(used / 3u);
        if (z80_budget > 0) Z80_Run(&ng.z80, z80_budget);

        ng.master_cycles += used;
        ng.cycles_since_vblank += used;
        if (ng.cycles_since_vblank >= 202752u) {
            ng.cycles_since_vblank -= 202752u;
            ng.irq_pending |= NEOGEO_IRQACK_VBLANK;
            m68k_core_set_irq(&ng.m68k, 1);
        }
        /* Achado #33 (ver docs/ROADMAP.md): esta ferramenta usava a lógica
         * VELHA de RTC (achado #14, contagem de 30 vblanks) em vez da real
         * baseada em ciclos (Achado #24, `rtc_next_toggle_cycles`) que
         * `neogeo_run_slice` (o caminho usado por `debug_screenshot` e o
         * frontend) já usa — causava STATUS_A bit6 divergir aos poucos e
         * explicava por que esta ferramenta (loop manual) e
         * `debug_screenshot` (via `neogeo_run_slice`) chegavam em PCs
         * DIFERENTES pro mesmo número de vblanks, um mistério em aberto
         * desde o início da investigação de boot. Corrigido pra usar a
         * mesma lógica. */
        neogeo_rtc_tick(&ng); /* Achado #85 */
        if (used == 0) { printf("CPU parou (used=0) na instrucao %ld\n", i); break; }
    }

    printf("\nFim: %ld instrucoes executadas, pc final=%06X master_cycles=%llu\n", max_instr, ng.m68k.pc,
           (unsigned long long)ng.master_cycles);
    neogeo_romset_free(&rom);
    return 0;
}
