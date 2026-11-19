#include "m68k_core.h"

#include <string.h>
#include "m68k_cycles_000.inc"

#if defined(M68K_OPCODE_PROFILE)
uint32_t g_m68k_opcode_profile[0x10000];
uint32_t g_m68k_rom_pc_profile[0x40000];
#endif

#if !defined(HOST_BUILD)
#pragma GCC optimize ("O3")
#endif

#define SR_S      0x2000u
#define SR_I_MASK 0x0700u
#define SR_X      0x0010u
#define SR_N      0x0008u
#define SR_Z      0x0004u
#define SR_V      0x0002u
#define SR_C      0x0001u
#define CCR_MASK  (SR_X | SR_N | SR_Z | SR_V | SR_C)
#define M68K_STRICT_HARDWARE 1

#if defined(__GNUC__) && !defined(HOST_BUILD)
#define M68K_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define M68K_FORCE_INLINE static
#endif

static inline uint32_t mask24(uint32_t v) { return v & 0x00FFFFFFu; }
static inline int32_t sx8(uint8_t v) { return (int32_t)(int8_t)v; }
static inline int32_t sx16(uint16_t v) { return (int32_t)(int16_t)v; }
static inline uint16_t swap16_fast(uint16_t v) {
  return (uint16_t)((v << 8) | (v >> 8));
}

static inline uint16_t fetch_rom16(const M68kCore* c, uint32_t addr) {
  uint16_t raw = *(const uint16_t*)(const void*)&c->fast_rom[addr];
  return swap16_fast(raw);
}

static inline uint8_t m68k_function_code(const M68kCore* c, bool instruction) {
  bool supervisor = c && ((c->sr & SR_S) != 0u);
  if (instruction) {
    return supervisor ? 6u : 2u;
  }
  return supervisor ? 5u : 1u;
}

static inline void set_access_context(M68kCore* c, bool is_read, bool instruction) {
  if (!c) return;
  c->access_fc = m68k_function_code(c, instruction);
  c->access_is_read = is_read ? 1u : 0u;
  c->access_is_instruction = instruction ? 1u : 0u;
}

static inline void raise_addr_error(M68kCore* c, uint32_t addr24) {
  if (!c || c->addr_error_pending) return;
  c->addr_error_pending = 1u;
  c->addr_error_addr = mask24(addr24);
  c->addr_error_ssw = (uint16_t)(((uint16_t)(c->access_is_read ? 1u : 0u) << 4) |
                                 ((uint16_t)(c->access_is_instruction ? 0u : 1u) << 3) |
                                 (uint16_t)(c->access_fc & 7u));
}

static inline uint32_t size_mask(uint8_t size) {
  return (size == 1u) ? 0xFFu : ((size == 2u) ? 0xFFFFu : 0xFFFFFFFFu);
}

static inline uint32_t sign_mask(uint8_t size) {
  return (size == 1u) ? 0x80u : ((size == 2u) ? 0x8000u : 0x80000000u);
}

static uint8_t mem_read8(M68kCore* c, uint32_t addr24) {
  uint32_t a;
  if (!c) return 0u;
  a = mask24(addr24);
  if (c->fast_rom && a < c->fast_rom_limit) {
    return c->fast_rom[a];
  }
  if (c->fast_ram && a >= 0x00E00000u) {
    return c->fast_ram[a & 0xFFFFu];
  }
  /* Reads outside direct ROM/RAM can observe or change device state.  Mark
     them so the polling-loop accelerator never skips volatile bus traffic. */
  c->observable_bus_epoch++;
  return c->read8 ? c->read8(c->user, a) : 0u;
}

static uint16_t mem_read16(M68kCore* c, uint32_t addr24) {
  uint32_t a;
  if ((addr24 & 1u) != 0u) {
    raise_addr_error(c, addr24);
    return 0u;
  }
  if (!c) return 0u;
  a = mask24(addr24);
  if (c->fast_rom && (a + 1u) < c->fast_rom_limit) {
    uint16_t raw = *(const uint16_t*)(const void*)&c->fast_rom[a];
    return swap16_fast(raw);
  }
  if (c->fast_ram && a >= 0x00E00000u) {
    uint16_t raw = *(const uint16_t*)(const void*)&c->fast_ram[a & 0xFFFFu];
    return swap16_fast(raw);
  }
  c->observable_bus_epoch++;
  return c->read16 ? c->read16(c->user, a) : 0u;
}

static uint32_t mem_read32(M68kCore* c, uint32_t addr24) {
  if ((addr24 & 1u) != 0u) {
    raise_addr_error(c, addr24);
    return 0u;
  }
  uint16_t hi = mem_read16(c, addr24);
  if (c && c->addr_error_pending) return 0u;
  uint16_t lo = mem_read16(c, addr24 + 2u);
  return ((uint32_t)hi << 16) | lo;
}

static void mem_write8(M68kCore* c, uint32_t addr24, uint8_t value) {
  uint32_t a;
  if (!c) return;
  a = mask24(addr24);
  c->observable_bus_epoch++;
  if (c->fast_ram && a >= 0x00E00000u) {
    c->fast_ram[a & 0xFFFFu] = value;
    return;
  }
  if (c->write8) c->write8(c->user, a, value);
}

static void mem_write16(M68kCore* c, uint32_t addr24, uint16_t value) {
  uint32_t a;
  if ((addr24 & 1u) != 0u) {
    raise_addr_error(c, addr24);
    return;
  }
  if (!c) return;
  a = mask24(addr24);
  c->observable_bus_epoch++;
  if (c->fast_ram && a >= 0x00E00000u) {
    *(uint16_t*)(void*)&c->fast_ram[a & 0xFFFFu] = swap16_fast(value);
    return;
  }
  if (c->write16) c->write16(c->user, a, value);
}

static void mem_write32(M68kCore* c, uint32_t addr24, uint32_t value) {
  if ((addr24 & 1u) != 0u) {
    raise_addr_error(c, addr24);
    return;
  }
  mem_write16(c, addr24, (uint16_t)(value >> 16));
  if (c && c->addr_error_pending) return;
  mem_write16(c, addr24 + 2u, (uint16_t)(value & 0xFFFFu));
}

static uint16_t fetch16(M68kCore* c) {
  uint32_t addr = c->pc;
  if ((addr & 1u) != 0u) {
    set_access_context(c, true, true);
  }
  uint32_t a = mask24(addr);
  uint16_t v;
  if ((addr & 1u) == 0u && c->fast_rom && (a + 1u) < c->fast_rom_limit) {
    v = fetch_rom16(c, a);
  } else {
    v = mem_read16(c, addr);
  }
  c->pc = mask24(c->pc + 2u);
  return v;
}

static uint32_t fetch32(M68kCore* c) {
  uint16_t hi = fetch16(c);
  if (c->addr_error_pending) {
    c->pc = mask24(c->pc + 2u);
    return 0u;
  }
  uint16_t lo = fetch16(c);
  return ((uint32_t)hi << 16) | lo;
}

static void push16(M68kCore* c, uint16_t v) {
  c->a[7] = (c->a[7] - 2u);
  if ((c->a[7] & 1u) != 0u) {
    set_access_context(c, false, false);
  }
  mem_write16(c, c->a[7], v);
}

static void push32(M68kCore* c, uint32_t v) {
  c->a[7] = (c->a[7] - 4u);
  if ((c->a[7] & 1u) != 0u) {
    set_access_context(c, false, false);
  }
  mem_write32(c, c->a[7], v);
}

static uint16_t pop16(M68kCore* c) {
  uint32_t addr = c->a[7];
  if ((addr & 1u) != 0u) {
    set_access_context(c, true, false);
  }
  uint16_t v = mem_read16(c, addr);
  c->a[7] = (c->a[7] + 2u);
  return v;
}

static uint32_t pop32(M68kCore* c) {
  uint32_t addr = c->a[7];
  if ((addr & 1u) != 0u) {
    set_access_context(c, true, false);
  }
  uint32_t v = mem_read32(c, addr);
  c->a[7] = (c->a[7] + 4u);
  return v;
}

static uint16_t data_read16(M68kCore* c, uint32_t addr24) {
  if ((addr24 & 1u) != 0u) {
    set_access_context(c, true, false);
  }
  return mem_read16(c, addr24);
}

static uint32_t data_read32(M68kCore* c, uint32_t addr24) {
  if ((addr24 & 1u) != 0u) {
    set_access_context(c, true, false);
  }
  return mem_read32(c, addr24);
}

static void data_write16(M68kCore* c, uint32_t addr24, uint16_t value) {
  if ((addr24 & 1u) != 0u) {
    set_access_context(c, false, false);
  }
  mem_write16(c, addr24, value);
}

static void data_write32(M68kCore* c, uint32_t addr24, uint32_t value) {
  if ((addr24 & 1u) != 0u) {
    set_access_context(c, false, false);
  }
  mem_write32(c, addr24, value);
}

static inline bool get_flag(const M68kCore* c, uint16_t mask) { return (c->sr & mask) != 0u; }
static inline void set_flag(M68kCore* c, uint16_t mask, bool on) {
  if (on) c->sr |= mask;
  else c->sr &= (uint16_t)~mask;
}

static void set_nz(M68kCore* c, uint32_t value, uint8_t size) {
  uint32_t m = size_mask(size);
  uint32_t s = sign_mask(size);
  uint32_t v = value & m;
  uint16_t flags = (v == 0u ? SR_Z : 0u) | ((v & s) != 0u ? SR_N : 0u);
  c->sr = (uint16_t)((c->sr & (uint16_t)~(SR_N | SR_Z)) | flags);
}

static void set_logic_flags(M68kCore* c, uint32_t value, uint8_t size) {
  uint32_t v = value & size_mask(size);
  uint16_t flags = (v == 0u ? SR_Z : 0u) |
                   ((v & sign_mask(size)) != 0u ? SR_N : 0u);
  c->sr = (uint16_t)((c->sr & (uint16_t)~(SR_N | SR_Z | SR_V | SR_C)) | flags);
}

static void set_add_flags(M68kCore* c, uint32_t dst, uint32_t src, uint32_t res, uint8_t size) {
  uint32_t m = size_mask(size);
  uint32_t s = sign_mask(size);
  uint32_t d = dst & m;
  uint32_t q = src & m;
  uint32_t r = res & m;
  bool carry = r < d;
  bool ov = ((~(d ^ q) & (d ^ r) & s) != 0u);
  uint16_t flags = (r == 0u ? SR_Z : 0u) |
                   ((r & s) != 0u ? SR_N : 0u) |
                   (ov ? SR_V : 0u) |
                   (carry ? (SR_X | SR_C) : 0u);
  c->sr = (uint16_t)((c->sr & (uint16_t)~(SR_X | SR_N | SR_Z | SR_V | SR_C)) | flags);
}

static void set_sub_flags(M68kCore* c, uint32_t dst, uint32_t src, uint32_t res, uint8_t size) {
  uint32_t m = size_mask(size);
  uint32_t s = sign_mask(size);
  uint32_t d = dst & m;
  uint32_t q = src & m;
  uint32_t r = res & m;
  bool borrow = q > d;
  bool ov = (((d ^ q) & (d ^ r) & s) != 0u);
  uint16_t flags = (r == 0u ? SR_Z : 0u) |
                   ((r & s) != 0u ? SR_N : 0u) |
                   (ov ? SR_V : 0u) |
                   (borrow ? (SR_X | SR_C) : 0u);
  c->sr = (uint16_t)((c->sr & (uint16_t)~(SR_X | SR_N | SR_Z | SR_V | SR_C)) | flags);
}

static uint8_t bcd_add(uint8_t dst, uint8_t src, bool x_in, bool* carry_out) {
  int32_t dd = (int32_t)(((dst >> 4) & 0x0Fu) * 10 + (dst & 0x0Fu));
  int32_t ss = (int32_t)(((src >> 4) & 0x0Fu) * 10 + (src & 0x0Fu));
  int32_t rr = dd + ss + (x_in ? 1 : 0);
  bool c = rr > 99;
  if (c) rr -= 100;
  if (carry_out) *carry_out = c;
  return (uint8_t)((((uint8_t)(rr / 10)) << 4) | ((uint8_t)(rr % 10)));
}

static uint8_t bcd_sub(uint8_t dst, uint8_t src, bool x_in, bool* borrow_out) {
  int32_t dd = (int32_t)(((dst >> 4) & 0x0Fu) * 10 + (dst & 0x0Fu));
  int32_t ss = (int32_t)(((src >> 4) & 0x0Fu) * 10 + (src & 0x0Fu));
  int32_t rr = dd - ss - (x_in ? 1 : 0);
  bool b = rr < 0;
  if (b) rr += 100;
  if (borrow_out) *borrow_out = b;
  return (uint8_t)((((uint8_t)(rr / 10)) << 4) | ((uint8_t)(rr % 10)));
}

static bool cond_true(const M68kCore* c, uint8_t cc) {
  bool n = get_flag(c, SR_N);
  bool z = get_flag(c, SR_Z);
  bool v = get_flag(c, SR_V);
  bool cbit = get_flag(c, SR_C);
  switch (cc & 0x0Fu) {
    case 0x0: return true;
    case 0x1: return false;
    case 0x2: return !cbit && !z;
    case 0x3: return cbit || z;
    case 0x4: return !cbit;
    case 0x5: return cbit;
    case 0x6: return !z;
    case 0x7: return z;
    case 0x8: return !v;
    case 0x9: return v;
    case 0xA: return !n;
    case 0xB: return n;
    case 0xC: return n == v;
    case 0xD: return n != v;
    case 0xE: return !z && (n == v);
    default:  return z || (n != v);
  }
}

static void switch_stack_mode(M68kCore* c, bool to_supervisor) {
  if (!c) return;
  if (to_supervisor) {
    c->usp = (c->a[7]);
    c->a[7] = (c->ssp);
  } else {
    c->ssp = (c->a[7]);
    c->a[7] = (c->usp);
  }
}

static void write_sr_full(M68kCore* c, uint16_t new_sr) {
  bool old_super;
  bool new_super;
  if (!c) return;
  old_super = (c->sr & SR_S) != 0u;
  new_super = (new_sr & SR_S) != 0u;
  if (old_super != new_super) {
    switch_stack_mode(c, new_super);
  }
  c->sr = new_sr;
}

static void take_exception(M68kCore* c, uint8_t vector) {
  uint16_t old_sr;
  uint32_t new_pc;
  if (!c) return;
  old_sr = c->sr;
  if ((old_sr & SR_S) == 0u) {
    switch_stack_mode(c, true);
  }
  c->last_exception_pc = c->pc;
  c->last_exception_vector = vector;
  c->last_exception_opcode = 0u;
  c->exception_seq++;
  c->exception_seq_vector = vector;
  c->sr = (uint16_t)(old_sr | SR_S);
  push32(c, c->pc);
  push16(c, old_sr);
  set_access_context(c, true, true);
  new_pc = mem_read32(c, (uint32_t)vector * 4u);
  c->pc = mask24(new_pc);
  c->halted = false;
  c->external_bus_halt = false;
}

