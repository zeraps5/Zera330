/* Acha o momento EXATO em que a execução sai do código do cartucho e cai na
 * BIOS (sintoma clássico de reset/exceção). Roda até o vblank N, depois passa
 * a executar instrução a instrução guardando um histórico circular de PCs;
 * quando o PC entra na faixa da BIOS ($C00000+) vindo do cartucho, imprime o
 * histórico — é ele que mostra QUAL instrução pulou pra lá.
 * Uso: debug_jump <dir_cartucho> <dir_bios> <vblank_inicial> <max_instrucoes>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>

#define HIST 24

static u32 g_lo = 0xC00000u, g_hi = 0xFFFFFFu;
static bool is_bios(u32 pc) { return pc >= g_lo && pc <= g_hi; }

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <vblank_inicial> <max_instrucoes> [alvo_lo_hex] [alvo_hi_hex]\n", argv[0]);
        return 1;
    }
    long start_vb = atol(argv[3]);
    long max_instr = atol(argv[4]);
    if (argc >= 6) g_lo = (u32)strtoul(argv[5], NULL, 16);
    if (argc >= 7) g_hi = (u32)strtoul(argv[6], NULL, 16); else if (argc >= 6) g_hi = g_lo;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    for (long i = 0; i < start_vb; i++) neogeo_run_until_vblank(&ng);
    printf("apos %ld vblanks: pc=%06x\n", start_vb, ng.m68k.pc);

    u32 hist[HIST];
    u32 sp_hist[HIST];
    int n = 0;
    u32 prev_pc = ng.m68k.pc;
    /* NEOGEO_TRACE_FILE=<arq>: grava um PC por linha (hex, 6 dígitos), pra
     * comparar com `trace arq,maincpu,noloop` do debugger do MAME. */
    FILE *cyc_file = getenv("NEOGEO_TRACE_CYC") ? fopen(getenv("NEOGEO_TRACE_CYC"), "w") : NULL;
    FILE *tf = getenv("NEOGEO_TRACE_FILE") ? fopen(getenv("NEOGEO_TRACE_FILE"), "w") : NULL;
    extern FILE *z80_trace_file;
    if (getenv("NEOGEO_TRACE_Z80")) z80_trace_file = fopen(getenv("NEOGEO_TRACE_Z80"), "w");

    for (long i = 0; i < max_instr; i++) {
        u32 pc = ng.m68k.pc;
        /* NEOGEO_WATCH_A7=1: para na 1ª instrução que deixa a pilha fora da
         * WRAM ($100000-$10FFFF), mostrando a instrução anterior. */
        if (getenv("NEOGEO_WATCH_A7")) {
            static u32 prev_a7 = 0, prev_pc2 = 0;
            u32 a7 = ng.m68k.a[7];
            if (i > 0 && (a7 < 0x100000u || a7 > 0x110000u)) {
                printf("A7 FORA DA WRAM na instrucao %ld: a7 %08x -> %08x, instrucao anterior pc=%06x (agora pc=%06x) sr=%04x master=%llu\n",
                       i, prev_a7, a7, prev_pc2, pc, ng.m68k.sr, (unsigned long long)ng.master_cycles);
                for (int r = 0; r < 8; r++) printf("  d%d=%08x a%d=%08x\n", r, ng.m68k.d[r], r, ng.m68k.a[r]);
                return 0;
            }
            prev_a7 = a7; prev_pc2 = pc;
        }
        if (tf) fprintf(tf, "%06X\n", pc);
        if (cyc_file) { /* NEOGEO_TRACE_CYC: PC + ciclos gastos por instrução */
            u64 c0 = ng.master_cycles;
            neogeo_run_slice(&ng, 1);
            fprintf(cyc_file, "%06X %llu %llu\n", pc, (unsigned long long)(ng.master_cycles - c0),
                    (unsigned long long)c0);
            prev_pc = pc;

            continue;
        }
        hist[n % HIST] = pc;
        sp_hist[n % HIST] = ng.m68k.a[7];
        n++;
        if (is_bios(pc) && !is_bios(prev_pc)) {
            printf("ENTROU NA BIOS na instrucao %ld: pc=%06x (vindo de %06x)\n", i, pc, prev_pc);
            printf("historico (mais antigo -> mais novo):\n");
            int start = (n > HIST) ? (n - HIST) : 0;
            for (int k = start; k < n; k++)
                printf("  pc=%06x sp=%06x\n", hist[k % HIST], sp_hist[k % HIST]);
            neogeo_romset_free(&rom);
            return 0;
        }
        u8 prev_vec = ng.m68k.last_exception_vector;
        u32 prev_vpc = ng.m68k.last_exception_pc;
        prev_pc = pc;
        neogeo_run_slice(&ng, 1);
        if (ng.m68k.last_exception_vector != prev_vec || ng.m68k.last_exception_pc != prev_vpc) {
            printf("EXCECAO vetor=%u pc=%06x opcode=%04x (instrucao %ld, pc atual=%06x)\n",
                   ng.m68k.last_exception_vector, ng.m68k.last_exception_pc,
                   ng.m68k.last_exception_opcode, i, ng.m68k.pc);
            for (int r = 0; r < 8; r++) printf("  d%d=%08x a%d=%08x\n", r, ng.m68k.d[r], r, ng.m68k.a[r]);
            printf("historico antes da excecao:\n");
            int st = (n > HIST) ? (n - HIST) : 0;
            for (int k = st; k < n; k++) printf("  pc=%06x sp=%06x\n", hist[k % HIST], sp_hist[k % HIST]);
        }
    }
    printf("nao entrou na BIOS em %ld instrucoes (pc=%06x)\n", max_instr, ng.m68k.pc);
    printf("historico final (mais antigo -> mais novo):\n");
    {
        int st = (n > HIST) ? (n - HIST) : 0;
        for (int k = st; k < n; k++) printf("  pc=%06x sp=%06x\n", hist[k % HIST], sp_hist[k % HIST]);
    }
    for (int r = 0; r < 8; r++) printf("  d%d=%08x a%d=%08x\n", r, ng.m68k.d[r], r, ng.m68k.a[r]);
    if (tf) fclose(tf);
    if (z80_trace_file) fclose(z80_trace_file);
    neogeo_romset_free(&rom);
    return 0;
}
