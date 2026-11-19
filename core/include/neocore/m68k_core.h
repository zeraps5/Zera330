#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HOST_BUILD)
#define M68K_RAMFUNC
#else
#define M68K_RAMFUNC __attribute__((section(".RamFunc")))
#endif

typedef uint8_t  (*M68kRead8Fn)(void* user, uint32_t addr24);
typedef uint16_t (*M68kRead16Fn)(void* user, uint32_t addr24);
typedef void     (*M68kWrite8Fn)(void* user, uint32_t addr24, uint8_t value);
typedef void     (*M68kWrite16Fn)(void* user, uint32_t addr24, uint16_t value);
typedef void     (*M68kResetFn)(void* user);
/* Called when the CPU acknowledges an interrupt (IACK), level = 1..7. */
typedef void     (*M68kIrqAckFn)(void* user, uint8_t level);
/* Called when an address error fires: fault_pc=bad PC, last_op_pc=instruction that
   triggered the jump, last_op=that instruction's opcode word. */
typedef void     (*M68kAddrErrFn)(void* user, uint32_t fault_pc,
                                  uint32_t last_op_pc, uint16_t last_op);

typedef struct M68kCore {
  uint32_t d[8];
  uint32_t a[8];
  uint32_t usp;
  uint32_t ssp;
  uint32_t pc;
  uint16_t sr;
  uint32_t cycles_total;
  /* Achado #101: sincronia do IACK autovetorado com o clock E (VPA), como
   * `m68000_device::vpa_sync` do MAME. O host põe em `host_time` o tempo
   * absoluto (em ciclos) do início de cada passo e liga `vpa_sync`. */
  uint64_t host_time;
  uint64_t last_vpa_time;
  uint8_t vpa_sync;
  /* Achado #101: divisor da última DIVU/DIVS (custo depende dos operandos). */
  uint16_t last_div_src;
  bool halted;
  bool catastrophic_halt;
  bool external_bus_halt;
  uint8_t irq_level;
  uint8_t addr_error_pending;
  uint8_t group0_exception_active;
  uint8_t access_fc;
  uint8_t access_is_read;
  uint8_t access_is_instruction;
  uint16_t addr_error_ssw;
  uint32_t addr_error_addr;
  uint32_t last_opcode_pc;
  uint16_t last_opcode;
  uint32_t prev_opcode_pc;
  uint16_t prev_opcode;
  uint32_t unknown_count;
  uint32_t last_unknown_pc;
  uint16_t last_unknown_opcode;
  uint8_t first_unknown_valid;
  uint32_t first_unknown_pc;
  uint16_t first_unknown_opcode;
  uint32_t first_unknown_prev_pc;
  uint16_t first_unknown_prev_opcode;
  uint16_t first_unknown_sr;
  uint32_t first_unknown_a7;
  uint32_t last_exception_pc;
  uint8_t last_exception_vector;
  /* Achado #90: conta exceções tomadas (pra saber se a instrução atual
   * gerou uma e cobrar o tempo da exceção). */
  uint32_t exception_seq;
  uint8_t exception_seq_vector;
  uint16_t last_exception_opcode;
  /* First address error: persistent, survives ring-buffer wraps, read by snapshot. */
  uint8_t first_addr_err_valid;
  uint32_t first_addr_err_fault_pc;  /* Bad (odd) PC that caused the fault */
  uint32_t first_addr_err_last_pc;   /* PC of the instruction that branched there */
  uint16_t first_addr_err_last_op;   /* Opcode of that instruction */
  uint8_t first_linef_valid;
  uint32_t first_linef_pc;
  uint16_t first_linef_opcode;
  uint32_t first_linef_prev_pc;
  uint16_t first_linef_prev_opcode;
  uint16_t first_linef_sr;
  uint32_t first_linef_a7;
  uint32_t recover_last_pc;
  uint16_t recover_last_op;
  uint8_t recover_last_vector;
  uint8_t recover_repeat_count;
  uint8_t ea_rw_cache_valid;
  uint8_t ea_rw_expect_writeback;
  uint8_t ea_rw_mode;
  uint8_t ea_rw_reg;
  uint8_t ea_rw_size;
  uint32_t ea_rw_addr;

  /* Optional direct mappings for the hot cartridge-ROM and work-RAM paths. */
  const uint8_t* fast_rom;
  uint32_t fast_rom_limit;
  uint8_t* fast_ram;
  uint32_t observable_bus_epoch;

  void* user;
  M68kRead8Fn read8;
  M68kRead16Fn read16;
  M68kWrite8Fn write8;
  M68kWrite16Fn write16;
  M68kResetFn reset_instr;
  M68kIrqAckFn irq_ack_fn;    /* Optional: called on IACK, clears peripheral IRQ */
  M68kAddrErrFn addr_err_fn;  /* Optional: called on address error, for diagnostics */
} M68kCore;

void m68k_core_init(M68kCore* c,
                    M68kRead8Fn read8, M68kRead16Fn read16,
                    M68kWrite8Fn write8, M68kWrite16Fn write16,
                    void* user);
void m68k_core_reset(M68kCore* c);
void m68k_core_set_irq(M68kCore* c, uint8_t level);
void m68k_core_set_fast_memory(M68kCore* c, const uint8_t* rom,
                               uint32_t rom_limit, uint8_t* ram64k);
M68K_RAMFUNC uint32_t m68k_core_step(M68kCore* c);
uint32_t m68k_core_run_cycles(M68kCore* c, uint32_t cycles_budget);

#ifdef __cplusplus
}
#endif