static void take_interrupt(M68kCore* c, uint8_t level) {
  uint16_t old_sr;
  uint32_t new_pc;
  if (!c) return;
  old_sr = c->sr;
  if ((old_sr & SR_S) == 0u) {
    switch_stack_mode(c, true);
  }
  c->sr = (uint16_t)(old_sr | SR_S);
  c->sr = (uint16_t)((c->sr & (uint16_t)~SR_I_MASK) | (((uint16_t)level & 7u) << 8));
  push32(c, c->pc);
  push16(c, old_sr);
  set_access_context(c, true, true);
  new_pc = mem_read32(c, (24u + (uint32_t)level) * 4u);
  c->pc = mask24(new_pc);
  c->halted = false;
  /* IACK: notify host so it can de-assert the interrupt source. */
  if (c->irq_ack_fn) {
    c->irq_ack_fn(c->user, level);
  }
}

static void take_address_error(M68kCore* c, uint32_t fault_pc, uint16_t fault_op, uint32_t* used) {
  uint16_t old_sr;
  uint16_t ssw;
  uint32_t fault_addr;
  uint32_t new_pc;
  if (!c || !used) return;
  c->exception_seq++;
  c->exception_seq_vector = 3u;
  old_sr = c->sr;
  ssw = c->addr_error_ssw;
  fault_addr = c->addr_error_addr;
  c->addr_error_pending = 0u;
  c->last_exception_pc = mask24(fault_pc);
  c->last_exception_vector = 3u;
  c->last_exception_opcode = fault_op;

  /* A bus/address error while the 68000 is already entering or servicing a
     group-0 exception is a double fault.  The physical CPU asserts HALT and
     cannot be resumed by an interrupt; only reset recovers it. */
  if (c->group0_exception_active) {
    c->catastrophic_halt = true;
    c->halted = true;
    *used = 4u;
    return;
  }
  c->group0_exception_active = 1u;

  if ((old_sr & SR_S) == 0u) {
    switch_stack_mode(c, true);
  }
  c->sr = (uint16_t)(old_sr | SR_S);
  push32(c, mask24(fault_pc));
  if (c->addr_error_pending) goto double_fault;
  push16(c, old_sr);
  if (c->addr_error_pending) goto double_fault;
  push16(c, fault_op);
  if (c->addr_error_pending) goto double_fault;
  push32(c, fault_addr);
  if (c->addr_error_pending) goto double_fault;
  push16(c, ssw);
  if (c->addr_error_pending) goto double_fault;
  set_access_context(c, true, true);
  new_pc = mem_read32(c, 3u * 4u);
  if (c->addr_error_pending) goto double_fault;
  c->pc = mask24(new_pc);
  if (!c->first_addr_err_valid) {
    c->first_addr_err_valid    = 1u;
    c->first_addr_err_fault_pc = mask24(fault_pc);
    c->first_addr_err_last_pc  = c->last_opcode_pc;
    c->first_addr_err_last_op  = c->last_opcode;
  }
  c->halted = false;
  if (c->addr_err_fn) {
    c->addr_err_fn(c->user, mask24(fault_pc), c->last_opcode_pc, c->last_opcode);
  }
  *used = 50u;
  return;

double_fault:
  c->addr_error_pending = 0u;
  c->catastrophic_halt = true;
  c->halted = true;
  *used = 4u;
}

#if !M68K_STRICT_HARDWARE
static bool vector_handler_is_invalid(M68kCore* c, uint8_t vector);
#endif

static bool service_interrupt(M68kCore* c) {
  uint8_t level = c->irq_level & 7u;
  uint8_t mask = (uint8_t)((c->sr >> 8) & 7u);
  if (level == 0u) return false;
  /* Level 7 is NMI: always accepted regardless of mask.
     Levels 1-6 must be strictly greater than current mask. */
  if (level < 7u && level <= mask) return false;

  take_interrupt(c, level);
  return true;
}

static bool require_supervisor(M68kCore* c) {
  if ((c->sr & SR_S) != 0u) return true;
  take_exception(c, 8u);
  return false;
}

#if !M68K_STRICT_HARDWARE
static bool vector_supports_recovery(uint8_t vector) {
  return vector == 3u || vector == 4u || vector == 5u ||
         vector == 6u || vector == 10u || vector == 11u;
}

static bool vector_handler_is_invalid(M68kCore* c, uint8_t vector) {
  uint32_t vec_pc;
  uint16_t w0;
  uint16_t w1;
  uint16_t w2;
  if (!c) return true;
  vec_pc = mask24(mem_read32(c, (uint32_t)vector * 4u) & ~1u);
  if (vec_pc == 0u) return true;
  w0 = mem_read16(c, vec_pc);
  w1 = mem_read16(c, vec_pc + 2u);
  w2 = mem_read16(c, vec_pc + 4u);
  if (vec_pc < 0x010000u) {
    uint8_t a0 = (uint8_t)(w0 >> 8);
    uint8_t a1 = (uint8_t)(w0 & 0xFFu);
    uint8_t b0 = (uint8_t)(w1 >> 8);
    uint8_t b1 = (uint8_t)(w1 & 0xFFu);
    bool ascii_pair = (a0 >= 0x20u && a0 <= 0x7Eu) &&
                      (a1 >= 0x20u && a1 <= 0x7Eu) &&
                      (b0 >= 0x20u && b0 <= 0x7Eu) &&
                      (b1 >= 0x20u && b1 <= 0x7Eu);
    if (w0 == 0x0000u || w0 == 0xFFFFu) return true;
    if (w0 == 0x4E73u || w0 == 0x4E75u) return true;
    if (w0 == 0x4E72u) return true; /* STOP in vector low area => dead handler */
    if (w0 == 0x60FDu || w0 == 0x60FEu) return true;
    if (w0 == 0x4E71u && w1 == 0x60FCu) return true;
    if (w0 == 0x6000u && w1 == 0xFFFEu) return true;
    if (vec_pc >= 0x000100u && vec_pc < 0x000200u && ascii_pair) return true;
    /* Common trap-loop stub seen in broken vector tables:
       NOP; NOP; BRA back (negative displacement). */
    if (w0 == 0x4E71u && w1 == 0x4E71u && (w2 & 0xFF00u) == 0x6000u &&
        (uint8_t)(w2 & 0x00FFu) >= 0x80u) {
      return true;
    }
  }
  return false;
}

static uint32_t vector_recover_next_pc(M68kCore* c, uint8_t vector, uint16_t op, uint32_t fault_pc) {
  uint32_t base;
  uint32_t step;
  uint8_t lane_vector;
  bool forced_local = false;
  if (!c) return mask24(fault_pc + 2u);

  base = mask24((fault_pc + 2u) & ~1u);
  if (fault_pc < 0x000200u) {
    uint32_t near = mask24((c->last_opcode_pc + 2u) & ~1u);
    if (near < 0x000200u) near = 0x000200u;
    base = near;
  }
  if (fault_pc >= 0x00FFF000u) {
    uint32_t local = mask24((c->last_opcode_pc + 2u) & ~1u);
    if (local < 0x000200u) {
      local = 0x000200u;
    }
    base = local;
    forced_local = true;
  }
  if (vector == 3u && (fault_pc & 1u) != 0u) {
    base = mask24((fault_pc + 1u) & ~1u);
  }
  if (!forced_local && base <= mask24(fault_pc & ~1u)) {
    base = mask24((fault_pc + 2u) & ~1u);
  }

  lane_vector = vector;
  if (lane_vector == 4u || lane_vector == 11u) {
    lane_vector = 0xFEu; /* illegal/lineF share same anti-loop lane */
  }

  if (c->recover_last_pc == fault_pc &&
      c->recover_last_op == op &&
      c->recover_last_vector == lane_vector) {
    if (c->recover_repeat_count < 0xFEu) c->recover_repeat_count++;
  } else {
    c->recover_last_pc = fault_pc;
    c->recover_last_op = op;
    c->recover_last_vector = lane_vector;
    c->recover_repeat_count = 1u;
  }

  step = (uint32_t)c->recover_repeat_count * 2u;
  if (step > 24u) step = 24u;
  return mask24(base + step - 2u);
}
#endif

static bool vector_is_recoverable(M68kCore* c, uint8_t vector, uint16_t op, uint32_t op_pc, uint32_t* used) {
#if M68K_STRICT_HARDWARE
  (void)c;
  (void)vector;
  (void)op;
  (void)op_pc;
  (void)used;
  return false;
#else
  if (!c || !used) return false;
  if (!vector_supports_recovery(vector)) return false;
  if (!vector_handler_is_invalid(c, vector)) return false;

  c->last_unknown_pc = op_pc;
  c->last_unknown_opcode = op;
  c->unknown_count++;
  c->pc = vector_recover_next_pc(c, vector, op, op_pc);
  *used = 6u;
  return true;
#endif
}

#if !M68K_STRICT_HARDWARE
static uint32_t sanitize_trap_return_pc(M68kCore* c, uint16_t op, uint32_t ret_pc) {
  uint32_t base;
  uint32_t step;
  if (!c) return mask24(ret_pc);

  /* Odd return addresses from malformed frames are a common infinite-loop source.
     Move forward to the next aligned instruction boundary. */
  if (ret_pc >= 0x00FFF000u) {
    base = mask24((c->last_opcode_pc + 2u) & ~1u);
    if (base < 0x000200u) {
      base = 0x000200u;
    }
  } else if ((ret_pc & 1u) != 0u) {
    base = mask24((ret_pc + 1u) & ~1u);
  } else {
    base = mask24(ret_pc);
  }

  if (c->recover_last_pc == ret_pc &&
      c->recover_last_op == op &&
      c->recover_last_vector == 0xFFu) {
    if (c->recover_repeat_count < 0xFEu) c->recover_repeat_count++;
  } else {
    c->recover_last_pc = ret_pc;
    c->recover_last_op = op;
    c->recover_last_vector = 0xFFu; /* Return-frame sanitizer lane */
    c->recover_repeat_count = 1u;
  }

  step = (uint32_t)(c->recover_repeat_count - 1u) * 2u;
  if (step > 16u) step = 16u;

  /* If trap returns keep targeting low-memory boot/trap area, force progress
     near the current handler stream instead of bouncing back to 0x0002xx. */
  if (base <= 0x000400u && c->recover_repeat_count >= 2u) {
    uint32_t handler_base = mask24((c->last_opcode_pc + 2u) & ~1u);
    base = handler_base;
    if (step > 8u) step = 8u;
  }

  return mask24(base + step);
}
#endif

static inline uint32_t read_dn_sized(const M68kCore* c, uint8_t dn, uint8_t size) {
  return c->d[dn & 7u] & size_mask(size);
}

static inline void write_dn_sized(M68kCore* c, uint8_t dn, uint8_t size, uint32_t value) {
  uint8_t r = (uint8_t)(dn & 7u);
  uint32_t m = size_mask(size);
  c->d[r] = (c->d[r] & ~m) | (value & m);
}

static uint8_t ea_step(uint8_t reg, uint8_t size) {
  if (size == 1u && (reg & 7u) == 7u) return 2u;
  return size;
}

static bool ea_is_data_alterable(uint8_t mode, uint8_t reg) {
  mode &= 7u;
  reg &= 7u;
  if (mode == 1u) return false;
  if (mode == 7u && reg >= 2u) return false;
  return true;
}

static bool ea_is_data_addressing(uint8_t mode, uint8_t reg) {
  mode &= 7u;
  reg &= 7u;
  if (mode == 1u) return false;
  if (mode == 7u && reg == 4u) return false;
  return true;
}

/* Achado #104: o valor devolvido aqui às vezes vira um endereço EFETIVO
 * guardado num registrador (LEA/PEA), e o 68000 de verdade NÃO trunca isso —
 * só o barramento de endereço (24 bits) é limitado, o cálculo em si usa a
 * ULA de 32 bits inteira (mesmo bug do Achado #100, escondido aqui: quem
 * acessa MEMÓRIA de verdade — `ea_read`/`ea_write`, via `mem_read/write` —
 * já mascara de novo na hora do acesso; então tirar o mask24 daqui não
 * quebra leitura/escrita, só corrige LEA/PEA/JMP/JSR). */
static uint32_t indexed_address(M68kCore* c, uint32_t base, uint16_t ext) {
  int32_t disp = sx8((uint8_t)(ext & 0xFFu));
  uint8_t r = (uint8_t)((ext >> 12) & 7u);
  bool is_addr = (ext & 0x8000u) != 0u;
  bool idx_long = (ext & 0x0800u) != 0u;
  int32_t idx = (int32_t)(is_addr ? c->a[r] : c->d[r]);
  if (!idx_long) idx = sx16((uint16_t)idx);
  return (uint32_t)((int32_t)base + disp + idx);
}

static bool ea_read(M68kCore* c, uint8_t mode, uint8_t reg, uint8_t size, bool allow_immediate, uint32_t* out) {
  uint32_t addr;
  uint16_t ext;
  bool cache_for_writeback = c && c->ea_rw_expect_writeback;
  if (!out || (size != 1u && size != 2u && size != 4u)) return false;
  if (cache_for_writeback) {
    c->ea_rw_cache_valid = 0u;
  }
  mode &= 7u;
  reg &= 7u;
  switch (mode) {
    case 0u: *out = c->d[reg] & size_mask(size); return true;
    case 1u: *out = c->a[reg] & size_mask(size); return true;
    case 2u: addr = c->a[reg]; break;
    case 3u: addr = c->a[reg]; c->a[reg] = (c->a[reg] + ea_step(reg, size)); break;
    case 4u: c->a[reg] = (c->a[reg] - ea_step(reg, size)); addr = c->a[reg]; break;
    case 5u: addr = mask24((uint32_t)((int32_t)c->a[reg] + sx16(fetch16(c)))); break;
    case 6u: ext = fetch16(c); addr = indexed_address(c, c->a[reg], ext); break;
    case 7u:
      switch (reg) {
        case 0u: addr = mask24((uint32_t)sx16(fetch16(c))); break;
        case 1u: addr = fetch32(c); break;
        case 2u: {
          /* d16(PC): displacement is relative to the address of the ext word itself */
          uint32_t ext_pc = c->pc; /* address of the ext word */
          int16_t disp = (int16_t)fetch16(c);
          addr = mask24((uint32_t)((int32_t)ext_pc + disp));
          break;
        }
        case 3u: {
          /* d8(PC,Xn): index is relative to the address of the ext word */
          uint32_t ext_pc = c->pc;
          ext = fetch16(c);
          addr = indexed_address(c, ext_pc, ext);
          break;
        }
        case 4u:
          if (!allow_immediate) return false;
          if (size == 1u) *out = (uint32_t)(fetch16(c) & 0x00FFu);
          else if (size == 2u) *out = (uint32_t)fetch16(c);
          else *out = fetch32(c);
          if (cache_for_writeback) {
            c->ea_rw_cache_valid = 0u;
          }
          return true;
        default: return false;
      }
      break;
    default: return false;
  }
  if (cache_for_writeback) {
    c->ea_rw_cache_valid = 1u;
    c->ea_rw_mode = mode;
    c->ea_rw_reg = reg;
    c->ea_rw_size = size;
    c->ea_rw_addr = addr;
  }
  if (size == 1u) *out = (uint32_t)mem_read8(c, addr);
  else if (size == 2u) *out = (uint32_t)data_read16(c, addr);
  else *out = data_read32(c, addr);
  if (c && c->addr_error_pending) return false;
  return true;
}

