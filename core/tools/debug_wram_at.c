/* Dump da WRAM (64KB) num vblank EXATO, pra comparar byte a byte contra o
 * `save mamewram.bin,0x100000,0x10000` do debugger do MAME.
 * Uso: debug_wram_at <dir_cartucho> <dir_bios> <n_vblanks> <saida.bin>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <n_vblanks> <saida.bin>\n", argv[0]);
        return 1;
    }
    long n = atol(argv[3]);

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    for (long i = 0; i < n; i++) neogeo_run_until_vblank(&ng);

    FILE *f = fopen(argv[4], "wb");
    if (!f) { printf("erro: nao consegui criar %s\n", argv[4]); return 1; }
    /* argv[5] opcional = "z80": despeja a RAM do Z80 ($F800-$FFFF, 2KB) em
     * vez da WRAM — compara com `save arq,0xf800:audiocpu,0x800` do MAME. */
    /* "p": despeja a P ROM inteira já decifrada (compara com `save` do
     * espaço do 68000 no MAME, lembrando que $0-$7F são vetores da BIOS). */
    /* "s": despeja a backup RAM ($D00000-$D0FFFF) — compara com
     * `save arq,0xd00000:maincpu,0x10000` do MAME. */
    /* "v": VRAM do LSPC em words big-endian (compara com m_videoram do
     * spritegen do MAME, lido via Lua `emu.item`). */
    if (argc >= 6 && argv[5][0] == 'v') {
        for (u32 i = 0; i < NEOGEO_VRAM_WORDS; i++) { fputc(ng.vram[i] >> 8, f); fputc(ng.vram[i] & 0xFF, f); }
    }
    else if (argc >= 6 && argv[5][0] == 's') fwrite(ng.backup_ram, 1, NEOGEO_BACKUP_RAM_SIZE, f);
    else if (argc >= 6 && argv[5][0] == 'b') fwrite(ng.rom.bios, 1, ng.rom.bios_size, f);
    else if (argc >= 6 && argv[5][0] == 'p') fwrite(ng.rom.p, 1, ng.rom.p_size, f);
    else if (argc >= 6 && argv[5][0] == 'z') fwrite(ng.sound_ram, 1, NEOGEO_SOUND_RAM_SIZE, f);
    else                                fwrite(ng.wram, 1, NEOGEO_WRAM_SIZE, f);
    fclose(f);
    printf("ok: %s de %ld vblanks -> %s (pc=%06x z80pc=%04x)\n",
           (argc >= 6 && argv[5][0] == 'z') ? "ram do z80" : "wram", n, argv[4], ng.m68k.pc, ng.z80.pc);
    neogeo_romset_free(&rom);
    return 0;
}
