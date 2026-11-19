/* Ferramenta de diagnóstico focada: roda o sistema real (neogeo_run_slice,
 * mesmo caminho de execução do frontend) em fatias de 1 scanline, e loga
 * toda vez que o PC passa pela entrada da ISR de vblank ($C11646) ou que
 * wram[0xFE8C] muda de valor — pra descobrir por que essa flag nunca é
 * zerada mesmo com a interrupção disparando (ver docs/ROADMAP.md).
 * Uso: debug_vblank <dir_cartucho> <dir_bios> <num_scanlines>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("uso: %s <dir_cartucho> <dir_bios> <num_scanlines>\n", argv[0]);
        return 1;
    }
    int scanlines = atoi(argv[3]);

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

    u8 last_fe8c = ng.wram[0xFE8C];
    u8 last_fee3 = ng.wram[0xFEE3];
    bool was_at_isr = false;
    int isr_entries = 0;
    const int cycles_per_scanline = 202752 / 264;

    for (int i = 0; i < scanlines; i++) {
        neogeo_run_slice(&ng, (u32)cycles_per_scanline);

        if (ng.m68k.pc == 0xC17F0Eu) {
            printf("scanline %d: ENTROU no handler de estado 0 (pc=C17F0E) d0=%08X a5=%06X\n",
                   i, ng.m68k.d[0], ng.m68k.a[5]);
        }
        if (ng.m68k.pc == 0xC17EC4u) {
            printf("scanline %d: prestes a despachar via jump table (pc=C17EC4) d0=%08X (indice*4, deveria ser 0)\n",
                   i, ng.m68k.d[0]);
        }
        bool at_isr = (ng.m68k.pc == 0xC11646u);
        if (at_isr && !was_at_isr) {
            isr_entries++;
            if (isr_entries <= 10 || isr_entries % 50 == 0) {
                printf("scanline %d: entrou na ISR (pc=C11646) a7=%06X sr=%04X d0=%08X fe8c=%02X fee3=%02X\n",
                       i, ng.m68k.a[7], ng.m68k.sr, ng.m68k.d[0], ng.wram[0xFE8C], ng.wram[0xFEE3]);
            }
        }
        was_at_isr = at_isr;

        if (ng.wram[0xFE8C] != last_fe8c) {
            printf("scanline %d: wram[0xFE8C] mudou de %02X pra %02X (pc=%06X a7=%06X)\n",
                   i, last_fe8c, ng.wram[0xFE8C], ng.m68k.pc, ng.m68k.a[7]);
            last_fe8c = ng.wram[0xFE8C];
        }
        if (ng.wram[0xFEE3] != last_fee3) {
            printf("scanline %d: wram[0xFEE3] mudou de %02X pra %02X (pc=%06X)\n",
                   i, last_fee3, ng.wram[0xFEE3], ng.m68k.pc);
            last_fee3 = ng.wram[0xFEE3];
        }
    }

    printf("\nTotal de entradas na ISR (pc==C11646): %d\n", isr_entries);
    printf("Estado final: pc=%06X a7=%06X sr=%04X fe8c=%02X fee3=%02X fee4=%02X\n",
           ng.m68k.pc, ng.m68k.a[7], ng.m68k.sr, ng.wram[0xFE8C], ng.wram[0xFEE3], ng.wram[0xFEE4]);
    {
        /* Todos os enderecos "($xxx,A5)" no disassembly sao RELATIVOS a
         * A5, nao absolutos — bug que eu tinha antes (somava errado, lia
         * offsets como se fossem enderecos diretos). A5 = $10F300 na
         * maioria dos contextos, mas usamos o valor REAL do registrador
         * agora pra nao errar de novo. */
        u32 a5 = ng.m68k.a[5] & 0xFFFFFFu;
        u32 base = a5 - 0x100000u; /* offset dentro de wram (assumindo A5 na janela de Work RAM) */
        printf("Condicoes de saida do loop C17E4C (A5=%06X, offsets relativos a ele):\n", a5);
        printf("  wram[A5+0xA82]=%02X (precisa != 0 p/ condicao 1)\n", ng.wram[(base + 0xA82u) & 0xFFFFu]);
        printf("  backup_ram[0x34..0x35]=%02X%02X (word, precisa != 0 p/ condicao 1)\n",
               ng.backup_ram[0x34], ng.backup_ram[0x35]);
        printf("  wram[A5+0xBEB]=%02X (nibble alto precisa != 0 p/ condicao 2)\n", ng.wram[(base + 0xBEBu) & 0xFFFFu]);
        printf("  wram[A5+0xAAD]=%02X (bit0 precisa = 1 p/ condicao 3)\n", ng.wram[(base + 0xAADu) & 0xFFFFu]);
        printf("  wram[A5+0x9D8]=%02X%02X (indice do jump table de estado, C17150)\n",
               ng.wram[(base + 0x9D8u) & 0xFFFFu], ng.wram[(base + 0x9D9u) & 0xFFFFu]);
        printf("  wram[A5+0xBDD]=%02X (bit0 precisa ser 1 p/ estado1 avancar pro estado2, ver C1724C)\n",
               ng.wram[(base + 0xBDDu) & 0xFFFFu]);
    }

    neogeo_romset_free(&rom);
    return 0;
}