static bool ea_read_for_writeback(M68kCore* c, uint8_t mode, uint8_t reg, uint8_t size, bool allow_immediate, uint32_t* out) {
  bool ok;
  if (!c) {
    return false;
  }
  c->ea_rw_expect_writeback = 1u;
  c->ea_rw_cache_valid = 0u;
  ok = ea_read(c, mode, reg, size, allow_immediate, out);
  c->ea_rw_expect_writeback = 0u;
  return ok;
}

static bool ea_write(M68kCore* c, uint8_t mode, uint8_t reg, uint8_t size, uint32_t value, bool allow_addr_reg) {
  uint32_t addr;
  uint16_t ext;
  bool used_cache = false;
  if (size != 1u && size != 2u && size != 4u) return false;
  mode &= 7u;
  reg &= 7u;
  if (c &&
      c->ea_rw_cache_valid &&
      c->ea_rw_mode == mode &&
      c->ea_rw_reg == reg &&
      c->ea_rw_size == size) {
    addr = c->ea_rw_addr;
    c->ea_rw_cache_valid = 0u;
    used_cache = true;
  }
  if (!used_cache) {
  switch (mode) {
    case 0u:
      if (c) c->ea_rw_cache_valid = 0u;
      write_dn_sized(c, reg, size, value);
      return true;
    case 1u:
      if (c) c->ea_rw_cache_valid = 0u;
      if (!allow_addr_reg) return false;
      c->a[reg] = (size == 2u) ? ((uint32_t)sx16((uint16_t)value)) : (value);
      return true;
    case 2u: addr = c->a[reg]; break;
    case 3u: addr = c->a[reg]; c->a[reg] = (c->a[reg] + ea_step(reg, size)); break;
    case 4u: c->a[reg] = (c->a[reg] - ea_step(reg, size)); addr = c->a[reg]; break;
    case 5u: addr = mask24((uint32_t)((int32_t)c->a[reg] + sx16(fetch16(c)))); break;
    case 6u: ext = fetch16(c); addr = indexed_address(c, c->a[reg], ext); break;
    case 7u:
      switch (reg) {
        case 0u: addr = mask24((uint32_t)sx16(fetch16(c))); break;
        case 1u: addr = fetch32(c); break;
        default: return false;
      }
      break;
    default: return false;
  }
  }
  if (size == 1u) mem_write8(c, addr, (uint8_t)value);
  else if (size == 2u) data_write16(c, addr, (uint16_t)value);
  else data_write32(c, addr, value);
  if (c) {
    c->ea_rw_cache_valid = 0u;
    if (c->addr_error_pending) return false;
  }
  return true;
}

/* Achado #104: endereço EFETIVO pra LEA/PEA/JMP/JSR — NÃO mascarar em 24
 * bits aqui (ver comentário em `indexed_address`); JMP/JSR já mascaram na
 * hora de atribuir a `c->pc` (só ele vira acesso a barramento de verdade). */
static bool ea_control_address(M68kCore* c, uint8_t mode, uint8_t reg, uint32_t* out_addr) {
  uint16_t ext;
  mode &= 7u;
  reg &= 7u;
  switch (mode) {
    case 2u: *out_addr = c->a[reg]; return true;
    case 5u: *out_addr = (uint32_t)((int32_t)c->a[reg] + sx16(fetch16(c))); return true;
    case 6u: ext = fetch16(c); *out_addr = indexed_address(c, c->a[reg], ext); return true;
    case 7u:
      switch (reg) {
        case 0u: *out_addr = (uint32_t)sx16(fetch16(c)); return true;
        case 1u: *out_addr = fetch32(c); return true;
        case 2u: {
          uint32_t ext_pc = c->pc;
          int16_t disp = (int16_t)fetch16(c);
          *out_addr = (uint32_t)((int32_t)ext_pc + disp);
          return true;
        }
        case 3u: {
          uint32_t ext_pc = c->pc;
          ext = fetch16(c);
          *out_addr = indexed_address(c, ext_pc, ext);
          return true;
        }
        default: return false;
      }
    default:
      return false;
  }
}

static uint32_t reg_index_read(const M68kCore* c, uint8_t idx, uint8_t size) {
  if (idx < 8u) return c->d[idx] & size_mask(size);
  return c->a[idx - 8u] & size_mask(size);
}

static void reg_index_write(M68kCore* c, uint8_t idx, uint8_t size, uint32_t value) {
  uint32_t v = value;
  if (size == 2u) v = (uint32_t)sx16((uint16_t)value);
  if (idx < 8u) c->d[idx] = v;
  else c->a[idx - 8u] = (v);
}

static uint8_t count_bits16(uint16_t v) {
  uint8_t n = 0u;
  while (v) {
    n = (uint8_t)(n + (v & 1u));
    v >>= 1u;
  }
  return n;
}

static bool op_movem(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t mode = (uint8_t)((op >> 3) & 7u);
  uint8_t reg = (uint8_t)(op & 7u);
  bool mem_to_reg;
  uint8_t size;
  uint16_t list;
  uint8_t count;
  uint32_t addr;
  uint16_t ext;
  uint8_t i;

  if ((op & 0xFB80u) != 0x4880u) return false;
  if (mode == 0u) return false; /* EXT.W/EXT.L use this encoding */

  mem_to_reg = (op & 0x0400u) != 0u;
  size = (op & 0x0040u) ? 4u : 2u;
  list = fetch16(c);
  count = count_bits16(list);

  if (mem_to_reg) {
    bool postinc = false;
    switch (mode) {
      case 2u: addr = c->a[reg]; break;
      case 3u: addr = c->a[reg]; postinc = true; break;
      case 5u: addr = mask24((uint32_t)((int32_t)c->a[reg] + sx16(fetch16(c)))); break;
      case 6u: ext = fetch16(c); addr = indexed_address(c, c->a[reg], ext); break;
      case 7u:
        switch (reg) {
          case 0u: addr = mask24((uint32_t)sx16(fetch16(c))); break;
          case 1u: addr = fetch32(c); break;
          case 2u: {
            uint32_t ext_pc = c->pc;
            int16_t disp = (int16_t)fetch16(c);
            addr = mask24((uint32_t)((int32_t)ext_pc + disp));
            break;
          }
          case 3u: {
            uint32_t ext_pc = c->pc;
            ext = fetch16(c);
            addr = indexed_address(c, ext_pc, ext);
            break;
          }
          default: return false;
        }
        break;
      default:
        return false;
    }

    for (i = 0u; i < 16u; i++) {
      if ((list & (1u << i)) == 0u) continue;
      reg_index_write(c, i, size, (size == 2u) ? (uint32_t)data_read16(c, addr) : data_read32(c, addr));
      if (c->addr_error_pending) return false;
      addr = mask24(addr + size);
    }

    if (postinc) c->a[reg] = addr;
  } else {
    bool predec = false;
    switch (mode) {
      case 2u: addr = c->a[reg]; break;
      case 4u: addr = c->a[reg]; predec = true; break;
      case 5u: addr = mask24((uint32_t)((int32_t)c->a[reg] + sx16(fetch16(c)))); break;
      case 6u: ext = fetch16(c); addr = indexed_address(c, c->a[reg], ext); break;
      case 7u:
        switch (reg) {
          case 0u: addr = mask24((uint32_t)sx16(fetch16(c))); break;
          case 1u: addr = fetch32(c); break;
          default: return false;
        }
        break;
      default:
        return false;
    }

    if (predec) {
      /* 68000 predecrement MOVEM uses a reversed register mask: bit 0 is
         A7 and bit 15 is D0. Store high registers first so the final memory
         layout can be restored by the normal postincrement mask. */
      for (i = 0u; i < 16u; i++) {
        uint8_t ridx;
        if ((list & (1u << i)) == 0u) continue;
        ridx = (uint8_t)(15u - i);
        addr = mask24(addr - size);
        if (size == 2u) data_write16(c, addr, (uint16_t)reg_index_read(c, ridx, 2u));
        else data_write32(c, addr, reg_index_read(c, ridx, 4u));
        if (c->addr_error_pending) return false;
      }
      c->a[reg] = addr;
    } else {
      for (i = 0u; i < 16u; i++) {
        if ((list & (1u << i)) == 0u) continue;
        if (size == 2u) data_write16(c, addr, (uint16_t)reg_index_read(c, i, 2u));
        else data_write32(c, addr, reg_index_read(c, i, 4u));
        if (c->addr_error_pending) return false;
        addr = mask24(addr + size);
      }
    }
  }

  *used = (uint32_t)(8u + (uint32_t)count * 4u);
  return true;
}

static bool op_move(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t top = (uint8_t)(op >> 12);
  uint8_t size = (top == 1u) ? 1u : (top == 2u ? 4u : 2u);
  uint8_t src_mode, src_reg, dst_mode, dst_reg;
  uint32_t v;
  if (top < 1u || top > 3u) return false;
  src_mode = (uint8_t)((op >> 3) & 7u);
  src_reg = (uint8_t)(op & 7u);
  dst_mode = (uint8_t)((op >> 6) & 7u);
  dst_reg = (uint8_t)((op >> 9) & 7u);
  if (size == 1u && src_mode == 1u) return false; /* MOVE.B An,<ea> is invalid */
  if (!ea_read(c, src_mode, src_reg, size, true, &v)) return false;
  if (dst_mode == 1u) {
    if (size == 1u) return false;
    c->a[dst_reg] = (size == 2u) ? ((uint32_t)sx16((uint16_t)v)) : (v);
    *used = 8u;
    return true;
  }
  if (!ea_write(c, dst_mode, dst_reg, size, v, false)) return false;
  set_logic_flags(c, v, size);
  *used = 8u;
  return true;
}

static bool op_moveq(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t dn;
  int8_t imm;
  if ((op & 0xF100u) != 0x7000u) return false;
  dn = (uint8_t)((op >> 9) & 7u);
  imm = (int8_t)(op & 0xFFu);
  c->d[dn] = (uint32_t)(int32_t)imm;
  set_logic_flags(c, c->d[dn], 4u);
  *used = 4u;
  return true;
}

static bool op_branch(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t cond, d8;
  int32_t disp;
  uint32_t base_pc; /* PC after opcode word = displacement base */
  if ((op & 0xF000u) != 0x6000u) return false;
  cond = (uint8_t)((op >> 8) & 0x0Fu);
  d8 = (uint8_t)(op & 0xFFu);
  /* On 68000, branch displacement is always relative to (opcode_addr + 2).
     For byte displacement, c->pc is already at opcode_addr + 2.
     For word displacement, we must save the base PC before fetching the ext word. */
  base_pc = c->pc; /* = opcode_addr + 2 */
  disp = (d8 == 0u) ? sx16(fetch16(c)) : sx8(d8);
  if (cond == 0u) {
    /* BRA */
    c->pc = mask24((uint32_t)((int32_t)base_pc + disp));
    *used = 10u;
  } else if (cond == 1u) {
    /* BSR - push return address (after all extension words) */
    push32(c, c->pc);
    if (c->addr_error_pending) return false;
    c->pc = mask24((uint32_t)((int32_t)base_pc + disp));
    *used = 18u;
  } else if (cond_true(c, cond)) {
    c->pc = mask24((uint32_t)((int32_t)base_pc + disp));
    *used = 10u;
  } else {
    *used = 8u;
  }
  return true;
}

static bool op_quick_dbcc(M68kCore* c, uint16_t op, uint32_t* used) {
  if ((op & 0xF000u) != 0x5000u) return false;
  if ((op & 0x00F8u) == 0x00C8u) {
    uint8_t cc = (uint8_t)((op >> 8) & 0x0Fu);
    uint8_t dn = (uint8_t)(op & 7u);
    uint32_t disp_base = c->pc; /* PC before reading displacement = opcode_addr + 2 */
    int16_t disp = (int16_t)fetch16(c);
    if (!cond_true(c, cc)) {
      uint16_t w = (uint16_t)c->d[dn];
      w = (uint16_t)(w - 1u);
      c->d[dn] = (c->d[dn] & 0xFFFF0000u) | w;
      if (w != 0xFFFFu) {
        c->pc = mask24((uint32_t)((int32_t)disp_base + disp));
        *used = 10u;
      } else {
        *used = 12u;
      }
    } else {
      *used = 12u;
    }
    return true;
  }
  if ((op & 0x00C0u) == 0x00C0u) {
    uint8_t cc = (uint8_t)((op >> 8) & 0x0Fu);
    uint8_t mode = (uint8_t)((op >> 3) & 7u);
    uint8_t reg = (uint8_t)(op & 7u);
    uint32_t v = cond_true(c, cc) ? 0xFFu : 0x00u;
    if (mode == 1u) return false; /* Address register direct is invalid for Scc. */
    if (mode == 0u) {
      c->d[reg] = (c->d[reg] & 0xFFFFFF00u) | v;
    } else if (!ea_write(c, mode, reg, 1u, v, false)) {
      return false;
    }
    *used = 8u;
    return true;
  }
  {
    uint8_t val = (uint8_t)((op >> 9) & 7u);
    bool is_sub = (op & 0x0100u) != 0u;
    uint8_t mode = (uint8_t)((op >> 3) & 7u);
    uint8_t reg = (uint8_t)(op & 7u);
    uint8_t sizecode = (uint8_t)((op >> 6) & 3u);
    uint8_t size;
    uint32_t dst, res;
    if (val == 0u) val = 8u;
    if (sizecode == 3u) return false;
    size = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
    if (mode == 1u) {
      c->a[reg] = is_sub ? (c->a[reg] - val) : (c->a[reg] + val);
      *used = 8u;
      return true;
    }
    if (!ea_read_for_writeback(c, mode, reg, size, false, &dst)) return false;
    if (is_sub) {
      res = (dst - val) & size_mask(size);
      set_sub_flags(c, dst, val, res, size);
    } else {
      res = (dst + val) & size_mask(size);
      set_add_flags(c, dst, val, res, size);
    }
    if (!ea_write(c, mode, reg, size, res, false)) return false;
    *used = 8u;
    return true;
  }
}

