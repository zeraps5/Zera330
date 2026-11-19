/* Teste de fumaça do core Z80 portado (third_party/z80 do projeto Mega
 * Drive do autor — usado lá pro Master System, aqui vai virar a CPU de som
 * do Neo-Geo controlando o YM2610, ver docs/ROADMAP.md Fase 2).
 *
 * Programa em 0x0000 (Z80 sempre reseta com PC=0):
 *   3E 12       LD A,0x12
 *   32 00 20    LD (0x2000),A
 *   76          HALT
 *
 * Esperado: mem[0x2000] == 0x12 e CPU em estado halted.
 */

#include "neocore/z80.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[65536];

static uint8_t t_read(void *ctx, uint16_t addr) { (void)ctx; return mem[addr]; }
static void t_write(void *ctx, uint16_t addr, uint8_t val) { (void)ctx; mem[addr] = val; }
static uint8_t t_io_read(void *ctx, uint8_t port) { (void)ctx; (void)port; return 0xFF; }
static void t_io_write(void *ctx, uint8_t port, uint8_t val) { (void)ctx; (void)port; (void)val; }

int main(void) {
    memset(mem, 0, sizeof(mem));
    mem[0] = 0x3E; mem[1] = 0x12;
    mem[2] = 0x32; mem[3] = 0x00; mem[4] = 0x20;
    mem[5] = 0x76;

    Z80 cpu;
    Z80_Init(&cpu);
    cpu.ctx = NULL;
    cpu.read = t_read;
    cpu.write = t_write;
    cpu.io_read = t_io_read;
    cpu.io_write = t_io_write;
    Z80_Reset(&cpu);

    int failures = 0;

    if (cpu.pc != 0) {
        printf("FALHA: PC pos-reset = 0x%04X, esperado 0x0000\n", cpu.pc);
        failures++;
    }

    Z80_Run(&cpu, 60);

    if (mem[0x2000] != 0x12) {
        printf("FALHA: mem[0x2000] = 0x%02X, esperado 0x12\n", mem[0x2000]);
        failures++;
    }
    if (!cpu.halted) {
        printf("FALHA: CPU deveria estar halted apos HALT\n");
        failures++;
    }

    if (failures == 0) {
        printf("OK: test_z80 passou (mem[0x2000]=0x%02X, halted=%d)\n", mem[0x2000], cpu.halted);
        return 0;
    }
    printf("test_z80 FALHOU (%d erro(s))\n", failures);
    return 1;
}
