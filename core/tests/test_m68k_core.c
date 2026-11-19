/* Teste de fumaça do core 68000 PORTADO (não é mais o esqueleto escrito do
 * zero — ver docs/ROADMAP.md e docs/ARCHITECTURE.md sobre a origem do core:
 * third_party/m68k do projeto STM32F429_lMD do próprio autor). Mesmo
 * programinha de antes, montado à mão em bytes numa RAM sintética.
 *
 *   0x000000: 00001000            ; vetor reset: SSP inicial
 *   0x000004: 00000400            ; vetor reset: PC inicial
 *   0x000400: 7005                MOVEQ #5,D0
 *   0x000402: 7203                MOVEQ #3,D1
 *   0x000404: D081                ADD.L D1,D0        ; D0 = 8
 *   0x000406: 2400                MOVE.L D0,D2       ; D2 = 8
 *   0x000408: 4EB9 00000420       JSR 0x420
 *   0x00040E: 4E71                NOP
 *   0x000410: 60FE                BRA *  (loop infinito)
 *   0x000420: 9481                SUB.L D1,D2        ; D2 = 8 - 3 = 5
 *   0x000422: 4E75                RTS
 *
 * Esperado: D0=8, D1=3, D2=5.
 */

#include "neocore/m68k_core.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[65536];

static uint8_t t_read8(void *ctx, uint32_t addr) { (void)ctx; return mem[addr & 0xFFFF]; }
static uint16_t t_read16(void *ctx, uint32_t addr) {
    (void)ctx;
    addr &= 0xFFFF;
    return (uint16_t)((mem[addr] << 8) | mem[(addr + 1) & 0xFFFF]);
}
static void t_write8(void *ctx, uint32_t addr, uint8_t val) { (void)ctx; mem[addr & 0xFFFF] = val; }
static void t_write16(void *ctx, uint32_t addr, uint16_t val) {
    (void)ctx;
    addr &= 0xFFFF;
    mem[addr] = (uint8_t)(val >> 8);
    mem[(addr + 1) & 0xFFFF] = (uint8_t)val;
}

static void poke16(uint32_t addr, uint16_t val) { t_write16(NULL, addr, val); }
static void poke32(uint32_t addr, uint32_t val) {
    t_write16(NULL, addr, (uint16_t)(val >> 16));
    t_write16(NULL, addr + 2, (uint16_t)val);
}

int main(void) {
    memset(mem, 0, sizeof(mem));

    poke32(0x000000, 0x00001000);
    poke32(0x000004, 0x00000400);

    poke16(0x000400, 0x7005);
    poke16(0x000402, 0x7203);
    poke16(0x000404, 0xD081);
    poke16(0x000406, 0x2400);
    poke16(0x000408, 0x4EB9);
    poke32(0x00040A, 0x00000420);
    poke16(0x00040E, 0x4E71);
    poke16(0x000410, 0x60FE);

    poke16(0x000420, 0x9481);
    poke16(0x000422, 0x4E75);

    M68kCore cpu;
    m68k_core_init(&cpu, t_read8, t_read16, t_write8, t_write16, NULL);
    m68k_core_reset(&cpu);

    int failures = 0;

    if (cpu.pc != 0x400) {
        printf("FALHA: PC pos-reset = 0x%06X, esperado 0x000400\n", cpu.pc);
        failures++;
    }
    if (cpu.a[7] != 0x1000) {
        printf("FALHA: SSP pos-reset = 0x%06X, esperado 0x001000\n", cpu.a[7]);
        failures++;
    }

    m68k_core_run_cycles(&cpu, 200);

    if (cpu.d[0] != 8) { printf("FALHA: D0 = %u, esperado 8\n", cpu.d[0]); failures++; }
    if (cpu.d[1] != 3) { printf("FALHA: D1 = %u, esperado 3\n", cpu.d[1]); failures++; }
    if (cpu.d[2] != 5) { printf("FALHA: D2 = %u, esperado 5\n", cpu.d[2]); failures++; }

    if (failures == 0) {
        printf("OK: test_m68k_core passou (D0=%u D1=%u D2=%u)\n", cpu.d[0], cpu.d[1], cpu.d[2]);
        return 0;
    }
    printf("test_m68k_core FALHOU (%d erro(s))\n", failures);
    return 1;
}