static bool op_immediate(M68kCore* c, uint16_t op, uint32_t* used) {
  uint16_t base = (uint16_t)(op & 0xFF00u);
  uint8_t mode, reg, sizecode, size;
  uint32_t imm, dst, res;

  if (op == 0x003Cu || op == 0x023Cu || op == 0x0A3Cu) {
    imm = (uint32_t)(fetch16(c) & 0x001Fu);
    if (op == 0x003Cu) c->sr = (uint16_t)((c->sr & ~CCR_MASK) | ((c->sr | imm) & CCR_MASK));
    else if (op == 0x023Cu) c->sr = (uint16_t)((c->sr & ~CCR_MASK) | ((c->sr & imm) & CCR_MASK));
    else c->sr = (uint16_t)((c->sr & ~CCR_MASK) | ((c->sr ^ imm) & CCR_MASK));
    *used = 8u;
    return true;
  }

  if (op == 0x007Cu || op == 0x027Cu || op == 0x0A7Cu) {
    uint16_t new_sr;
    if (!require_supervisor(c)) {
      *used = 34u;
      return true;
    }
    imm = (uint32_t)fetch16(c);
    if (op == 0x007Cu) new_sr = (uint16_t)(c->sr | imm);
    else if (op == 0x027Cu) new_sr = (uint16_t)(c->sr & imm);
    else new_sr = (uint16_t)(c->sr ^ imm);
    write_sr_full(c, new_sr);
    *used = 12u;
    return true;
  }

  if (!(base == 0x0000u || base == 0x0200u || base == 0x0400u ||
        base == 0x0600u || base == 0x0A00u || base == 0x0C00u)) {
    return false;
  }

  sizecode = (uint8_t)((op >> 6) & 3u);
  if (sizecode == 3u) {
    /* 0x00C0.. space maps to CHK2/CMP2 on >=68020, not on 68000.
       Keep the temporary compatibility escape here because Altered Beast
       still depends on forward progress while the real decode/root-cause
       is unresolved. */
    if (base == 0x0000u) {
      (void)fetch16(c);
      *used = 8u;
      return true;
    }
    return false;
  }
  size = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
  mode = (uint8_t)((op >> 3) & 7u);
  reg = (uint8_t)(op & 7u);

  if (!ea_is_data_addressing(mode, reg)) return false;

  if (size == 1u) imm = (uint32_t)(fetch16(c) & 0x00FFu);
  else if (size == 2u) imm = (uint32_t)fetch16(c);
  else imm = fetch32(c);

  if (base == 0x0C00u) {
    if (!ea_is_data_addressing(mode, reg)) return false;
  } else {
    if (!ea_is_data_alterable(mode, reg)) return false;
  }

  if (base == 0x0C00u) {
    if (!ea_read(c, mode, reg, size, false, &dst)) return false;
  } else {
    if (!ea_read_for_writeback(c, mode, reg, size, false, &dst)) return false;
  }

  if (base == 0x0C00u) {
    res = (dst - imm) & size_mask(size);
    set_sub_flags(c, dst, imm, res, size);
    *used = 8u;
    return true;
  }

  if (base == 0x0000u) {
    res = (dst | imm) & size_mask(size);
    set_logic_flags(c, res, size);
  } else if (base == 0x0200u) {
    res = (dst & imm) & size_mask(size);
    set_logic_flags(c, res, size);
  } else if (base == 0x0A00u) {
    res = (dst ^ imm) & size_mask(size);
    set_logic_flags(c, res, size);
  } else if (base == 0x0600u) {
    res = (dst + imm) & size_mask(size);
    set_add_flags(c, dst, imm, res, size);
  } else {
    res = (dst - imm) & size_mask(size);
    set_sub_flags(c, dst, imm, res, size);
  }

  if (!ea_write(c, mode, reg, size, res, false)) return false;
  *used = 8u;
  return true;
}

static bool op_bitops(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t mode;
  uint8_t reg;
  uint8_t kind;
  uint32_t bitnum;
  uint32_t data;
  uint32_t bits;
  uint32_t bitmask;

  if ((op & 0xFF00u) == 0x0800u) {
    kind = (uint8_t)((op >> 6) & 3u);
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_is_data_addressing(mode, reg)) return false;
    bitnum = (uint32_t)fetch16(c);

    if (mode == 0u) {
      bits = 32u;
      data = c->d[reg];
    } else {
      bits = 8u;
      if (kind != 0u) {
        if (!ea_read_for_writeback(c, mode, reg, 1u, false, &data)) return false;
      } else {
        if (!ea_read(c, mode, reg, 1u, false, &data)) return false;
      }
    }

    bitmask = 1u << (bitnum % bits);
    set_flag(c, SR_Z, (data & bitmask) == 0u);

    if (kind == 1u) data ^= bitmask;      /* BCHG */
    else if (kind == 2u) data &= ~bitmask;/* BCLR */
    else if (kind == 3u) data |= bitmask; /* BSET */

    if (kind != 0u) {
      if (mode == 0u) c->d[reg] = data;
      else if (!ea_write(c, mode, reg, 1u, data, false)) return false;
    }

    *used = 8u;
    return true;
  }

  if ((op & 0xF100u) == 0x0100u) {
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    kind = (uint8_t)((op >> 6) & 3u);
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);

    if (mode == 1u) {
      /* MOVEP overlap:
         kind 0/1: mem->Dn (W/L)
         kind 2/3: Dn->mem (W/L) */
      int16_t disp = (int16_t)fetch16(c);
      uint32_t addr = mask24((uint32_t)((int32_t)c->a[reg] + disp));
      uint32_t dv = c->d[dn];
      if (kind == 0u) {
        uint16_t w = (uint16_t)(((uint16_t)mem_read8(c, addr) << 8) |
                                (uint16_t)mem_read8(c, addr + 2u));
        write_dn_sized(c, dn, 2u, w);
        *used = 16u;
      } else if (kind == 1u) {
        uint32_t l = ((uint32_t)mem_read8(c, addr) << 24) |
                     ((uint32_t)mem_read8(c, addr + 2u) << 16) |
                     ((uint32_t)mem_read8(c, addr + 4u) << 8) |
                     (uint32_t)mem_read8(c, addr + 6u);
        c->d[dn] = l;
        *used = 24u;
      } else if (kind == 2u) {
        mem_write8(c, addr, (uint8_t)((dv >> 8) & 0xFFu));
        mem_write8(c, addr + 2u, (uint8_t)(dv & 0xFFu));
        *used = 16u;
      } else {
        mem_write8(c, addr, (uint8_t)((dv >> 24) & 0xFFu));
        mem_write8(c, addr + 2u, (uint8_t)((dv >> 16) & 0xFFu));
        mem_write8(c, addr + 4u, (uint8_t)((dv >> 8) & 0xFFu));
        mem_write8(c, addr + 6u, (uint8_t)(dv & 0xFFu));
        *used = 24u;
      }
      return true;
    }
    bitnum = c->d[dn];

    if (mode == 0u) {
      bits = 32u;
      data = c->d[reg];
    } else {
      bits = 8u;
      if (kind != 0u) {
        if (!ea_read_for_writeback(c, mode, reg, 1u, false, &data)) return false;
      } else {
        if (!ea_read(c, mode, reg, 1u, false, &data)) return false;
      }
    }

    bitmask = 1u << (bitnum % bits);
    set_flag(c, SR_Z, (data & bitmask) == 0u);

    if (kind == 1u) data ^= bitmask;
    else if (kind == 2u) data &= ~bitmask;
    else if (kind == 3u) data |= bitmask;

    if (kind != 0u) {
      if (mode == 0u) c->d[reg] = data;
      else if (!ea_write(c, mode, reg, 1u, data, false)) return false;
    }

    *used = 8u;
    return true;
  }

  return false;
}

static bool op_misc(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t mode, reg, sizecode, size;
  uint32_t addr, v, res;
  if ((op & 0xF000u) != 0x4000u) return false;

  if (op == 0x4E70u) {
    if (!require_supervisor(c)) { *used = 34u; return true; }
    if (c->reset_instr) {
      c->reset_instr(c->user);
    }
    *used = 132u;
    return true;
  }
  if (op == 0x4E71u) { *used = 4u; return true; }
  if (op == 0x4E75u) {
    uint32_t new_pc = pop32(c);
    if (c->addr_error_pending) return false;
    c->pc = mask24(new_pc);
    *used = 16u;
    return true;
  }
  if (op == 0x4E76u) {
    if (get_flag(c, SR_V)) {
      take_exception(c, 7u);
      c->last_exception_pc = c->last_opcode_pc;
      c->last_exception_vector = 7u;
      c->last_exception_opcode = op;
      *used = 34u;
    } else {
      *used = 4u;
    }
    return true;
  }
  if (op == 0x4E77u) {
    uint16_t ccr = (uint16_t)(pop16(c) & CCR_MASK);
    if (c->addr_error_pending) return false;
    uint32_t new_pc = pop32(c);
    if (c->addr_error_pending) return false;
    c->sr = (uint16_t)((c->sr & (uint16_t)~CCR_MASK) | ccr);
#if !M68K_STRICT_HARDWARE
    if ((new_pc & 1u) != 0u) {
      /* Malformed return frame (odd PC): force forward-aligned recovery. */
      c->unknown_count++;
      c->last_unknown_pc = new_pc;
      c->last_unknown_opcode = op;
      new_pc = sanitize_trap_return_pc(c, op, new_pc);
    }
#endif
    c->pc = mask24(new_pc);
    *used = 20u;
    return true;
  }
  if (op == 0x4E73u) {
    uint16_t new_sr;
    uint32_t new_pc;
    if (!require_supervisor(c)) { *used = 34u; return true; }
    new_sr = pop16(c);
    if (c->addr_error_pending) return false;
    new_pc = pop32(c);
    if (c->addr_error_pending) return false;
#if !M68K_STRICT_HARDWARE
    if ((new_pc & 1u) != 0u) {
      /* Same safeguard as RTR: keep forward progress on broken exception frames. */
      c->unknown_count++;
      c->last_unknown_pc = new_pc;
      c->last_unknown_opcode = op;
      new_pc = sanitize_trap_return_pc(c, op, new_pc);
    }
#endif
    write_sr_full(c, new_sr);
    c->pc = mask24(new_pc);
    c->group0_exception_active = 0u;
    *used = 20u;
    return true;
  }
  if (op == 0x4E72u) {
    uint16_t new_sr;
    if (!require_supervisor(c)) { *used = 34u; return true; }
    new_sr = fetch16(c);
    write_sr_full(c, new_sr);
    c->halted = true;
    *used = 4u;
    return true;
  }
  if ((op & 0xFFF0u) == 0x4E40u) {
    uint8_t vec = (uint8_t)(32u + (op & 0x000Fu));
    take_exception(c, vec);
    c->last_exception_pc = mask24(c->pc - 2u);
    c->last_exception_vector = vec;
    c->last_exception_opcode = op;
    *used = 34u;
    return true;
  }

  if ((op & 0xFFF8u) == 0x4E50u) {
    uint8_t an = (uint8_t)(op & 7u);
    int16_t disp = (int16_t)fetch16(c);
    push32(c, c->a[an]);
    if (c->addr_error_pending) return false;
    c->a[an] = c->a[7];
    c->a[7] = ((uint32_t)((int32_t)c->a[7] + disp));
    *used = 16u;
    return true;
  }
  if ((op & 0xFFF8u) == 0x4E58u) {
    uint8_t an = (uint8_t)(op & 7u);
    c->a[7] = c->a[an];
    c->a[an] = pop32(c);
    if (c->addr_error_pending) return false;
    *used = 12u;
    return true;
  }
  if ((op & 0xFFF8u) == 0x4E60u) {
    uint8_t an = (uint8_t)(op & 7u);
    if (!require_supervisor(c)) { *used = 34u; return true; }
    c->usp = mask24(c->a[an]);
    *used = 4u;
    return true;
  }
  if ((op & 0xFFF8u) == 0x4E68u) {
    uint8_t an = (uint8_t)(op & 7u);
    if (!require_supervisor(c)) { *used = 34u; return true; }
    c->a[an] = (c->usp);
    *used = 4u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x4800u) {
    /* NBCD <ea> */
    uint32_t dst;
    uint8_t out8;
    bool borrow = false;
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (mode == 1u) return false;
    if (!ea_read_for_writeback(c, mode, reg, 1u, false, &dst)) return false;
    out8 = bcd_sub(0u, (uint8_t)dst, get_flag(c, SR_X), &borrow);
    set_flag(c, SR_C, borrow);
    set_flag(c, SR_X, borrow);
    set_flag(c, SR_N, (out8 & 0x80u) != 0u);
    set_flag(c, SR_V, false);
    if (out8 != 0u) set_flag(c, SR_Z, false);
    if (!ea_write(c, mode, reg, 1u, out8, false)) return false;
    *used = 8u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x4AC0u) {
    /* TAS <ea> */
    uint32_t dst;
    uint8_t out8;
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (mode == 1u) return false;
    if (!ea_read_for_writeback(c, mode, reg, 1u, false, &dst)) return false;
    set_logic_flags(c, dst, 1u);
    out8 = (uint8_t)(dst | 0x80u);
    if (!ea_write(c, mode, reg, 1u, out8, false)) return false;
    *used = 10u;
    return true;
  }
  if (op_movem(c, op, used)) return true;

  if ((op & 0xFFC0u) == 0x4840u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (ea_control_address(c, mode, reg, &addr)) {
      push32(c, addr);
      if (c->addr_error_pending) return false;
      *used = 12u;
      return true;
    }
  }
  if ((op & 0xFFF8u) == 0x4840u) {
    uint8_t dn = (uint8_t)(op & 7u);
    uint32_t x = c->d[dn];
    c->d[dn] = (x << 16) | (x >> 16);
    set_logic_flags(c, c->d[dn], 4u);
    *used = 4u;
    return true;
  }
  if ((op & 0xFFF8u) == 0x4880u) {
    uint8_t dn = (uint8_t)(op & 7u);
    int16_t w = (int16_t)(int8_t)(c->d[dn] & 0xFFu);
    c->d[dn] = (c->d[dn] & 0xFFFF0000u) | (uint16_t)w;
    set_logic_flags(c, c->d[dn], 2u);
    *used = 4u;
    return true;
  }
  if ((op & 0xFFF8u) == 0x48C0u) {
    uint8_t dn = (uint8_t)(op & 7u);
    c->d[dn] = (uint32_t)sx16((uint16_t)c->d[dn]);
    set_logic_flags(c, c->d[dn], 4u);
    *used = 4u;
    return true;
  }

  if ((op & 0xFFC0u) == 0x4EC0u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_control_address(c, mode, reg, &addr)) return false;
    c->pc = mask24(addr);
    *used = 10u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x4E80u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_control_address(c, mode, reg, &addr)) return false;
    push32(c, c->pc);
    if (c->addr_error_pending) return false;
    c->pc = mask24(addr);
    *used = 18u;
    return true;
  }
  if ((op & 0xF1C0u) == 0x41C0u) {
    uint8_t an = (uint8_t)((op >> 9) & 7u);
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_control_address(c, mode, reg, &addr)) return false;
    c->a[an] = addr;
    *used = 8u;
    return true;
  }
  if ((op & 0xF1C0u) == 0x4180u) {
    /* CHK.W <ea>,Dn */
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    int16_t upper;
    int16_t dval;
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_read(c, mode, reg, 2u, false, &v)) return false;
    upper = (int16_t)(uint16_t)v;
    dval = (int16_t)(uint16_t)c->d[dn];
    if (dval < 0 || dval > upper) {
      set_flag(c, SR_N, dval < 0);
      if (vector_is_recoverable(c, 6u, op, c->last_opcode_pc, used)) {
        /* Forward progress path for invalid CHK vector handlers. */
      } else {
        take_exception(c, 6u);
        c->last_exception_pc = c->last_opcode_pc;
        c->last_exception_vector = 6u;
        c->last_exception_opcode = op;
        *used = 34u;
      }
    } else {
      *used = 10u;
    }
    return true;
  }
  if ((op & 0xFFC0u) == 0x40C0u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_write(c, mode, reg, 2u, c->sr, false)) return false;
    *used = 8u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x44C0u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_read(c, mode, reg, 2u, true, &v)) return false;
    c->sr = (uint16_t)((c->sr & ~CCR_MASK) | (v & CCR_MASK));
    *used = 8u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x42C0u) {
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!ea_write(c, mode, reg, 2u, (uint32_t)(c->sr & CCR_MASK), false)) return false;
    *used = 8u;
    return true;
  }
  if ((op & 0xFFC0u) == 0x46C0u) {
    uint16_t new_sr;
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (!require_supervisor(c)) { *used = 34u; return true; }
    if (!ea_read(c, mode, reg, 2u, true, &v)) return false;
    new_sr = (uint16_t)v;
    write_sr_full(c, new_sr);
    *used = 8u;
    return true;
  }

  if ((op & 0xFF00u) == 0x4000u || (op & 0xFF00u) == 0x4200u || (op & 0xFF00u) == 0x4400u ||
      (op & 0xFF00u) == 0x4600u || (op & 0xFF00u) == 0x4A00u) {
    sizecode = (uint8_t)((op >> 6) & 3u);
    if (sizecode == 3u) return false;
    size = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if ((op & 0xFF00u) == 0x4A00u) {
      if (!ea_read(c, mode, reg, size, false, &v)) return false;
    } else {
      if (!ea_read_for_writeback(c, mode, reg, size, false, &v)) return false;
    }
    if ((op & 0xFF00u) == 0x4A00u) {
      set_logic_flags(c, v, size);
      *used = 4u;
      return true;
    }
    if ((op & 0xFF00u) == 0x4000u) {
      uint32_t m = size_mask(size);
      uint32_t s = sign_mask(size);
      uint32_t srcm = v & m;
      uint32_t full_src = srcm + (get_flag(c, SR_X) ? 1u : 0u);
      uint32_t src_eff = full_src & m;
      bool borrow = (full_src != 0u);
      bool ovf;
      res = (0u - src_eff) & m;
      ovf = ((src_eff & res & s) != 0u);
      set_flag(c, SR_N, (res & s) != 0u);
      if (res != 0u) set_flag(c, SR_Z, false); /* NEGX preserves Z when result is zero */
      set_flag(c, SR_V, ovf);
      set_flag(c, SR_C, borrow);
      set_flag(c, SR_X, borrow);
    } else if ((op & 0xFF00u) == 0x4200u) {
      res = 0u;
      set_logic_flags(c, res, size);
    } else if ((op & 0xFF00u) == 0x4600u) {
      res = (~v) & size_mask(size);
      set_logic_flags(c, res, size);
    } else {
      res = (0u - v) & size_mask(size);
      set_sub_flags(c, 0u, v, res, size);
    }
    if (!ea_write(c, mode, reg, size, res, false)) return false;
    *used = 8u;
    return true;
  }

  return false;
}

