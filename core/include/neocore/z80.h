/**
 * @file z80.h
 * @brief Zilog Z80 CPU emulator for Sega Master System
 *
 * Instruction-level accurate Z80 core with callback-based memory/IO.
 * Supports all standard + CB/DD/ED/FD prefix instructions, NMI, and
 * maskable interrupts (IM 0/1/2).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Z80 {
    /* --- Main register set --- */
    uint16_t af, bc, de, hl;

    /* --- Shadow register set (EX AF,AF' / EXX) --- */
    uint16_t af2, bc2, de2, hl2;

    /* --- Index registers --- */
    uint16_t ix, iy;

    /* --- Stack pointer & program counter --- */
    uint16_t sp, pc;

    /* --- Special registers --- */
    uint8_t i;       /* Interrupt vector base (IM 2) */
    uint8_t r;       /* Refresh counter (7-bit + bit 7 preserved) */

    /* --- Interrupt state --- */
    uint8_t iff1;    /* Interrupt flip-flop 1 */
    uint8_t iff2;    /* Interrupt flip-flop 2 */
    uint8_t im;      /* Interrupt mode (0, 1, or 2) */
    bool    halted;  /* CPU is HALTed (waiting for interrupt) */

    /* --- Cycle accounting --- */
    int32_t cycles;  /* Cycles consumed so far this run */
    int32_t target;  /* Target cycle count for Z80_Run */

    /* --- Interrupt requests --- */
    bool nmi_pending; /* Edge-triggered NMI request */
    bool irq_line;    /* Level-triggered IRQ line state */

    /* Delayed EI: the instruction after EI has interrupts still disabled */
    bool ei_pending;

    /* Optional direct mapping for the hot local-RAM mirror. */
    uint8_t* fast_ram;
    uint16_t fast_ram_limit;
    uint16_t fast_ram_mask;

    /* --- Bus callbacks --- */
    void* ctx;  /* Opaque pointer (Sms*) */
    uint8_t (*read)(void* ctx, uint16_t addr);
    void    (*write)(void* ctx, uint16_t addr, uint8_t val);
    uint8_t (*io_read)(void* ctx, uint8_t port);
    void    (*io_write)(void* ctx, uint8_t port, uint8_t val);
} Z80;

/**
 * @brief Initialize Z80 state to power-on defaults.
 */
void Z80_Init(Z80* z);

/**
 * @brief Reset Z80 (PC=0, interrupts disabled).
 */
void Z80_Reset(Z80* z);

/**
 * @brief Execute instructions until at least target_cycles have elapsed.
 * @param target_cycles Number of T-states to execute.
 * @return Actual number of T-states executed (may overshoot slightly).
 */
int Z80_Run(Z80* z, int target_cycles);

/**
 * @brief Trigger a non-maskable interrupt (edge-triggered).
 *        Used by SMS PAUSE button.
 */
void Z80_NMI(Z80* z);

/**
 * @brief Set the maskable interrupt line level.
 *        Used by VDP frame/line interrupts (IM 1 → RST 38h).
 */
void Z80_IRQ(Z80* z, bool level);

#ifdef __cplusplus
}
#endif
