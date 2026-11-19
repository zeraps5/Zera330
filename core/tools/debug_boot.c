/* Ferramenta de diagnóstico (não faz parte da suite de testes): carrega um
 * romset real e roda um número de frames, imprimindo o estado da CPU e
 * pontos-chave de VRAM/paleta a cada passo, pra descobrir POR QUE a tela
 * fica preta em vez de adivinhar. Uso:
 *   debug_boot <dir_cartucho> <dir_bios> [frames]
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("uso: %s <dir_cartucho> <dir_bios> [frames]\n", argv[0]);
        return 1;
    }
    int frames = (argc > 3) ? atoi(argv[3]) : 10;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro ao carregar romset: %s\n", err);
        return 1;
    }
    printf("romset ok: BIOS=%u P=%u S=%u M=%u\n", rom.bios_size, rom.p_size, rom.s_size, rom.m_size);

    neogeo_t ng;
    neogeo_init(&ng, &rom);

    /* EXPERIMENTO temporario: carrega a Backup RAM inteira de um arquivo
     * (ex: salva pelo MAME em third_party/mame/nvram/doubledr/saveram),
     * com correcao de byte-swap, pra testar se bate com o que a BIOS
     * espera em detalhe (nao so a assinatura de 16 bytes). */
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
            printf("(experimento: Backup RAM carregada de '%s', %zu bytes, com byte-swap)\n", backup_file, n);
        } else {
            printf("(experimento: nao consegui abrir '%s')\n", backup_file);
        }
    }

    neogeo_reset(&ng);

    printf("pos-reset: pc=%06X sr=%04X ssp=%06X\n", ng.m68k.pc, ng.m68k.sr, ng.m68k.a[7]);

    for (int f = 0; f < frames; f++) {
        u32 prev_pc = ng.m68k.pc;
        neogeo_run_slice(&ng, 202752u);
        printf("frame %2d: pc=%06X sr=%04X halted=%d cat_halt=%d unknown_count=%u "
               "first_unknown_pc=%06X first_unknown_op=%04X irq_pend=%u vram_addr=%04X\n",
               f, ng.m68k.pc, ng.m68k.sr, ng.m68k.halted, ng.m68k.catastrophic_halt,
               ng.m68k.unknown_count, ng.m68k.first_unknown_pc, ng.m68k.first_unknown_opcode,
               ng.irq_pending, ng.vram_addr);
        if (ng.m68k.pc == prev_pc && f > 0) {
            printf("  (PC nao mudou desde o frame anterior - possivel loop apertado)\n");
        }
    }

    printf("\n--- diagnostico de falha ---\n");
    printf("last_exception_pc=%06X last_exception_vector=%u last_exception_opcode=%04X\n",
           ng.m68k.last_exception_pc, ng.m68k.last_exception_vector, ng.m68k.last_exception_opcode);
    printf("first_addr_err_valid=%u fault_pc=%06X last_pc=%06X last_op=%04X\n",
           ng.m68k.first_addr_err_valid, ng.m68k.first_addr_err_fault_pc,
           ng.m68k.first_addr_err_last_pc, ng.m68k.first_addr_err_last_op);
    printf("first_linef_valid=%u pc=%06X opcode=%04X prev_pc=%06X prev_op=%04X sr=%04X a7=%06X\n",
           ng.m68k.first_linef_valid, ng.m68k.first_linef_pc, ng.m68k.first_linef_opcode,
           ng.m68k.first_linef_prev_pc, ng.m68k.first_linef_prev_opcode,
           ng.m68k.first_linef_sr, ng.m68k.first_linef_a7);
    printf("first_unknown_valid=%u pc=%06X opcode=%04X prev_pc=%06X prev_op=%04X\n",
           ng.m68k.first_unknown_valid, ng.m68k.first_unknown_pc, ng.m68k.first_unknown_opcode,
           ng.m68k.first_unknown_prev_pc, ng.m68k.first_unknown_prev_opcode);
    printf("recover_last_pc=%06X recover_last_op=%04X recover_last_vector=%u recover_repeat_count=%u\n",
           ng.m68k.recover_last_pc, ng.m68k.recover_last_op, ng.m68k.recover_last_vector,
           ng.m68k.recover_repeat_count);
    printf("last_opcode_pc=%06X last_opcode=%04X prev_opcode_pc=%06X prev_opcode=%04X\n",
           ng.m68k.last_opcode_pc, ng.m68k.last_opcode, ng.m68k.prev_opcode_pc, ng.m68k.prev_opcode);

    printf("\nwram[0xFE8C]=%02X (flag que o C18714 espera virar 0)\n", ng.wram[0xFE8C]);
    printf("wram[0xFEE3]=%02X (gate do handler de vblank em C11654)\n", ng.wram[0xFEE3]);
    printf("wram[0xFEE4]=%02X (contador incrementado sempre, mesmo se pular o handler)\n", ng.wram[0xFEE4]);

    printf("\n--- amostras de VRAM ---\n");
    printf("fix map [0x7000..0x700F]: ");
    for (int i = 0; i < 16; i++) printf("%04X ", ng.vram[0x7000 + i]);
    printf("\npaleta [0..15]: ");
    for (int i = 0; i < 16; i++) printf("%04X ", ng.palette[i]);
    printf("\nSCB3 [0x8200..0x8203] (Y/altura sprite 0-3): ");
    for (int i = 0; i < 4; i++) printf("%04X ", ng.vram[0x8200 + i]);
    printf("\n");

    neogeo_romset_free(&rom);
    return 0;
}