static bool op_logic_arith(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t top = (uint8_t)(op >> 12);
  uint8_t dn = (uint8_t)((op >> 9) & 7u);
  uint8_t opmode = (uint8_t)((op >> 6) & 7u);
  uint8_t mode = (uint8_t)((op >> 3) & 7u);
  uint8_t reg = (uint8_t)(op & 7u);
  uint8_t size;
  uint32_t src, dst, res;

  if (!(top == 0x8u || top == 0x9u || top == 0xBu || top == 0xCu || top == 0xDu)) return false;

  if ((op & 0xF138u) == 0xB108u && opmode != 7u) {
    /* CMPM.(B/W/L) (Ay)+,(Ax)+ */
    uint8_t sizecode = (uint8_t)((op >> 6) & 3u);
    uint8_t src_an = (uint8_t)(op & 7u);
    uint8_t dst_an = (uint8_t)((op >> 9) & 7u);
    uint8_t ssz;
    uint32_t srcv;
    uint32_t dstv;
    uint32_t rv;
    ssz = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
    if (ssz == 1u) {
      srcv = mem_read8(c, c->a[src_an]);
      dstv = mem_read8(c, c->a[dst_an]);
    } else if (ssz == 2u) {
      srcv = data_read16(c, c->a[src_an]);
      dstv = data_read16(c, c->a[dst_an]);
    } else {
      srcv = data_read32(c, c->a[src_an]);
      dstv = data_read32(c, c->a[dst_an]);
    }
    if (c->addr_error_pending) return false;
    c->a[src_an] = (c->a[src_an] + ea_step(src_an, ssz));
    c->a[dst_an] = (c->a[dst_an] + ea_step(dst_an, ssz));
    rv = (dstv - srcv) & size_mask(ssz);
    set_sub_flags(c, dstv, srcv, rv, ssz);
    *used = (ssz == 4u) ? 20u : 12u;
    return true;
  }

  if ((op & 0xF130u) == 0x9100u || (op & 0xF130u) == 0xD100u) {
    /* SUBX / ADDX */
    bool is_sub = ((op & 0xF000u) == 0x9000u);
    bool rm = (op & 0x0008u) != 0u;
    uint8_t sizecode = (uint8_t)((op >> 6) & 3u);
    uint8_t sreg = (uint8_t)(op & 7u);
    uint8_t dreg = (uint8_t)((op >> 9) & 7u);
    uint8_t ssz;
    uint32_t x = get_flag(c, SR_X) ? 1u : 0u;
    uint32_t srcv;
    uint32_t dstv;
    uint32_t rv;
    uint32_t m;
    uint32_t s;
    bool cf;
    bool vf;

    /* Important: this mask is intentionally broad and also matches some
       ADDA opcodes (e.g. 0xD9C0). Sizecode=3 is invalid for ADDX/SUBX,
       so in that case we must NOT fail the whole decoder path here. */
    if (sizecode == 3u) {
      /* Not SUBX/ADDX; let later arithmetic decoders handle it. */
    } else {
    ssz = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
    m = size_mask(ssz);
    s = sign_mask(ssz);

    if (!rm) {
      srcv = read_dn_sized(c, sreg, ssz);
      dstv = read_dn_sized(c, dreg, ssz);
    } else {
      uint8_t step_s = ea_step(sreg, ssz);
      uint8_t step_d = ea_step(dreg, ssz);
      c->a[sreg] = (c->a[sreg] - step_s);
      c->a[dreg] = (c->a[dreg] - step_d);
      if (ssz == 1u) {
        srcv = mem_read8(c, c->a[sreg]);
        dstv = mem_read8(c, c->a[dreg]);
      } else if (ssz == 2u) {
        srcv = data_read16(c, c->a[sreg]);
        dstv = data_read16(c, c->a[dreg]);
      } else {
        srcv = data_read32(c, c->a[sreg]);
        dstv = data_read32(c, c->a[dreg]);
      }
      if (c->addr_error_pending) return false;
    }

    if (is_sub) {
      uint32_t srcx = (srcv & m) + x;
      rv = (dstv - srcv - x) & m;
      cf = srcx > (dstv & m);
      vf = (((dstv ^ srcx) & ((dstv ^ rv) & s)) != 0u);
    } else {
      uint32_t srcx = ((srcv & m) + x) & m;
      rv = (dstv + srcv + x) & m;
      cf = (((uint64_t)(dstv & m) + (uint64_t)(srcv & m) + (uint64_t)x) > (uint64_t)m);
      vf = ((~((dstv & m) ^ srcx) & ((dstv & m) ^ rv) & s) != 0u);
    }

    set_flag(c, SR_N, (rv & s) != 0u);
    if (rv != 0u) set_flag(c, SR_Z, false);
    set_flag(c, SR_V, vf);
    set_flag(c, SR_C, cf);
    set_flag(c, SR_X, cf);

    if (!rm) {
      write_dn_sized(c, dreg, ssz, rv);
      *used = 8u;
    } else {
      if (ssz == 1u) mem_write8(c, c->a[dreg], (uint8_t)rv);
      else if (ssz == 2u) data_write16(c, c->a[dreg], (uint16_t)rv);
      else data_write32(c, c->a[dreg], rv);
      if (c->addr_error_pending) return false;
      *used = 18u;
    }
    return true;
    }
  }

  if ((op & 0xF1F8u) == 0x8100u || (op & 0xF1F8u) == 0x8108u) {
    /* SBCD Dy,Dx / SBCD -(Ay),-(Ax) */
    uint8_t sx = (uint8_t)(op & 7u);
    uint8_t dx = (uint8_t)((op >> 9) & 7u);
    bool rm = (op & 0x0008u) != 0u;
    uint8_t src8, dst8, out8;
    bool borrow = false;

    if (!rm) {
      src8 = (uint8_t)(c->d[sx] & 0xFFu);
      dst8 = (uint8_t)(c->d[dx] & 0xFFu);
      out8 = bcd_sub(dst8, src8, get_flag(c, SR_X), &borrow);
      c->d[dx] = (c->d[dx] & 0xFFFFFF00u) | out8;
    } else {
      c->a[sx] = (c->a[sx] - 1u);
      c->a[dx] = (c->a[dx] - 1u);
      src8 = mem_read8(c, c->a[sx]);
      dst8 = mem_read8(c, c->a[dx]);
      out8 = bcd_sub(dst8, src8, get_flag(c, SR_X), &borrow);
      mem_write8(c, c->a[dx], out8);
    }

    set_flag(c, SR_C, borrow);
    set_flag(c, SR_X, borrow);
    set_flag(c, SR_N, (out8 & 0x80u) != 0u);
    set_flag(c, SR_V, false);
    if (out8 != 0u) set_flag(c, SR_Z, false);
    *used = rm ? 18u : 6u;
    return true;
  }

  if ((op & 0xF1F8u) == 0xC100u || (op & 0xF1F8u) == 0xC108u) {
    /* ABCD Dy,Dx / ABCD -(Ay),-(Ax) */
    uint8_t sx = (uint8_t)(op & 7u);
    uint8_t dx = (uint8_t)((op >> 9) & 7u);
    bool rm = (op & 0x0008u) != 0u;
    uint8_t src8, dst8, out8;
    bool carry = false;

    if (!rm) {
      src8 = (uint8_t)(c->d[sx] & 0xFFu);
      dst8 = (uint8_t)(c->d[dx] & 0xFFu);
      out8 = bcd_add(dst8, src8, get_flag(c, SR_X), &carry);
      c->d[dx] = (c->d[dx] & 0xFFFFFF00u) | out8;
    } else {
      c->a[sx] = (c->a[sx] - 1u);
      c->a[dx] = (c->a[dx] - 1u);
      src8 = mem_read8(c, c->a[sx]);
      dst8 = mem_read8(c, c->a[dx]);
      out8 = bcd_add(dst8, src8, get_flag(c, SR_X), &carry);
      mem_write8(c, c->a[dx], out8);
    }

    set_flag(c, SR_C, carry);
    set_flag(c, SR_X, carry);
    set_flag(c, SR_N, (out8 & 0x80u) != 0u);
    set_flag(c, SR_V, false);
    if (out8 != 0u) set_flag(c, SR_Z, false);
    *used = rm ? 18u : 6u;
    return true;
  }

  if ((op & 0xF1F8u) == 0xC140u) {
    /* EXG Dn,Dm */
    uint8_t rx = (uint8_t)(op & 7u);
    uint32_t t = c->d[dn];
    c->d[dn] = c->d[rx];
    c->d[rx] = t;
    *used = 6u;
    return true;
  }
  if ((op & 0xF1F8u) == 0xC148u) {
    /* EXG An,Am */
    uint8_t rx = (uint8_t)(op & 7u);
    uint32_t t = c->a[dn];
    c->a[dn] = c->a[rx];
    c->a[rx] = t;
    *used = 6u;
    return true;
  }
  if ((op & 0xF1F8u) == 0xC188u) {
    /* EXG Dn,Am */
    uint8_t rx = (uint8_t)(op & 7u);
    uint32_t t = c->d[dn];
    c->d[dn] = c->a[rx];
    c->a[rx] = (t);
    *used = 6u;
    return true;
  }

  if ((op & 0xF1C0u) == 0x80C0u || (op & 0xF1C0u) == 0x81C0u) {
    /* DIVU/DIVS <ea>.w,Dn */
    uint32_t s32;
    uint16_t divw;
    uint32_t dividend;
    bool is_signed = (op & 0x0100u) != 0u;

    /* DIVU/DIVS source allows immediate mode (e.g. DIVU #imm,Dn). */
    if (!ea_read(c, mode, reg, 2u, true, &s32)) return false;
    divw = (uint16_t)s32;
    c->last_div_src = divw; /* Achado #101 */
    if (divw == 0u) {
      if (vector_is_recoverable(c, 5u, op, c->last_opcode_pc, used)) {
        /* Forward progress path for invalid DIV0 vector handlers. */
      } else {
        take_exception(c, 5u);
        c->last_exception_pc = c->last_opcode_pc;
        c->last_exception_vector = 5u;
        c->last_exception_opcode = op;
        *used = 38u;
      }
      return true;
    }

    dividend = c->d[dn];
    set_flag(c, SR_C, false);

    if (!is_signed) {
      uint32_t rem_u = dividend % (uint32_t)divw;
      uint32_t quot_u = dividend / (uint32_t)divw;
      if (quot_u > 0xFFFFu) {
        set_flag(c, SR_V, true);
      } else {
        c->d[dn] = (rem_u << 16) | (quot_u & 0xFFFFu);
        set_flag(c, SR_V, false);
        set_flag(c, SR_N, (quot_u & 0x8000u) != 0u);
        set_flag(c, SR_Z, (quot_u & 0xFFFFu) == 0u);
      }
    } else {
      int32_t divisor_s = sx16(divw);
      int32_t dividend_s = (int32_t)dividend;
      int32_t quot_s = dividend_s / divisor_s;
      int32_t rem_s = dividend_s % divisor_s;
      if (quot_s < -32768 || quot_s > 32767) {
        set_flag(c, SR_V, true);
      } else {
        c->d[dn] = (((uint32_t)((uint16_t)rem_s)) << 16) | ((uint16_t)quot_s);
        set_flag(c, SR_V, false);
        set_flag(c, SR_N, (((uint16_t)quot_s) & 0x8000u) != 0u);
        set_flag(c, SR_Z, ((uint16_t)quot_s) == 0u);
      }
    }

    *used = 140u;
    return true;
  }

  if ((op & 0xF1C0u) == 0xC0C0u || (op & 0xF1C0u) == 0xC1C0u) {
    /* MULU/MULS <ea>.w,Dn */
    uint32_t s32;
    bool is_signed = (op & 0x0100u) != 0u;
    /* MULU/MULS source allows immediate mode (e.g. MULU #imm,Dn). */
    if (!ea_read(c, mode, reg, 2u, true, &s32)) return false;
    if (!is_signed) {
      uint32_t r = (uint32_t)((uint16_t)c->d[dn]) * (uint32_t)((uint16_t)s32);
      c->d[dn] = r;
    } else {
      int32_t r = sx16((uint16_t)c->d[dn]) * sx16((uint16_t)s32);
      c->d[dn] = (uint32_t)r;
    }
    set_logic_flags(c, c->d[dn], 4u);
    *used = 70u;
    return true;
  }

  if (top == 0xBu && (opmode == 3u || opmode == 7u)) {
    uint8_t s = (opmode == 3u) ? 2u : 4u;
    int32_t sval;
    if (!ea_read(c, mode, reg, s, true, &src)) return false;
    sval = (s == 2u) ? sx16((uint16_t)src) : (int32_t)src;
    res = c->a[dn] - (uint32_t)sval;
    set_sub_flags(c, c->a[dn], (uint32_t)sval, res, 4u);
    *used = 8u;
    return true;
  }

  if ((top == 0x9u || top == 0xDu) && (opmode == 3u || opmode == 7u)) {
    uint8_t s = (opmode == 3u) ? 2u : 4u;
    int32_t sval;
    if (!ea_read(c, mode, reg, s, true, &src)) return false;
    sval = (s == 2u) ? sx16((uint16_t)src) : (int32_t)src;
    if (top == 0x9u) c->a[dn] = (c->a[dn] - (uint32_t)sval);
    else c->a[dn] = (c->a[dn] + (uint32_t)sval);
    *used = 8u;
    return true;
  }

  if (opmode == 0u || opmode == 4u) size = 1u;
  else if (opmode == 1u || opmode == 5u) size = 2u;
  else if (opmode == 2u || opmode == 6u) size = 4u;
  else return false;

  if (opmode <= 2u) {
    if (mode == 1u) {
      /* An as source is valid for word/long arithmetic compares (e.g. 0x9288),
         but never for byte-sized form and not for OR/AND/EOR data ops. */
      if (size == 1u) return false;
      if (!(top == 0x9u || top == 0xBu || top == 0xDu)) return false;
    }
    if (!ea_read(c, mode, reg, size, true, &src)) return false;
    dst = read_dn_sized(c, dn, size);
    if (top == 0x8u) {
      res = (dst | src) & size_mask(size);
      set_logic_flags(c, res, size);
      write_dn_sized(c, dn, size, res);
    } else if (top == 0xCu) {
      res = (dst & src) & size_mask(size);
      set_logic_flags(c, res, size);
      write_dn_sized(c, dn, size, res);
    } else if (top == 0x9u) {
      res = (dst - src) & size_mask(size);
      set_sub_flags(c, dst, src, res, size);
      write_dn_sized(c, dn, size, res);
    } else if (top == 0xDu) {
      res = (dst + src) & size_mask(size);
      set_add_flags(c, dst, src, res, size);
      write_dn_sized(c, dn, size, res);
    } else {
      res = (dst - src) & size_mask(size); /* CMP */
      set_sub_flags(c, dst, src, res, size);
    }
    *used = 8u;
    return true;
  }

  if (!ea_is_data_alterable(mode, reg)) return false;
  if (!ea_read_for_writeback(c, mode, reg, size, false, &dst)) return false;
  src = read_dn_sized(c, dn, size);

  if (top == 0x8u) {
    res = (dst | src) & size_mask(size);
    set_logic_flags(c, res, size);
  } else if (top == 0xCu) {
    res = (dst & src) & size_mask(size);
    set_logic_flags(c, res, size);
  } else if (top == 0x9u) {
    res = (dst - src) & size_mask(size);
    set_sub_flags(c, dst, src, res, size);
  } else if (top == 0xDu) {
    res = (dst + src) & size_mask(size);
    set_add_flags(c, dst, src, res, size);
  } else {
    res = (dst ^ src) & size_mask(size); /* EOR */
    set_logic_flags(c, res, size);
  }

  if (!ea_write(c, mode, reg, size, res, false)) return false;
  *used = 8u;
  return true;
}

