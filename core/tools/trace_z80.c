/* Trace instrução-por-instrução do lado Z80 (PC em hex, uma por linha),
 * pra comparar contra um trace real do MAME (audiocpu) e achar onde nosso
 * Z80/mapa de som diverge do hardware real — mesma metodologia usada com
 * sucesso do lado 68000 (ver docs/ROADMAP.md).
 *
 * Uso: trace_z80 <dir_cartucho> <dir_bios> <num_passos> [comando_68k]
 *   comando_68k (opcional, default 1): valor escrito em REG_SOUND antes de
 *   começar o trace, simulando o 68000 mandando esse comando pro Z80 (isso
 * dispara a NMI real, do jeito que aconteceria durante o boot).
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("uso: %s <dir_cartucho> <dir_bios> <num_passos> [comando_68k]\n", argv[0]);
        return 1;
    }
    int steps = atoi(argv[3]);
    u8 cmd = (argc > 4) ? (u8)atoi(argv[4]) : 1;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }

    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    /* Deixa o Z80 terminar a propria inicializacao (zera RAM, configura
     * pilha etc — visto no handler de reset em 0x00D0) antes de simular o
     * 68000 mandando um comando. Sem isso, a NMI interrompe o Z80 no meio
     * da inicializacao e corrompe tudo (achado depurando este teste). */
    Z80_Run(&ng.z80, 10000);
    printf("(pos-init: pc=%04X nmi_pending=%d halted=%d iff1=%d iff2=%d)\n",
           ng.z80.pc, ng.z80.nmi_pending, ng.z80.halted, ng.z80.iff1, ng.z80.iff2);

    /* Simula o 68000 escrevendo o comando em REG_SOUND: seta o latch e
     * dispara a NMI, igual ao reg_write16 faz de verdade. */
    ng.sound_latch_to_z80 = cmd;
    ng.sound_latch_pending = true;
    Z80_NMI(&ng.z80);
    printf("(pos-NMI: nmi_pending=%d)\n", ng.z80.nmi_pending);

    u8 last_reply = 0xFF;
    for (int i = 0; i < steps; i++) {
        u16 pc_before = ng.z80.pc;
        printf("%04X\n", pc_before);
        int used = Z80_Run(&ng.z80, 1);
        if (ng.sound_latch_to_68k != last_reply) {
            fprintf(stderr, "(passo %d: sound_latch_to_68k mudou pra %u em pc=%04X)\n",
                    i, ng.sound_latch_to_68k, ng.z80.pc);
            last_reply = ng.sound_latch_to_68k;
        }
        if (used <= 0) break;
    }

    neogeo_romset_free(&rom);
    return 0;
}