static bool op_shift(M68kCore* c, uint16_t op, uint32_t* used) {
  uint8_t top = (uint8_t)(op >> 12);
  uint8_t type, sizecode, size, reg, mode;
  bool by_reg, right;
  uint32_t count, value, m, s;
  bool carry = false, ov = false;
  if (top != 0xEu) return false;
  if ((op & 0x00C0u) == 0x00C0u) {
    uint32_t res;
    bool x_in;
    type = (uint8_t)((op >> 9) & 0x3u);
    right = (op & 0x0100u) == 0u;
    mode = (uint8_t)((op >> 3) & 7u);
    reg = (uint8_t)(op & 7u);
    if (mode <= 1u) return false;
    if (mode == 7u && reg >= 2u) return false;
    if (!ea_read_for_writeback(c, mode, reg, 2u, false, &value)) return false;
    res = value & 0xFFFFu;
    x_in = get_flag(c, SR_X);

    switch (type) {
      case 0u: /* AS */
        if (right) {
          carry = (res & 1u) != 0u;
          res = (res >> 1) | (res & 0x8000u);
          ov = false;
        } else {
          uint32_t old_msb = res & 0x8000u;
          res = (res << 1) & 0xFFFFu;
          carry = old_msb != 0u;
          ov = (((res & 0x8000u) != 0u) != (old_msb != 0u));
        }
        set_flag(c, SR_X, carry);
        break;
      case 1u: /* LS */
        if (right) {
          carry = (res & 1u) != 0u;
          res >>= 1;
        } else {
          carry = (res & 0x8000u) != 0u;
          res = (res << 1) & 0xFFFFu;
        }
        ov = false;
        set_flag(c, SR_X, carry);
        break;
      case 2u: /* ROX */
        if (right) {
          carry = (res & 1u) != 0u;
          res = (res >> 1) | (x_in ? 0x8000u : 0u);
        } else {
          carry = (res & 0x8000u) != 0u;
          res = ((res << 1) & 0xFFFFu) | (x_in ? 1u : 0u);
        }
        ov = false;
        set_flag(c, SR_X, carry);
        break;
      default: /* RO */
        if (right) {
          carry = (res & 1u) != 0u;
          res = (res >> 1) | (carry ? 0x8000u : 0u);
        } else {
          carry = (res & 0x8000u) != 0u;
          res = ((res << 1) & 0xFFFFu) | (carry ? 1u : 0u);
        }
        ov = false;
        break;
    }

    set_flag(c, SR_C, carry);
    set_flag(c, SR_V, ov);
    set_nz(c, res, 2u);
    if (!ea_write(c, mode, reg, 2u, res, false)) return false;
    *used = 8u;
    return true;
  }

  type = (uint8_t)((op >> 3) & 0x3u);
  sizecode = (uint8_t)((op >> 6) & 3u);
  if (sizecode == 3u) return false;
  size = (sizecode == 0u) ? 1u : (sizecode == 1u ? 2u : 4u);
  reg = (uint8_t)(op & 7u);
  by_reg = (op & 0x0020u) != 0u;
  right = (op & 0x0100u) == 0u;
  if (by_reg) count = c->d[(op >> 9) & 7u] & 63u;
  else { count = (op >> 9) & 7u; if (count == 0u) count = 8u; }
  value = read_dn_sized(c, reg, size);
  m = size_mask(size);
  s = sign_mask(size);
  if (count > 0u) {
    uint32_t i;
    bool x_in = get_flag(c, SR_X);
    for (i = 0u; i < count; i++) {
      switch (type) {
        case 0u: /* AS */
          if (right) {
            carry = (value & 1u) != 0u;
            value = (value >> 1) | (value & s);
            ov = false;
          } else {
            uint32_t old_msb = value & s;
            value = (value << 1) & m;
            carry = old_msb != 0u;
            if (((value & s) != 0u) != (old_msb != 0u)) ov = true;
          }
          x_in = carry;
          break;
        case 1u: /* LS */
          if (right) {
            carry = (value & 1u) != 0u;
            value >>= 1;
          } else {
            carry = (value & s) != 0u;
            value = (value << 1) & m;
          }
          ov = false;
          x_in = carry;
          break;
        case 2u: /* ROX */
          if (right) {
            carry = (value & 1u) != 0u;
            value = (value >> 1) | (x_in ? s : 0u);
          } else {
            carry = (value & s) != 0u;
            value = ((value << 1) & m) | (x_in ? 1u : 0u);
          }
          ov = false;
          x_in = carry;
          break;
        default: /* RO */
          if (right) {
            carry = (value & 1u) != 0u;
            value = (value >> 1) | (carry ? s : 0u);
          } else {
            carry = (value & s) != 0u;
            value = ((value << 1) & m) | (carry ? 1u : 0u);
          }
          ov = false;
          break;
      }
      value &= m;
    }
    set_flag(c, SR_C, carry);
    if (type != 3u) {
      set_flag(c, SR_X, x_in);
    }
    set_flag(c, SR_V, ov);
  }
  set_nz(c, value, size);
  write_dn_sized(c, reg, size, value);
  *used = 6u;
  return true;
}

void m68k_core_init(M68kCore* c,
                    M68kRead8Fn read8, M68kRead16Fn read16,
                    M68kWrite8Fn write8, M68kWrite16Fn write16,
                    void* user) {
  if (!c) return;
  memset(c, 0, sizeof(*c));
  c->read8 = read8;
  c->read16 = read16;
  c->write8 = write8;
  c->write16 = write16;
  c->reset_instr = NULL;
  c->user = user;
}

void m68k_core_reset(M68kCore* c) {
  if (!c) return;
  c->sr = 0x2700u;
  c->usp = 0u;
  set_access_context(c, true, true);
  c->ssp = mask24(mem_read32(c, 0x000000u));
  c->a[7] = c->ssp;
  set_access_context(c, true, true);
  c->pc = mask24(mem_read32(c, 0x000004u));
  c->halted = false;
  c->catastrophic_halt = false;
  c->irq_level = 0u;
  c->last_vpa_time = 0u; /* m68000_device::device_reset */
  c->addr_error_pending = 0u;
  c->group0_exception_active = 0u;
  c->access_fc = 0u;
  c->access_is_read = 1u;
  c->access_is_instruction = 1u;
  c->addr_error_ssw = 0u;
  c->addr_error_addr = 0u;
  c->last_opcode_pc = 0u;
  c->last_opcode = 0u;
  c->prev_opcode_pc = 0u;
  c->prev_opcode = 0u;
  c->unknown_count = 0u;
  c->last_unknown_pc = 0u;
  c->last_unknown_opcode = 0u;
  c->first_unknown_valid = 0u;
  c->first_unknown_pc = 0u;
  c->first_unknown_opcode = 0u;
  c->first_unknown_prev_pc = 0u;
  c->first_unknown_prev_opcode = 0u;
  c->first_unknown_sr = 0u;
  c->first_unknown_a7 = 0u;
  c->last_exception_pc = 0u;
  c->last_exception_vector = 0u;
  c->last_exception_opcode = 0u;
  c->first_linef_valid = 0u;
  c->first_linef_pc = 0u;
  c->first_linef_opcode = 0u;
  c->first_linef_prev_pc = 0u;
  c->first_linef_prev_opcode = 0u;
  c->first_linef_sr = 0u;
  c->first_linef_a7 = 0u;
  c->recover_last_pc = 0u;
  c->recover_last_op = 0u;
  c->recover_last_vector = 0u;
  c->recover_repeat_count = 0u;
  c->ea_rw_cache_valid = 0u;
  c->ea_rw_expect_writeback = 0u;
  c->ea_rw_mode = 0u;
  c->ea_rw_reg = 0u;
  c->ea_rw_size = 0u;
  c->ea_rw_addr = 0u;
  c->observable_bus_epoch = 0u;
  c->cycles_total = 0u;
}

void m68k_core_set_irq(M68kCore* c, uint8_t level) {
  if (!c) return;
  c->irq_level = (uint8_t)(level & 7u);
  if (c->irq_level != 0u && !c->catastrophic_halt) c->halted = false;
}

void m68k_core_set_fast_memory(M68kCore* c, const uint8_t* rom,
                               uint32_t rom_limit, uint8_t* ram64k) {
  if (!c) return;
  c->fast_rom = rom;
  c->fast_rom_limit = rom_limit;
  c->fast_ram = ram64k;
}

/* Achado #90: TEMPO DE EXECUÇÃO do 68000 conforme a tabela da Motorola.
 *
 * Este núcleo tinha contagens de ciclo aproximadas, espalhadas pelos atalhos
 * ("used = 8u" pra quase tudo). Medido contra o MAME: na espera da BIOS pela
 * resposta do Z80, a volta
 *     move.b D0,$300001 / move.b $320000,D0 / subq.b #1,D0 / beq / dbra
 * custa 16+16+4+8+10 = 54 ciclos no hardware e contava 42 aqui — o 68000
 * andava ~25% mais rápido que o real em relação ao Z80, aos timers e ao vídeo.
 *
 * A tabela `m68k_cycles_000` (65536 entradas) foi GERADA rodando o próprio
 * `m68kmake` do Musashi e replicando `m68ki_build_opcode_table` — nada
 * redigitado. Por cima dela, os mesmos ajustes dinâmicos que o Musashi faz
 * no 68000 (`m68k_set_cpu_type`, M68K_CPU_TYPE_68000):
 *   CYC_BCC_NOTAKE_B=-2  CYC_BCC_NOTAKE_W=+2  CYC_DBCC_F_NOEXP=-2
 *   CYC_DBCC_F_EXP=+2    CYC_SCC_R_TRUE=+2    CYC_MOVEM_W=2 (<<)
 *   CYC_MOVEM_L=3 (<<)   CYC_SHIFT=1 (<<)
 * e `m68ki_exception_cycle_table[0]` quando a instrução gera exceção. */

/* Achado #101: custo em ciclos da DIVU/DIVS word, transcrito do microcódigo
 * do MAME (`m68000_device::divu_w_ds_dd_if` / `divs_w_ds_dd_if`,
 * m68000-sif.cpp) com as funções de ALU de m68000.h. Só o fluxo de controle
 * e os `m_icount -= n`; os desvios dependem dos resultados intermediários,
 * por isso a aritmética é refeita aqui. Devolve o custo do núcleo da divisão
 * INCLUINDO a pré-busca final (= total do modo Dn,Dn). Divisor zero não passa
 * por aqui (é exceção). */
typedef struct { uint16_t aluo, alue, alub, isr; uint32_t at, au; } DivAlu;
#define DV_Z 0x04u
#define DV_N 0x08u
#define DV_C 0x01u
static void dv_and(DivAlu* a, uint16_t x, uint16_t y) {
  uint16_t r = (uint16_t)(y & x); a->isr = 0u;
  if (!r) a->isr |= DV_Z; if (r & 0x8000u) a->isr |= DV_N; a->aluo = r;
}
static void dv_sub(DivAlu* a, uint16_t x, uint16_t y) {
  uint32_t r = (uint32_t)y - (uint32_t)x; a->isr = 0u;
  if (!(r & 0xFFFFu)) a->isr |= DV_Z; if (r & 0x8000u) a->isr |= DV_N;
  if (r & 0x10000u) a->isr |= DV_C; a->aluo = (uint16_t)r;
}
static void dv_subc(DivAlu* a, uint16_t x, uint16_t y) {
  uint32_t r = (uint32_t)y - (uint32_t)x - ((a->isr & DV_C) ? 1u : 0u); a->isr = 0u;
  if (!(r & 0xFFFFu)) a->isr |= DV_Z; if (r & 0x8000u) a->isr |= DV_N;
  if (r & 0x10000u) a->isr |= DV_C; a->aluo = (uint16_t)r;
}
static void dv_sla(DivAlu* a, uint16_t x, uint32_t bit) {
  uint32_t r = ((uint32_t)x << 17) | ((uint32_t)a->alue << 1) | bit; a->isr = 0u;
  if (!r) a->isr |= DV_Z; if (r & 0x80000000u) a->isr |= DV_N; if (x & 0x8000u) a->isr |= DV_C;
  a->alue = (uint16_t)r; a->aluo = (uint16_t)(r >> 16);
}
#define DV_SET16H(r, v) ((r) = ((r) & 0x0000FFFFu) | ((uint32_t)(uint16_t)(v) << 16))
#define DV_SET16L(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))

static uint32_t m68k_divu_cycles(uint32_t dx, uint16_t src) {
  DivAlu a = {0}; uint32_t n = 0u; int t;
  a.alub = src; a.alue = (uint16_t)dx; DV_SET16H(a.at, src);
  dv_and(&a, src, 0xFFFFu); n += 2u;                                   /* dvur1 */
  t = (a.isr & DV_Z) != 0; dv_sub(&a, a.alub, (uint16_t)(dx >> 16)); n += 2u; /* dvum2 */
  if (t) return n; /* divisor zero: exceção */
  t = (a.isr & DV_C) != 0; DV_SET16L(a.at, dx >> 16); a.au = 0x0Fu + 1u; /* dvum3 */
  dv_and(&a, (uint16_t)(dx >> 16), 0xFFFFu); n += 2u;
  if (!t) return n + 4u;                                               /* dvum4 (estouro) */
dvum5:
  t = (a.isr & DV_N) != 0; a.au--; dv_sla(&a, a.aluo, 0u); n += 2u;
  if (t) goto dvum7; else goto dvum8;
dvum6:
  t = (a.isr & DV_N) != 0; a.au--; dv_sla(&a, a.aluo, 1u); n += 2u;
  if (t) goto dvum7; else goto dvum8;
dvum8:
  t = !(a.au & 0x3Fu); DV_SET16L(a.at, a.aluo); dv_sub(&a, a.alub, a.aluo); n += 2u;
  if (t) goto dvumc;
  t = (a.isr & DV_C) != 0; n += 2u;                                    /* dvumb */
  if (t) { dv_and(&a, (uint16_t)a.at, 0xFFFFu); n += 2u; goto dvum5; } /* dvume */
  goto dvum6;
dvum7:
  t = !(a.au & 0x3Fu); DV_SET16L(a.at, a.aluo); dv_sub(&a, a.alub, a.aluo); n += 2u;
  if (t) { n += 2u; return n + 4u; }                                   /* dvum9, dvum0 */
  goto dvum6;
dvumc:
  n += 2u; return n + 4u;                                              /* dvumc, dvum0 */
}

static uint32_t m68k_divs_cycles(uint32_t dx, uint16_t src) {
  DivAlu a = {0}; uint32_t n = 0u; int t;
  a.alub = src; a.alue = (uint16_t)dx; DV_SET16H(a.at, src);
  dv_and(&a, src, 0xFFFFu); n += 2u;                                   /* dvs02 */
  t = (a.isr & DV_Z) ? 2 : (a.isr & DV_N) ? 1 : 0;
  dv_sub(&a, a.alub, 0u); n += 2u;                                     /* dvs03 */
  if (t == 2) return n; /* divisor zero: exceção */
  if (t == 1) a.alub = a.aluo;                                         /* dvs05 */
  DV_SET16L(a.at, dx >> 16); a.au = 0x0Fu + 1u;                        /* dvs04/05 */
  dv_and(&a, (uint16_t)(dx >> 16), 0xFFFFu); n += 2u;
  t = (a.isr & DV_N) != 0; dv_sub(&a, (uint16_t)dx, 0u); n += 2u;      /* dvs06 */
  if (t) {
    a.alue = a.aluo; dv_subc(&a, (uint16_t)(dx >> 16), 0u); n += 2u;   /* dvs10 */
    DV_SET16L(a.at, a.aluo); dv_sub(&a, a.alub, a.aluo); n += 2u;      /* dvs11 */
  } else {
    dv_sub(&a, a.alub, (uint16_t)a.at); n += 2u;                       /* dvs07 */
  }
  t = (a.isr & DV_C) != 0; dv_and(&a, (uint16_t)a.at, 0xFFFFu); n += 2u; /* dvs08 */
  if (!t) return n + 4u;                                               /* dvumz/dvuma (estouro) */
dvs09:
  a.au--; dv_sla(&a, a.aluo, 0u); n += 2u;
dvs0c:
  t = !(a.au & 0x3Fu); DV_SET16L(a.at, a.aluo); dv_sub(&a, a.alub, a.aluo); n += 2u;
  if (t) goto dvs0e;
  t = (a.isr & DV_C) != 0; n += 2u;                                    /* dvs0d */
  if (t) { dv_and(&a, (uint16_t)a.at, 0xFFFFu); n += 2u; goto dvs09; } /* dvs0f */
  a.au--; dv_sla(&a, a.aluo, 1u); n += 2u; goto dvs0c;                 /* dvs0a */
dvs0e:
  t = (a.isr & DV_C) != 0; n += 2u;
  if (t) { a.au--; dv_sla(&a, a.aluo, 0u); n += 2u; }                  /* dvs12 */
  else   { DV_SET16L(a.at, a.aluo); dv_sla(&a, a.aluo, 1u); n += 2u; } /* dvs13 */
  dv_and(&a, (uint16_t)(a.at >> 16), 0xFFFFu); n += 2u;                /* dvs14 */
  t = (a.isr & DV_N) != 0; a.alub = a.alue;
  dv_and(&a, (uint16_t)(dx >> 16), 0xFFFFu); n += 2u;                  /* dvs15 */
  if (!t) {
    t = (a.isr & DV_N) != 0; DV_SET16H(a.at, a.at); dv_and(&a, a.alub, 0xFFFFu); n += 2u; /* dvs16 */
    if (!t) return n + 4u;                                             /* dvs17 -> leaa2/dvuma */
    dv_sub(&a, a.alub, 0u); n += 2u;                                   /* dvs1a */
    n += 2u; return n + 4u;                                            /* dvs1b -> dvs1c/dvum4 */
  } else {
    t = (a.isr & DV_N) != 0; DV_SET16H(a.at, a.at); dv_and(&a, a.alub, 0xFFFFu); n += 2u; /* dvs1d */
    if (!t) { n += 2u; return n + 4u; }                                /* dvs1f -> dvs20 */
    n += 2u; return n + 4u;                                            /* dvs1e -> dvum4/dvs1c */
  }
}

static uint32_t m68k_cycles_fixup(M68kCore* c, uint16_t op, uint32_t op_pc, uint32_t used_core,
                                  uint32_t exc_seq_before, const uint32_t* d_before) {
  static const uint8_t exc_cycles[32] = {
    40, 4, 50, 50, 34, 38, 40, 34, 34, 34, 34, 34, 4, 4, 4, 44,
    4, 4, 4, 4, 4, 4, 4, 4, 44, 44, 44, 44, 44, 44, 44, 44
  };
  if (c->exception_seq != exc_seq_before) {
    uint8_t v = c->exception_seq_vector;
    if (v < 32u) return exc_cycles[v];
    return 34u; /* TRAP #n */
  }
  uint32_t t = m68k_cycles_000[op];
  if (t == 0u) return used_core; /* opcode sem entrada (não deveria acontecer) */

  switch (op >> 12) {
  case 0x6: { /* Bcc / BRA / BSR */
    uint8_t cc = (uint8_t)((op >> 8) & 0xFu);
    /* Achado #95: decide "tomado" pela CONDIÇÃO (Bcc não mexe nas flags),
     * não pelo PC — `bne` word com deslocamento 2 desvia exatamente pra
     * op_pc+4 e era contado como não tomado. */
    if (cc >= 2u && !cond_true(c, cc)) {
      if ((op & 0xFFu) != 0u) t -= 2u;
      else                    t += 2u;
    }
    break;
  }
  case 0x5:
    if ((op & 0x00C0u) == 0x00C0u) {
      uint8_t mode = (uint8_t)((op >> 3) & 7u);
      uint8_t reg = (uint8_t)(op & 7u);
      if (mode == 1u) { /* DBcc */
        uint8_t cc = (uint8_t)((op >> 8) & 0xFu);
        bool cond = (cc == 0u) ? true : (cc == 1u) ? false : cond_true(c, cc);
        if (!cond) {
          if ((c->d[reg] & 0xFFFFu) != 0xFFFFu) t -= 2u; /* desviou */
          else t += 2u;                                   /* expirou */
        }
      } else if (mode == 0u) { /* Scc em registrador */
        if ((c->d[reg] & 0xFFu) == 0xFFu) t += 2u;
      }
    }
    break;
  case 0xE:
    if (((op >> 6) & 3u) != 3u) { /* deslocamento/rotação em registrador */
      uint32_t count;
      if (op & 0x20u) count = d_before[(op >> 9) & 7u] & 0x3Fu;
      else { count = (op >> 9) & 7u; if (count == 0u) count = 8u; }
      t += count << 1;
    }
    break;
  case 0x4:
    if ((op & 0xFB80u) == 0x4880u && ((op >> 3) & 7u) != 0u) { /* MOVEM (não EXT) */
      uint16_t mask = mem_read16(c, mask24(op_pc + 2u));
      uint32_t n = 0u;
      for (uint16_t m = mask; m; m &= (uint16_t)(m - 1u)) n++;
      t += (op & 0x40u) ? (n << 3) : (n << 2);
    }
    break;
  case 0x8:
    if ((op & 0x01C0u) == 0x00C0u || (op & 0x01C0u) == 0x01C0u) { /* DIVU / DIVS word */
      /* Achado #101: custo do modo EA vem da tabela (diferença pro modo Dn),
       * o do núcleo vem do microcódigo transcrito. */
      uint32_t dividend = d_before[(op >> 9) & 7u];
      uint32_t core = ((op & 0x01C0u) == 0x00C0u) ? m68k_divu_cycles(dividend, c->last_div_src)
                                                  : m68k_divs_cycles(dividend, c->last_div_src);
      t = t - m68k_cycles_000[op & 0xFFC0u] + core;
    }
    break;
  case 0xC:
    if ((op & 0x01C0u) == 0x00C0u || (op & 0x01C0u) == 0x01C0u) { /* MULU / MULS word */
      uint8_t dst = (uint8_t)((op >> 9) & 7u);
      uint8_t mode = (uint8_t)((op >> 3) & 7u), reg = (uint8_t)(op & 7u);
      bool is_signed = (op & 0x0100u) != 0u;
      bool have = true;
      uint16_t src = 0u;
      if (mode == 0u) src = (uint16_t)d_before[reg];
      else if (mode == 7u && reg == 4u) src = mem_read16(c, mask24(op_pc + 2u));
      else if ((uint16_t)d_before[dst] != 0u) {
        if (is_signed) src = (uint16_t)((int32_t)c->d[dst] / (int32_t)(int16_t)(uint16_t)d_before[dst]);
        else           src = (uint16_t)(c->d[dst] / (uint16_t)d_before[dst]);
      } else have = false;
      if (have) {
        uint32_t extra = 0u;
        if (is_signed) { /* Musashi 010_LESS: 2 por transição de bit */
          uint32_t x = (uint32_t)((int32_t)(int16_t)src) & 0x1FFFFu, f = 0u;
          for (uint32_t y = x; y; y >>= 1) { if ((y & 1u) != f) { extra += 2u; f = 1u - f; } }
        } else {        /* 2 por bit ligado */
          for (uint16_t y = src; y; y &= (uint16_t)(y - 1u)) extra += 2u;
        }
        t += extra;
      }
    }
    break;
  default:
    break;
  }
  return t;
}

M68K_FORCE_INLINE uint32_t m68k_core_step_internal(M68kCore* c) {
  uint16_t op;
  uint32_t op_pc;
  uint32_t used = 0u;
  bool handled = false;

  if (!c) {
    return 0u;
  }

  if (c->catastrophic_halt) {
    return 0u;
  }

  if (service_interrupt(c)) {
    used = 44u;
    if (c->vpa_sync) {
      /* Achado #101: no microcódigo do MAME (`state_interrupt_if`) o IACK
       * começa 10 ciclos depois do início da exceção (itlx1..itlx4) e o
       * mapa de autovetores faz `before_time(vpa_sync)` + `after_delay(1)`:
       * o acesso espera a próxima borda do clock E (período 10), pulando
       * uma se faltar menos de 3 ciclos. */
      uint64_t t = c->host_time + 10u;
      uint64_t sync;
      if (c->last_vpa_time >= t) {
        sync = c->last_vpa_time;
      } else {
        uint64_t mod = t % 10u;
        sync = (mod < 7u) ? (t - mod + 10u) : (t - mod + 20u);
        c->last_vpa_time = sync;
      }
      used += (uint32_t)(sync - t) + 1u;
    }
    c->cycles_total += used;
    return used;
  }

  if (c->halted) {
    return 0u;
  }

  c->addr_error_pending = 0u;

  if ((c->pc & 1u) != 0u) {
    uint32_t fault_pc = c->pc;
    c->addr_error_pending = 1u;
    c->addr_error_addr = mask24(fault_pc);
    c->addr_error_ssw = (uint16_t)((1u << 4) | m68k_function_code(c, true));
    /* Prefer real 68000 address-error handling when vector 3 exists.
       Fallback to alignment only when vector is uninitialized/deadloop. */
    if (vector_is_recoverable(c, 3u, 0xFFFFu, fault_pc, &used)) {
    } else {
      take_address_error(c, fault_pc, 0xFFFFu, &used);
    }
    c->cycles_total += used;
    return used;
  }

  op_pc = c->pc;
  op = fetch16(c);
#if defined(M68K_OPCODE_PROFILE)
  g_m68k_opcode_profile[op]++;
  if (op_pc < 0x00080000u) {
    g_m68k_rom_pc_profile[op_pc >> 1]++;
  }
#endif
  c->prev_opcode_pc = c->last_opcode_pc;
  c->prev_opcode = c->last_opcode;
  c->last_opcode_pc = op_pc;
  c->last_opcode = op;

  /* Achado #90: foto do estado que os ajustes de tempo precisam. */
  uint32_t cyc_exc_seq = c->exception_seq;
  uint32_t cyc_d_before[8];
  memcpy(cyc_d_before, c->d, sizeof(cyc_d_before));

  /* Cortex-M fast paths for the most common 68000 forms.  They keep the same
     architectural operations as the generic decoder, but avoid constructing
     temporary effective-address objects for simple register/indirect modes. */
  if ((op & 0xF100u) == 0x7000u) {
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    c->d[dn] = (uint32_t)(int32_t)(int8_t)(op & 0xFFu);
    set_logic_flags(c, c->d[dn], 4u);
    handled = true;
    used = 4u;
  } else if ((op & 0xF000u) == 0x6000u &&
             (op & 0x00FFu) != 0u &&
             (op & 0x0F00u) != 0x0100u) {
    uint8_t cc = (uint8_t)((op >> 8) & 0x0Fu);
    bool take = cc == 0u ||
                (cc == 6u ? (c->sr & SR_Z) == 0u :
                 cc == 7u ? (c->sr & SR_Z) != 0u : cond_true(c, cc));
    if (take) {
      c->pc = mask24((uint32_t)((int32_t)c->pc + sx8((uint8_t)op)));
      used = 10u;
    } else {
      used = 8u;
    }
    handled = true;
  } else if ((op & 0xF0F8u) == 0x50C8u) {
    uint8_t cc = (uint8_t)((op >> 8) & 0x0Fu);
    uint8_t dn = (uint8_t)(op & 7u);
    uint32_t base = c->pc;
    int16_t disp = (int16_t)fetch16(c);
    bool condition_met = cc == 1u ? false : cond_true(c, cc);
    if (!condition_met) {
      uint16_t w = (uint16_t)(c->d[dn] - 1u);
      c->d[dn] = (c->d[dn] & 0xFFFF0000u) | w;
      if (w != 0xFFFFu) {
        c->pc = mask24((uint32_t)((int32_t)base + disp));
        used = 10u;
      } else {
        used = 12u;
      }
    } else {
      used = 12u;
    }
    handled = true;
  } else if ((op & 0xF1FFu) == 0x1038u) {
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    uint32_t addr = mask24((uint32_t)sx16(fetch16(c)));
    uint8_t value = mem_read8(c, addr);
    c->d[dn] = (c->d[dn] & 0xFFFFFF00u) | value;
    set_logic_flags(c, value, 1u);
    handled = true;
    used = 8u;
  } else if ((op & 0xF1F8u) == 0x1010u ||
             (op & 0xF1F8u) == 0x1018u) {
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    uint8_t an = (uint8_t)(op & 7u);
    uint8_t value = mem_read8(c, c->a[an]);
    if ((op & 0x0008u) != 0u) {
      c->a[an] = (c->a[an] + ea_step(an, 1u));
    }
    c->d[dn] = (c->d[dn] & 0xFFFFFF00u) | value;
    set_logic_flags(c, value, 1u);
    handled = true;
    used = 8u;
  } else if ((op & 0xF1F8u) == 0x2018u) {
    uint8_t dn = (uint8_t)((op >> 9) & 7u);
    uint8_t an = (uint8_t)(op & 7u);
    uint32_t value = data_read32(c, c->a[an]);
    c->a[an] = (c->a[an] + 4u);
    c->d[dn] = value;
    set_logic_flags(c, value, 4u);
    handled = true;
    used = 8u;
  } else if ((op & 0xF1F8u) == 0x2100u) {
    uint8_t an = (uint8_t)((op >> 9) & 7u);
    uint8_t dn = (uint8_t)(op & 7u);
    c->a[an] = (c->a[an] - 4u);
    data_write32(c, c->a[an], c->d[dn]);
    set_logic_flags(c, c->d[dn], 4u);
    handled = true;
    used = 8u;
  } else if ((op & 0xF1F8u) == 0x3080u ||
             (op & 0xF1F8u) == 0x30C0u) {
    uint8_t an = (uint8_t)((op >> 9) & 7u);
    uint8_t dn = (uint8_t)(op & 7u);
    uint16_t value = (uint16_t)c->d[dn];
    data_write16(c, c->a[an], value);
    if ((op & 0x0040u) != 0u) {
      c->a[an] = (c->a[an] + 2u);
    }
    set_logic_flags(c, value, 2u);
    handled = true;
    used = 8u;
  } else if (op == 0x48E7u) {
    uint16_t list = fetch16(c);
    uint32_t addr = c->a[7];
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < 16u; i++) {
      if ((list & (uint16_t)(1u << i)) != 0u) {
        uint8_t ridx = (uint8_t)(15u - i);
        addr = mask24(addr - 4u);
        data_write32(c, addr, reg_index_read(c, ridx, 4u));
        count++;
      }
    }
    c->a[7] = addr;
    handled = true;
    used = 8u + (uint32_t)count * 4u;
  } else if (op == 0x4CDFu) {
    uint16_t list = fetch16(c);
    uint32_t addr = c->a[7];
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < 16u; i++) {
      if ((list & (uint16_t)(1u << i)) != 0u) {
        reg_index_write(c, i, 4u, data_read32(c, addr));
        addr = mask24(addr + 4u);
        count++;
      }
    }
    c->a[7] = addr;
    handled = true;
    used = 8u + (uint32_t)count * 4u;
  } else if ((op & 0xF1F8u) == 0x41E8u) {
    uint8_t dst = (uint8_t)((op >> 9) & 7u);
    uint8_t src = (uint8_t)(op & 7u);
    c->a[dst] = ((uint32_t)((int32_t)c->a[src] + sx16(fetch16(c))));
    handled = true;
    used = 8u;
  } else if ((op & 0xFFF8u) == 0x4A28u ||
             (op & 0xFFF8u) == 0x4A68u) {
    uint8_t an = (uint8_t)(op & 7u);
    uint32_t addr = mask24((uint32_t)((int32_t)c->a[an] + sx16(fetch16(c))));
    if ((op & 0x0040u) != 0u) {
      set_logic_flags(c, data_read16(c, addr), 2u);
    } else {
      set_logic_flags(c, mem_read8(c, addr), 1u);
    }
    handled = true;
    used = 4u;
  } else if ((op & 0xFFF8u) == 0x4880u) {
    uint8_t dn = (uint8_t)(op & 7u);
    int16_t value = (int16_t)(int8_t)c->d[dn];
    c->d[dn] = (c->d[dn] & 0xFFFF0000u) | (uint16_t)value;
    set_logic_flags(c, c->d[dn], 2u);
    handled = true;
    used = 4u;
  } else if ((op & 0xFFF8u) == 0x0C40u) {
    uint8_t dn = (uint8_t)(op & 7u);
    uint32_t src = fetch16(c);
    uint32_t dst = c->d[dn] & 0xFFFFu;
    uint32_t result = (dst - src) & 0xFFFFu;
    set_sub_flags(c, dst, src, result, 2u);
    handled = true;
    used = 8u;
  } else if (op == 0x4EBAu) {
    uint32_t base = c->pc;
    int16_t disp = (int16_t)fetch16(c);
    uint32_t return_pc = c->pc;
    push32(c, return_pc);
    c->pc = mask24((uint32_t)((int32_t)base + disp));
    handled = true;
    used = 18u;
  } else if (op == 0x4E75u) {
    c->pc = mask24(pop32(c));
    handled = true;
    used = 16u;
  } else {
    c->ea_rw_cache_valid = 0u;
    c->ea_rw_expect_writeback = 0u;
    switch ((op >> 12) & 0x0Fu) {
    case 0x0:
      handled = op_immediate(c, op, &used) || op_bitops(c, op, &used);
      break;
    case 0x1:
    case 0x2:
    case 0x3:
      handled = op_move(c, op, &used);
      break;
    case 0x4:
      handled = op_misc(c, op, &used);
      break;
    case 0x5:
      handled = op_quick_dbcc(c, op, &used);
      break;
    case 0x6:
      handled = op_branch(c, op, &used);
      break;
    case 0x7:
      handled = op_moveq(c, op, &used);
      break;
    case 0x8:
    case 0x9:
    case 0xB:
    case 0xC:
    case 0xD:
      handled = op_logic_arith(c, op, &used);
      break;
    case 0xA:
      if (vector_is_recoverable(c, 10u, op, op_pc, &used)) {
        handled = true;
      } else {
        take_exception(c, 10u);
        c->last_exception_pc = op_pc;
        c->last_exception_vector = 10u;
        c->last_exception_opcode = op;
        handled = true;
        used = 34u;
      }
      break;
    case 0xE:
      handled = op_shift(c, op, &used);
      break;
    case 0xF:
      if (vector_is_recoverable(c, 11u, op, op_pc, &used)) {
        handled = true;
      } else {
        if (!c->first_linef_valid) {
          c->first_linef_valid = 1u;
          c->first_linef_pc = op_pc;
          c->first_linef_opcode = op;
          c->first_linef_prev_pc = c->prev_opcode_pc;
          c->first_linef_prev_opcode = c->prev_opcode;
          c->first_linef_sr = c->sr;
          c->first_linef_a7 = c->a[7];
        }
        take_exception(c, 11u);
        c->last_exception_pc = op_pc;
        c->last_exception_vector = 11u;
        c->last_exception_opcode = op;
        handled = true;
        used = 34u;
      }
      break;
    default:
      break;
    }
  }

  if (c->addr_error_pending) {
    take_address_error(c, op_pc, op, &used);
    handled = true;
  }

  if (!handled) {
    bool repeat_unknown = (c->last_unknown_pc == op_pc && c->last_unknown_opcode == op);
    if (!c->first_unknown_valid) {
      c->first_unknown_valid = 1u;
      c->first_unknown_pc = op_pc;
      c->first_unknown_opcode = op;
      c->first_unknown_prev_pc = c->prev_opcode_pc;
      c->first_unknown_prev_opcode = c->prev_opcode;
      c->first_unknown_sr = c->sr;
      c->first_unknown_a7 = c->a[7];
    }
    c->last_unknown_pc = op_pc;
    c->last_unknown_opcode = op;
    c->unknown_count++;

    (void)repeat_unknown;
    take_exception(c, 4u);
    c->last_exception_pc = op_pc;
    c->last_exception_vector = 4u;
    c->last_exception_opcode = op;
    used = 34u;
  }

  if (used == 0u) {
    used = 4u;
  }

  used = m68k_cycles_fixup(c, op, op_pc, used, cyc_exc_seq, cyc_d_before);

  c->cycles_total += used;
  return used;
}

M68K_RAMFUNC uint32_t m68k_core_step(M68kCore* c) {
  return m68k_core_step_internal(c);
}

uint32_t m68k_core_run_cycles(M68kCore* c, uint32_t cycles_budget) {
  uint32_t cycles = 0u;
  uint32_t anchor_pc;
  uint32_t anchor_epoch;
  uint32_t anchor_d[8];
  uint32_t anchor_a[8];
  uint16_t anchor_sr;
  uint8_t stable_loops = 0u;
  if (!c) return 0u;

  anchor_pc = c->pc;
  anchor_epoch = c->observable_bus_epoch;
  anchor_sr = c->sr;
  memcpy(anchor_d, c->d, sizeof(anchor_d));
  memcpy(anchor_a, c->a, sizeof(anchor_a));

  while (cycles < cycles_budget && !c->external_bus_halt) {
    uint32_t used = m68k_core_step_internal(c);
    if (used == 0u) {
      break;
    }
    cycles += used;

    if (c->pc == anchor_pc) {
      bool stable = c->observable_bus_epoch == anchor_epoch &&
                    c->sr == anchor_sr &&
                    memcmp(anchor_d, c->d, sizeof(anchor_d)) == 0 &&
                    memcmp(anchor_a, c->a, sizeof(anchor_a)) == 0;
      if (stable) {
        stable_loops++;
        if (stable_loops >= 2u) {
          /* Direct ROM/RAM polling cannot observe an external change until
             the scheduler advances the other chips at the slice boundary. */
          if (cycles < cycles_budget) {
            c->cycles_total += cycles_budget - cycles;
          }
          return cycles_budget;
        }
      } else {
        anchor_epoch = c->observable_bus_epoch;
        anchor_sr = c->sr;
        memcpy(anchor_d, c->d, sizeof(anchor_d));
        memcpy(anchor_a, c->a, sizeof(anchor_a));
        stable_loops = 0u;
      }
    }
  }
  return (cycles > cycles_budget) ? cycles_budget : cycles;
}
