/**
 * @file z80.c
 * @brief Zilog Z80 CPU emulator – full instruction set
 *
 * Switch-based dispatch with pre-computed flag tables.
 * All base + CB/DD/ED/FD/DDCB/FDCB prefixed instructions implemented.
 */

#include "z80.h"
#include <string.h>
#include <stdio.h>

/* Depuração: se não-NULL, grava o PC de cada instrução (hex) — pra comparar
 * com `trace arq,audiocpu,noloop` do MAME. */
FILE *z80_trace_file = NULL;

#if !defined(HOST_BUILD)
#pragma GCC optimize ("O3")
#endif

/* =========================================================================
 * Register access macros
 * ========================================================================= */

#define A  ((uint8_t)(z->af >> 8))
#define F  ((uint8_t)(z->af & 0xFF))
#define B  ((uint8_t)(z->bc >> 8))
#define C  ((uint8_t)(z->bc & 0xFF))
#define D  ((uint8_t)(z->de >> 8))
#define E  ((uint8_t)(z->de & 0xFF))
#define H  ((uint8_t)(z->hl >> 8))
#define L  ((uint8_t)(z->hl & 0xFF))

#define SET_A(v)  z->af = ((uint16_t)(v) << 8) | (z->af & 0x00FF)
#define SET_F(v)  z->af = (z->af & 0xFF00) | ((v) & 0xFF)
#define SET_B(v)  z->bc = ((uint16_t)(v) << 8) | (z->bc & 0x00FF)
#define SET_C(v)  z->bc = (z->bc & 0xFF00) | ((v) & 0xFF)
#define SET_D(v)  z->de = ((uint16_t)(v) << 8) | (z->de & 0x00FF)
#define SET_E(v)  z->de = (z->de & 0xFF00) | ((v) & 0xFF)
#define SET_H(v)  z->hl = ((uint16_t)(v) << 8) | (z->hl & 0x00FF)
#define SET_L(v)  z->hl = (z->hl & 0xFF00) | ((v) & 0xFF)

#define IXH ((uint8_t)(z->ix >> 8))
#define IXL ((uint8_t)(z->ix & 0xFF))
#define IYH ((uint8_t)(z->iy >> 8))
#define IYL ((uint8_t)(z->iy & 0xFF))

#define SET_IXH(v)  z->ix = ((uint16_t)(v) << 8) | (z->ix & 0x00FF)
#define SET_IXL(v)  z->ix = (z->ix & 0xFF00) | ((v) & 0xFF)
#define SET_IYH(v)  z->iy = ((uint16_t)(v) << 8) | (z->iy & 0x00FF)
#define SET_IYL(v)  z->iy = (z->iy & 0xFF00) | ((v) & 0xFF)

/* =========================================================================
 * Flag bits
 * ========================================================================= */

#define ZF_C   0x01   /* Carry */
#define ZF_N   0x02   /* Subtract */
#define ZF_PV  0x04   /* Parity/Overflow */
#define ZF_3   0x08   /* Undocumented bit 3 */
#define ZF_H   0x10   /* Half-carry */
#define ZF_5   0x20   /* Undocumented bit 5 */
#define ZF_Z   0x40   /* Zero */
#define ZF_S   0x80   /* Sign */

/* =========================================================================
 * Pre-computed flag tables
 * ========================================================================= */

/* Sign, Zero, bits 5+3 */
static uint8_t sz53_table[256];

/* Sign, Zero, bits 5+3, Parity */
static uint8_t sz53p_table[256];

/* Parity flag only */
static uint8_t parity_table[256];

static bool tables_inited = false;

static void init_tables(void)
{
    if (tables_inited) return;
    tables_inited = true;

    for (int i = 0; i < 256; i++) {
        uint8_t f = (uint8_t)(i & (ZF_S | ZF_3 | ZF_5));
        if (i == 0) f |= ZF_Z;

        /* Parity: even number of set bits → PV set */
        int bits = 0;
        for (int b = 0; b < 8; b++)
            if (i & (1 << b)) bits++;
        if ((bits & 1) == 0) {
            parity_table[i] = ZF_PV;
            f |= ZF_PV;
        } else {
            parity_table[i] = 0;
        }

        sz53p_table[i] = f;
        sz53_table[i] = f & ~ZF_PV;
    }
}

/* =========================================================================
 * Memory / IO helpers (inline for speed)
 * ========================================================================= */

static inline uint8_t rd(Z80* z, uint16_t addr)
{
    if (z->fast_ram && addr < z->fast_ram_limit)
        return z->fast_ram[addr & z->fast_ram_mask];
    return z->read(z->ctx, addr);
}

static inline void wr(Z80* z, uint16_t addr, uint8_t val)
{
    if (z->fast_ram && addr < z->fast_ram_limit) {
        z->fast_ram[addr & z->fast_ram_mask] = val;
        return;
    }
    z->write(z->ctx, addr, val);
}

static inline uint8_t io_rd(Z80* z, uint8_t port)
{
    return z->io_read(z->ctx, port);
}

static inline void io_wr(Z80* z, uint8_t port, uint8_t val)
{
    z->io_write(z->ctx, port, val);
}

static inline uint8_t fetch8(Z80* z)
{
    uint8_t v = rd(z, z->pc);
    z->pc++;
    return v;
}

static inline uint16_t fetch16(Z80* z)
{
    uint8_t lo = fetch8(z);
    uint8_t hi = fetch8(z);
    return (uint16_t)((hi << 8) | lo);
}

static inline uint16_t rd16(Z80* z, uint16_t addr)
{
    uint8_t lo = rd(z, addr);
    uint8_t hi = rd(z, (uint16_t)(addr + 1));
    return (uint16_t)((hi << 8) | lo);
}

static inline void wr16(Z80* z, uint16_t addr, uint16_t val)
{
    wr(z, addr, (uint8_t)(val & 0xFF));
    wr(z, (uint16_t)(addr + 1), (uint8_t)(val >> 8));
}

static inline void push16(Z80* z, uint16_t val)
{
    z->sp -= 2;
    wr16(z, z->sp, val);
}

static inline uint16_t pop16(Z80* z)
{
    uint16_t val = rd16(z, z->sp);
    z->sp += 2;
    return val;
}

/* =========================================================================
 * Increment R register (7-bit counter, bit 7 preserved)
 * ========================================================================= */

static inline void inc_r(Z80* z)
{
    z->r = (z->r & 0x80) | ((z->r + 1) & 0x7F);
}

/* =========================================================================
 * ALU helpers
 * ========================================================================= */

static inline void alu_add8(Z80* z, uint8_t val)
{
    uint8_t a = A;
    uint16_t r16 = (uint16_t)a + val;
    uint8_t result = (uint8_t)r16;
    uint8_t f = sz53_table[result];
    f |= (r16 & 0x100) ? ZF_C : 0;
    f |= ((a ^ val ^ result) & 0x10) ? ZF_H : 0;
    f |= (((a ^ ~val) & (a ^ result)) & 0x80) ? ZF_PV : 0;
    SET_A(result);
    SET_F(f);
}

static inline void alu_adc8(Z80* z, uint8_t val)
{
    uint8_t a = A;
    uint8_t cy = F & ZF_C;
    uint16_t r16 = (uint16_t)a + val + cy;
    uint8_t result = (uint8_t)r16;
    uint8_t f = sz53_table[result];
    f |= (r16 & 0x100) ? ZF_C : 0;
    f |= ((a ^ val ^ result) & 0x10) ? ZF_H : 0;
    f |= (((a ^ ~val) & (a ^ result)) & 0x80) ? ZF_PV : 0;
    SET_A(result);
    SET_F(f);
}

static inline void alu_sub8(Z80* z, uint8_t val)
{
    uint8_t a = A;
    uint16_t r16 = (uint16_t)a - val;
    uint8_t result = (uint8_t)r16;
    uint8_t f = sz53_table[result] | ZF_N;
    f |= (r16 & 0x100) ? ZF_C : 0;
    f |= ((a ^ val ^ result) & 0x10) ? ZF_H : 0;
    f |= (((a ^ val) & (a ^ result)) & 0x80) ? ZF_PV : 0;
    SET_A(result);
    SET_F(f);
}

static inline void alu_sbc8(Z80* z, uint8_t val)
{
    uint8_t a = A;
    uint8_t cy = F & ZF_C;
    uint16_t r16 = (uint16_t)a - val - cy;
    uint8_t result = (uint8_t)r16;
    uint8_t f = sz53_table[result] | ZF_N;
    f |= (r16 & 0x100) ? ZF_C : 0;
    f |= ((a ^ val ^ result) & 0x10) ? ZF_H : 0;
    f |= (((a ^ val) & (a ^ result)) & 0x80) ? ZF_PV : 0;
    SET_A(result);
    SET_F(f);
}

static inline void alu_and(Z80* z, uint8_t val)
{
    uint8_t result = A & val;
    SET_A(result);
    SET_F(sz53p_table[result] | ZF_H);
}

static inline void alu_xor(Z80* z, uint8_t val)
{
    uint8_t result = A ^ val;
    SET_A(result);
    SET_F(sz53p_table[result]);
}

static inline void alu_or(Z80* z, uint8_t val)
{
    uint8_t result = A | val;
    SET_A(result);
    SET_F(sz53p_table[result]);
}

static inline void alu_cp(Z80* z, uint8_t val)
{
    uint8_t a = A;
    uint16_t r16 = (uint16_t)a - val;
    uint8_t result = (uint8_t)r16;
    /* CP sets bits 5,3 from the operand, not from the result */
    uint8_t f = (sz53_table[result] & ~(ZF_3 | ZF_5)) | (val & (ZF_3 | ZF_5)) | ZF_N;
    f |= (r16 & 0x100) ? ZF_C : 0;
    f |= ((a ^ val ^ result) & 0x10) ? ZF_H : 0;
    f |= (((a ^ val) & (a ^ result)) & 0x80) ? ZF_PV : 0;
    SET_F(f);
}

static inline uint8_t alu_inc8(Z80* z, uint8_t val)
{
    uint8_t result = val + 1;
    uint8_t f = (F & ZF_C) | sz53_table[result];
    f |= (val == 0x7F) ? ZF_PV : 0;
    f |= ((val ^ result) & 0x10) ? ZF_H : 0;
    SET_F(f);
    return result;
}

static inline uint8_t alu_dec8(Z80* z, uint8_t val)
{
    uint8_t result = val - 1;
    uint8_t f = (F & ZF_C) | sz53_table[result] | ZF_N;
    f |= (val == 0x80) ? ZF_PV : 0;
    f |= ((val ^ result) & 0x10) ? ZF_H : 0;
    SET_F(f);
    return result;
}

static inline void alu_add16(Z80* z, uint16_t* dst, uint16_t val)
{
    uint32_t r32 = (uint32_t)*dst + val;
    uint8_t f = (F & (ZF_S | ZF_Z | ZF_PV));
    f |= (r32 & 0x10000) ? ZF_C : 0;
    f |= ((*dst ^ val ^ (uint16_t)r32) & 0x1000) ? ZF_H : 0;
    f |= (uint8_t)((r32 >> 8) & (ZF_3 | ZF_5));
    SET_F(f);
    *dst = (uint16_t)r32;
}

static inline void alu_adc16(Z80* z, uint16_t val)
{
    uint16_t hl = z->hl;
    uint8_t cy = F & ZF_C;
    uint32_t r32 = (uint32_t)hl + val + cy;
    uint16_t result = (uint16_t)r32;
    uint8_t f = (uint8_t)((result >> 8) & (ZF_S | ZF_3 | ZF_5));
    f |= (result == 0) ? ZF_Z : 0;
    f |= (r32 & 0x10000) ? ZF_C : 0;
    f |= ((hl ^ val ^ result) & 0x1000) ? ZF_H : 0;
    f |= (((hl ^ ~val) & (hl ^ result)) & 0x8000) ? ZF_PV : 0;
    SET_F(f);
    z->hl = result;
}

static inline void alu_sbc16(Z80* z, uint16_t val)
{
    uint16_t hl = z->hl;
    uint8_t cy = F & ZF_C;
    uint32_t r32 = (uint32_t)hl - val - cy;
    uint16_t result = (uint16_t)r32;
    uint8_t f = (uint8_t)((result >> 8) & (ZF_S | ZF_3 | ZF_5)) | ZF_N;
    f |= (result == 0) ? ZF_Z : 0;
    f |= (r32 & 0x10000) ? ZF_C : 0;
    f |= ((hl ^ val ^ result) & 0x1000) ? ZF_H : 0;
    f |= (((hl ^ val) & (hl ^ result)) & 0x8000) ? ZF_PV : 0;
    SET_F(f);
    z->hl = result;
}

/* =========================================================================
 * Get/Set register by 3-bit index (used in LD r,r' and ALU ops)
 *   0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A
 * ========================================================================= */

static inline uint8_t get_reg8(Z80* z, uint8_t idx)
{
    switch (idx) {
        case 0: return B;
        case 1: return C;
        case 2: return D;
        case 3: return E;
        case 4: return H;
        case 5: return L;
        case 6: return rd(z, z->hl);
        case 7: return A;
    }
    return 0;
}

static inline void set_reg8(Z80* z, uint8_t idx, uint8_t val)
{
    switch (idx) {
        case 0: SET_B(val); break;
        case 1: SET_C(val); break;
        case 2: SET_D(val); break;
        case 3: SET_E(val); break;
        case 4: SET_H(val); break;
        case 5: SET_L(val); break;
        case 6: wr(z, z->hl, val); break;
        case 7: SET_A(val); break;
    }
}

static inline uint16_t* get_rp(Z80* z, uint8_t idx)
{
    switch (idx & 3) {
        case 0: return &z->bc;
        case 1: return &z->de;
        case 2: return &z->hl;
        case 3: return &z->sp;
    }
    return &z->hl;
}

static inline uint16_t* get_rp2(Z80* z, uint8_t idx)
{
    switch (idx & 3) {
        case 0: return &z->bc;
        case 1: return &z->de;
        case 2: return &z->hl;
        case 3: return &z->af;
    }
    return &z->hl;
}

/* =========================================================================
 * Condition codes: NZ, Z, NC, C, PO, PE, P, M
 * ========================================================================= */

static inline bool check_cc(Z80* z, uint8_t cc)
{
    switch (cc) {
        case 0: return !(F & ZF_Z);   /* NZ */
        case 1: return  (F & ZF_Z);   /* Z */
        case 2: return !(F & ZF_C);   /* NC */
        case 3: return  (F & ZF_C);   /* C */
        case 4: return !(F & ZF_PV);  /* PO */
        case 5: return  (F & ZF_PV);  /* PE */
        case 6: return !(F & ZF_S);   /* P (positive) */
        case 7: return  (F & ZF_S);   /* M (minus) */
    }
    return false;
}

/* =========================================================================
 * CB prefix: rotates, shifts, bit, res, set
 * ========================================================================= */

static inline uint8_t rlc(Z80* z, uint8_t val)
{
    uint8_t c = (val >> 7) & 1;
    val = (val << 1) | c;
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t rrc(Z80* z, uint8_t val)
{
    uint8_t c = val & 1;
    val = (val >> 1) | (c << 7);
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t rl(Z80* z, uint8_t val)
{
    uint8_t c = (val >> 7) & 1;
    val = (val << 1) | (F & ZF_C);
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t rr(Z80* z, uint8_t val)
{
    uint8_t c = val & 1;
    val = (val >> 1) | ((F & ZF_C) << 7);
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t sla(Z80* z, uint8_t val)
{
    uint8_t c = (val >> 7) & 1;
    val <<= 1;
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t sra(Z80* z, uint8_t val)
{
    uint8_t c = val & 1;
    val = (val >> 1) | (val & 0x80);
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t sll(Z80* z, uint8_t val)
{
    /* Undocumented: SLL shifts left and sets bit 0 */
    uint8_t c = (val >> 7) & 1;
    val = (val << 1) | 1;
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline uint8_t srl(Z80* z, uint8_t val)
{
    uint8_t c = val & 1;
    val >>= 1;
    SET_F(sz53p_table[val] | c);
    return val;
}

static inline void bit_op(Z80* z, uint8_t bit, uint8_t val)
{
    uint8_t result = val & (1 << bit);
    uint8_t f = (F & ZF_C) | ZF_H;
    f |= (result) ? (result & (ZF_S | ZF_3 | ZF_5)) : (ZF_Z | ZF_PV);
    SET_F(f);
}

static void exec_cb(Z80* z)
{
    uint8_t op = fetch8(z);
    inc_r(z);

    uint8_t y = (op >> 3) & 7;
    uint8_t reg = op & 7;
    uint8_t val;

    /* Fetch operand */
    if (reg == 6) {
        val = rd(z, z->hl);
        z->cycles += 4; /* extra for (HL) */
    } else {
        val = get_reg8(z, reg);
    }

    /* Execute */
    switch (op >> 6) {
    case 0: /* Rotates/shifts */
        switch (y) {
            case 0: val = rlc(z, val); break;
            case 1: val = rrc(z, val); break;
            case 2: val = rl(z, val); break;
            case 3: val = rr(z, val); break;
            case 4: val = sla(z, val); break;
            case 5: val = sra(z, val); break;
            case 6: val = sll(z, val); break;
            case 7: val = srl(z, val); break;
        }
        if (reg == 6) {
            wr(z, z->hl, val);
            z->cycles += 3;
        } else {
            set_reg8(z, reg, val);
        }
        z->cycles += 8;
        break;

    case 1: /* BIT y, r */
        bit_op(z, y, val);
        /* BIT with (HL) has different undoc flags */
        if (reg == 6) {
            /* Bits 5,3 come from high byte of address */
            uint8_t f2 = F & ~(ZF_3 | ZF_5);
            f2 |= (uint8_t)((z->hl >> 8) & (ZF_3 | ZF_5));
            SET_F(f2);
        }
        z->cycles += 8;
        break;

    case 2: /* RES y, r */
        val &= ~(1 << y);
        if (reg == 6) {
            wr(z, z->hl, val);
            z->cycles += 3;
        } else {
            set_reg8(z, reg, val);
        }
        z->cycles += 8;
        break;

    case 3: /* SET y, r */
        val |= (1 << y);
        if (reg == 6) {
            wr(z, z->hl, val);
            z->cycles += 3;
        } else {
            set_reg8(z, reg, val);
        }
        z->cycles += 8;
        break;
    }
}

/* =========================================================================
 * DDCB / FDCB prefix: indexed bit ops (IX+d / IY+d)
 * Displacement is fetched BEFORE the opcode byte.
 * ========================================================================= */

static void exec_xxcb(Z80* z, uint16_t base)
{
    int8_t d = (int8_t)fetch8(z);
    uint8_t op = fetch8(z);
    uint16_t addr = (uint16_t)(base + d);
    uint8_t val = rd(z, addr);

    uint8_t y = (op >> 3) & 7;
    uint8_t reg = op & 7;

    switch (op >> 6) {
    case 0: /* Rotate/shift with index */
        switch (y) {
            case 0: val = rlc(z, val); break;
            case 1: val = rrc(z, val); break;
            case 2: val = rl(z, val); break;
            case 3: val = rr(z, val); break;
            case 4: val = sla(z, val); break;
            case 5: val = sra(z, val); break;
            case 6: val = sll(z, val); break;
            case 7: val = srl(z, val); break;
        }
        wr(z, addr, val);
        /* Also store in register (undocumented) */
        if (reg != 6) set_reg8(z, reg, val);
        z->cycles += 23;
        break;

    case 1: /* BIT y, (IX/IY+d) */
        bit_op(z, y, val);
        {
            uint8_t f2 = F & ~(ZF_3 | ZF_5);
            f2 |= (uint8_t)((addr >> 8) & (ZF_3 | ZF_5));
            SET_F(f2);
        }
        z->cycles += 20;
        break;

    case 2: /* RES y, (IX/IY+d) */
        val &= ~(1 << y);
        wr(z, addr, val);
        if (reg != 6) set_reg8(z, reg, val);
        z->cycles += 23;
        break;

    case 3: /* SET y, (IX/IY+d) */
        val |= (1 << y);
        wr(z, addr, val);
        if (reg != 6) set_reg8(z, reg, val);
        z->cycles += 23;
        break;
    }
}

/* =========================================================================
 * ED prefix instructions
 * ========================================================================= */

static void exec_ed(Z80* z)
{
    uint8_t op = fetch8(z);
    inc_r(z);

    switch (op) {
    /* --- IN r,(C) --- */
    case 0x40: { uint8_t v = io_rd(z, C); SET_B(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x48: { uint8_t v = io_rd(z, C); SET_C(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x50: { uint8_t v = io_rd(z, C); SET_D(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x58: { uint8_t v = io_rd(z, C); SET_E(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x60: { uint8_t v = io_rd(z, C); SET_H(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x68: { uint8_t v = io_rd(z, C); SET_L(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }
    case 0x70: { uint8_t v = io_rd(z, C); (void)v; SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; } /* IN (C) - undoc */
    case 0x78: { uint8_t v = io_rd(z, C); SET_A(v); SET_F((F & ZF_C) | sz53p_table[v]); z->cycles += 12; break; }

    /* --- OUT (C),r --- */
    case 0x41: io_wr(z, C, B); z->cycles += 12; break;
    case 0x49: io_wr(z, C, C); z->cycles += 12; break;
    case 0x51: io_wr(z, C, D); z->cycles += 12; break;
    case 0x59: io_wr(z, C, E); z->cycles += 12; break;
    case 0x61: io_wr(z, C, H); z->cycles += 12; break;
    case 0x69: io_wr(z, C, L); z->cycles += 12; break;
    case 0x71: io_wr(z, C, 0); z->cycles += 12; break; /* OUT (C),0 - undoc */
    case 0x79: io_wr(z, C, A); z->cycles += 12; break;

    /* --- SBC HL,rr --- */
    case 0x42: alu_sbc16(z, z->bc); z->cycles += 15; break;
    case 0x52: alu_sbc16(z, z->de); z->cycles += 15; break;
    case 0x62: alu_sbc16(z, z->hl); z->cycles += 15; break;
    case 0x72: alu_sbc16(z, z->sp); z->cycles += 15; break;

    /* --- ADC HL,rr --- */
    case 0x4A: alu_adc16(z, z->bc); z->cycles += 15; break;
    case 0x5A: alu_adc16(z, z->de); z->cycles += 15; break;
    case 0x6A: alu_adc16(z, z->hl); z->cycles += 15; break;
    case 0x7A: alu_adc16(z, z->sp); z->cycles += 15; break;

    /* --- LD (nn),rr --- */
    case 0x43: { uint16_t addr = fetch16(z); wr16(z, addr, z->bc); z->cycles += 20; break; }
    case 0x53: { uint16_t addr = fetch16(z); wr16(z, addr, z->de); z->cycles += 20; break; }
    case 0x63: { uint16_t addr = fetch16(z); wr16(z, addr, z->hl); z->cycles += 20; break; }
    case 0x73: { uint16_t addr = fetch16(z); wr16(z, addr, z->sp); z->cycles += 20; break; }

    /* --- LD rr,(nn) --- */
    case 0x4B: { uint16_t addr = fetch16(z); z->bc = rd16(z, addr); z->cycles += 20; break; }
    case 0x5B: { uint16_t addr = fetch16(z); z->de = rd16(z, addr); z->cycles += 20; break; }
    case 0x6B: { uint16_t addr = fetch16(z); z->hl = rd16(z, addr); z->cycles += 20; break; }
    case 0x7B: { uint16_t addr = fetch16(z); z->sp = rd16(z, addr); z->cycles += 20; break; }

    /* --- NEG --- */
    case 0x44: case 0x4C: case 0x54: case 0x5C:
    case 0x64: case 0x6C: case 0x74: case 0x7C: {
        uint8_t a = A;
        SET_A(0);
        alu_sub8(z, a);
        z->cycles += 8;
        break;
    }

    /* --- RETN --- */
    case 0x45: case 0x55: case 0x65: case 0x75: {
        z->iff1 = z->iff2;
        z->pc = pop16(z);
        z->cycles += 14;
        break;
    }

    /* --- RETI --- */
    case 0x4D: case 0x5D: case 0x6D: case 0x7D: {
        z->iff1 = z->iff2;
        z->pc = pop16(z);
        z->cycles += 14;
        break;
    }

    /* --- IM 0/1/2 --- */
    case 0x46: case 0x66: z->im = 0; z->cycles += 8; break;
    case 0x56: case 0x76: z->im = 1; z->cycles += 8; break;
    case 0x5E: case 0x7E: z->im = 2; z->cycles += 8; break;
    case 0x4E: case 0x6E: z->im = 0; z->cycles += 8; break; /* undocumented IM 0 */

    /* --- LD I,A / LD A,I --- */
    case 0x47: z->i = A; z->cycles += 9; break;
    case 0x57: {
        SET_A(z->i);
        uint8_t f = (F & ZF_C) | sz53_table[z->i];
        f |= z->iff2 ? ZF_PV : 0;
        SET_F(f);
        z->cycles += 9;
        break;
    }

    /* --- LD R,A / LD A,R --- */
    case 0x4F: z->r = A; z->cycles += 9; break;
    case 0x5F: {
        SET_A(z->r);
        uint8_t f = (F & ZF_C) | sz53_table[z->r];
        f |= z->iff2 ? ZF_PV : 0;
        SET_F(f);
        z->cycles += 9;
        break;
    }

    /* --- RRD --- */
    case 0x67: {
        uint8_t val = rd(z, z->hl);
        uint8_t a = A;
        SET_A((a & 0xF0) | (val & 0x0F));
        val = ((a & 0x0F) << 4) | (val >> 4);
        wr(z, z->hl, val);
        SET_F((F & ZF_C) | sz53p_table[A]);
        z->cycles += 18;
        break;
    }

    /* --- RLD --- */
    case 0x6F: {
        uint8_t val = rd(z, z->hl);
        uint8_t a = A;
        SET_A((a & 0xF0) | (val >> 4));
        val = ((val & 0x0F) << 4) | (a & 0x0F);
        wr(z, z->hl, val);
        SET_F((F & ZF_C) | sz53p_table[A]);
        z->cycles += 18;
        break;
    }

    /* --- LDI --- */
    case 0xA0: {
        uint8_t val = rd(z, z->hl);
        wr(z, z->de, val);
        z->hl++; z->de++; z->bc--;
        uint8_t n = val + A;
        uint8_t f = F & (ZF_S | ZF_Z | ZF_C);
        f |= (z->bc != 0) ? ZF_PV : 0;
        f |= (n & ZF_3);
        f |= (n & 0x02) ? ZF_5 : 0;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- LDIR --- */
    case 0xB0: {
        uint8_t val = rd(z, z->hl);
        wr(z, z->de, val);
        z->hl++; z->de++; z->bc--;
        if (z->bc != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            uint8_t n = val + A;
            uint8_t f = F & (ZF_S | ZF_Z | ZF_C);
            f |= (n & ZF_3);
            f |= (n & 0x02) ? ZF_5 : 0;
            SET_F(f);
            z->cycles += 16;
        }
        break;
    }

    /* --- LDD --- */
    case 0xA8: {
        uint8_t val = rd(z, z->hl);
        wr(z, z->de, val);
        z->hl--; z->de--; z->bc--;
        uint8_t n = val + A;
        uint8_t f = F & (ZF_S | ZF_Z | ZF_C);
        f |= (z->bc != 0) ? ZF_PV : 0;
        f |= (n & ZF_3);
        f |= (n & 0x02) ? ZF_5 : 0;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- LDDR --- */
    case 0xB8: {
        uint8_t val = rd(z, z->hl);
        wr(z, z->de, val);
        z->hl--; z->de--; z->bc--;
        if (z->bc != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            uint8_t n = val + A;
            uint8_t f = F & (ZF_S | ZF_Z | ZF_C);
            f |= (n & ZF_3);
            f |= (n & 0x02) ? ZF_5 : 0;
            SET_F(f);
            z->cycles += 16;
        }
        break;
    }

    /* --- CPI --- */
    case 0xA1: {
        uint8_t val = rd(z, z->hl);
        uint8_t result = A - val;
        z->hl++; z->bc--;
        uint8_t f = (F & ZF_C) | ZF_N;
        f |= sz53_table[result] & (ZF_S | ZF_Z);
        f |= ((A ^ val ^ result) & 0x10) ? ZF_H : 0;
        f |= (z->bc != 0) ? ZF_PV : 0;
        uint8_t n = result - ((f & ZF_H) ? 1 : 0);
        f |= (n & ZF_3);
        f |= (n & 0x02) ? ZF_5 : 0;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- CPIR --- */
    case 0xB1: {
        uint8_t val = rd(z, z->hl);
        uint8_t result = A - val;
        z->hl++; z->bc--;
        uint8_t f = (F & ZF_C) | ZF_N;
        f |= sz53_table[result] & (ZF_S | ZF_Z);
        f |= ((A ^ val ^ result) & 0x10) ? ZF_H : 0;
        f |= (z->bc != 0) ? ZF_PV : 0;
        if (z->bc != 0 && result != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            uint8_t n = result - ((f & ZF_H) ? 1 : 0);
            f |= (n & ZF_3);
            f |= (n & 0x02) ? ZF_5 : 0;
            SET_F(f);
            z->cycles += 16;
        }
        break;
    }

    /* --- CPD --- */
    case 0xA9: {
        uint8_t val = rd(z, z->hl);
        uint8_t result = A - val;
        z->hl--; z->bc--;
        uint8_t f = (F & ZF_C) | ZF_N;
        f |= sz53_table[result] & (ZF_S | ZF_Z);
        f |= ((A ^ val ^ result) & 0x10) ? ZF_H : 0;
        f |= (z->bc != 0) ? ZF_PV : 0;
        uint8_t n = result - ((f & ZF_H) ? 1 : 0);
        f |= (n & ZF_3);
        f |= (n & 0x02) ? ZF_5 : 0;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- CPDR --- */
    case 0xB9: {
        uint8_t val = rd(z, z->hl);
        uint8_t result = A - val;
        z->hl--; z->bc--;
        uint8_t f = (F & ZF_C) | ZF_N;
        f |= sz53_table[result] & (ZF_S | ZF_Z);
        f |= ((A ^ val ^ result) & 0x10) ? ZF_H : 0;
        f |= (z->bc != 0) ? ZF_PV : 0;
        if (z->bc != 0 && result != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            uint8_t n = result - ((f & ZF_H) ? 1 : 0);
            f |= (n & ZF_3);
            f |= (n & 0x02) ? ZF_5 : 0;
            SET_F(f);
            z->cycles += 16;
        }
        break;
    }

    /* --- INI --- */
    case 0xA2: {
        uint8_t val = io_rd(z, C);
        wr(z, z->hl, val);
        z->hl++;
        uint8_t b = B - 1;
        SET_B(b);
        uint8_t f = sz53_table[b] | ZF_N;
        f |= (F & ZF_C);
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- INIR --- */
    case 0xB2: {
        uint8_t val = io_rd(z, C);
        wr(z, z->hl, val);
        z->hl++;
        uint8_t b = B - 1;
        SET_B(b);
        if (b != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            SET_F(ZF_Z | ZF_N);
            z->cycles += 16;
        }
        break;
    }

    /* --- IND --- */
    case 0xAA: {
        uint8_t val = io_rd(z, C);
        wr(z, z->hl, val);
        z->hl--;
        uint8_t b = B - 1;
        SET_B(b);
        uint8_t f = sz53_table[b] | ZF_N;
        f |= (F & ZF_C);
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- INDR --- */
    case 0xBA: {
        uint8_t val = io_rd(z, C);
        wr(z, z->hl, val);
        z->hl--;
        uint8_t b = B - 1;
        SET_B(b);
        if (b != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            SET_F(ZF_Z | ZF_N);
            z->cycles += 16;
        }
        break;
    }

    /* --- OUTI --- */
    case 0xA3: {
        uint8_t val = rd(z, z->hl);
        uint8_t b = B - 1;
        SET_B(b);
        io_wr(z, C, val);
        z->hl++;
        uint8_t f = sz53_table[b] | ZF_N;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- OTIR --- */
    case 0xB3: {
        uint8_t val = rd(z, z->hl);
        uint8_t b = B - 1;
        SET_B(b);
        io_wr(z, C, val);
        z->hl++;
        if (b != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            SET_F(ZF_Z | ZF_N);
            z->cycles += 16;
        }
        break;
    }

    /* --- OUTD --- */
    case 0xAB: {
        uint8_t val = rd(z, z->hl);
        uint8_t b = B - 1;
        SET_B(b);
        io_wr(z, C, val);
        z->hl--;
        uint8_t f = sz53_table[b] | ZF_N;
        SET_F(f);
        z->cycles += 16;
        break;
    }

    /* --- OTDR --- */
    case 0xBB: {
        uint8_t val = rd(z, z->hl);
        uint8_t b = B - 1;
        SET_B(b);
        io_wr(z, C, val);
        z->hl--;
        if (b != 0) {
            z->pc -= 2;
            z->cycles += 21;
        } else {
            SET_F(ZF_Z | ZF_N);
            z->cycles += 16;
        }
        break;
    }

    default:
        /* Undocumented ED NOP: 2 bytes consumed, 8 T-states */
        z->cycles += 8;
        break;
    }
}

/* =========================================================================
 * DD/FD prefix: IX/IY instructions
 *
 * Most HL-referencing instructions use IX/IY instead,
 * and (HL) becomes (IX/IY + d) with a displacement byte.
 * H/L references in register-to-register ops become IXH/IXL/IYH/IYL
 * (undocumented).
 * ========================================================================= */

static void exec_dd_fd(Z80* z, uint16_t* ireg)
{
    uint8_t op = fetch8(z);
    inc_r(z);

    /* Helper: read a register, replacing H/L with IrH/IrL and (HL) with (Ir+d) */
    /* For (Ir+d) we need the displacement which comes right after the opcode */

    switch (op) {

    /* --- CB prefix with index → DDCB/FDCB --- */
    case 0xCB:
        exec_xxcb(z, *ireg);
        break;

    /* --- DD/FD prefix eaten (another DD/FD or ED follows) --- */
    case 0xDD: case 0xFD: case 0xED:
        /* Re-execute: the current prefix is wasted */
        z->pc--;
        z->cycles += 4;
        break;

    /* --- LD Ir,nn --- */
    case 0x21: *ireg = fetch16(z); z->cycles += 14; break;

    /* --- LD (nn),Ir --- */
    case 0x22: { uint16_t addr = fetch16(z); wr16(z, addr, *ireg); z->cycles += 20; break; }

    /* --- INC Ir --- */
    case 0x23: (*ireg)++; z->cycles += 10; break;

    /* --- LD Ir,(nn) --- */
    case 0x2A: { uint16_t addr = fetch16(z); *ireg = rd16(z, addr); z->cycles += 20; break; }

    /* --- DEC Ir --- */
    case 0x2B: (*ireg)--; z->cycles += 10; break;

    /* --- ADD Ir,rr --- */
    case 0x09: alu_add16(z, ireg, z->bc); z->cycles += 15; break;
    case 0x19: alu_add16(z, ireg, z->de); z->cycles += 15; break;
    case 0x29: alu_add16(z, ireg, *ireg); z->cycles += 15; break;
    case 0x39: alu_add16(z, ireg, z->sp); z->cycles += 15; break;

    /* --- INC/DEC IrH (undocumented) --- */
    case 0x24: { uint8_t h = (uint8_t)(*ireg >> 8); h = alu_inc8(z, h); *ireg = ((uint16_t)h << 8) | (*ireg & 0xFF); z->cycles += 8; break; }
    case 0x25: { uint8_t h = (uint8_t)(*ireg >> 8); h = alu_dec8(z, h); *ireg = ((uint16_t)h << 8) | (*ireg & 0xFF); z->cycles += 8; break; }

    /* --- LD IrH,n (undocumented) --- */
    case 0x26: { uint8_t n = fetch8(z); *ireg = ((uint16_t)n << 8) | (*ireg & 0xFF); z->cycles += 11; break; }

    /* --- INC/DEC IrL (undocumented) --- */
    case 0x2C: { uint8_t lo = (uint8_t)(*ireg & 0xFF); lo = alu_inc8(z, lo); *ireg = (*ireg & 0xFF00) | lo; z->cycles += 8; break; }
    case 0x2D: { uint8_t lo = (uint8_t)(*ireg & 0xFF); lo = alu_dec8(z, lo); *ireg = (*ireg & 0xFF00) | lo; z->cycles += 8; break; }

    /* --- LD IrL,n (undocumented) --- */
    case 0x2E: { uint8_t n = fetch8(z); *ireg = (*ireg & 0xFF00) | n; z->cycles += 11; break; }

    /* --- INC (Ir+d) --- */
    case 0x34: { int8_t d = (int8_t)fetch8(z); uint16_t addr = (uint16_t)(*ireg + d); uint8_t v = rd(z, addr); v = alu_inc8(z, v); wr(z, addr, v); z->cycles += 23; break; }

    /* --- DEC (Ir+d) --- */
    case 0x35: { int8_t d = (int8_t)fetch8(z); uint16_t addr = (uint16_t)(*ireg + d); uint8_t v = rd(z, addr); v = alu_dec8(z, v); wr(z, addr, v); z->cycles += 23; break; }

    /* --- LD (Ir+d),n --- */
    case 0x36: { int8_t d = (int8_t)fetch8(z); uint8_t n = fetch8(z); wr(z, (uint16_t)(*ireg + d), n); z->cycles += 19; break; }

    /* --- LD r,(Ir+d) --- */
    case 0x46: { int8_t d = (int8_t)fetch8(z); SET_B(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x4E: { int8_t d = (int8_t)fetch8(z); SET_C(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x56: { int8_t d = (int8_t)fetch8(z); SET_D(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x5E: { int8_t d = (int8_t)fetch8(z); SET_E(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x66: { int8_t d = (int8_t)fetch8(z); SET_H(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x6E: { int8_t d = (int8_t)fetch8(z); SET_L(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x7E: { int8_t d = (int8_t)fetch8(z); SET_A(rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }

    /* --- LD (Ir+d),r --- */
    case 0x70: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), B); z->cycles += 19; break; }
    case 0x71: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), C); z->cycles += 19; break; }
    case 0x72: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), D); z->cycles += 19; break; }
    case 0x73: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), E); z->cycles += 19; break; }
    case 0x74: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), H); z->cycles += 19; break; }
    case 0x75: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), L); z->cycles += 19; break; }
    case 0x77: { int8_t d = (int8_t)fetch8(z); wr(z, (uint16_t)(*ireg + d), A); z->cycles += 19; break; }

    /* --- ALU A,(Ir+d) --- */
    case 0x86: { int8_t d = (int8_t)fetch8(z); alu_add8(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x8E: { int8_t d = (int8_t)fetch8(z); alu_adc8(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x96: { int8_t d = (int8_t)fetch8(z); alu_sub8(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0x9E: { int8_t d = (int8_t)fetch8(z); alu_sbc8(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0xA6: { int8_t d = (int8_t)fetch8(z); alu_and(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0xAE: { int8_t d = (int8_t)fetch8(z); alu_xor(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0xB6: { int8_t d = (int8_t)fetch8(z); alu_or(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }
    case 0xBE: { int8_t d = (int8_t)fetch8(z); alu_cp(z, rd(z, (uint16_t)(*ireg + d))); z->cycles += 19; break; }

    /* --- PUSH/POP Ir --- */
    case 0xE1: *ireg = pop16(z); z->cycles += 14; break;
    case 0xE5: push16(z, *ireg); z->cycles += 15; break;

    /* --- JP (Ir) --- */
    case 0xE9: z->pc = *ireg; z->cycles += 8; break;

    /* --- EX (SP),Ir --- */
    case 0xE3: {
        uint16_t val = rd16(z, z->sp);
        wr16(z, z->sp, *ireg);
        *ireg = val;
        z->cycles += 23;
        break;
    }

    /* --- LD SP,Ir --- */
    case 0xF9: z->sp = *ireg; z->cycles += 10; break;

    /* --- Undocumented: LD between IrH/IrL and registers --- */
    /* LD IrH,B..A / LD IrL,B..A / LD B..A,IrH / LD B..A,IrL */
    case 0x44: SET_B((uint8_t)(*ireg >> 8)); z->cycles += 8; break;   /* LD B,IrH */
    case 0x45: SET_B((uint8_t)(*ireg & 0xFF)); z->cycles += 8; break; /* LD B,IrL */
    case 0x4C: SET_C((uint8_t)(*ireg >> 8)); z->cycles += 8; break;   /* LD C,IrH */
    case 0x4D: SET_C((uint8_t)(*ireg & 0xFF)); z->cycles += 8; break; /* LD C,IrL */
    case 0x54: SET_D((uint8_t)(*ireg >> 8)); z->cycles += 8; break;   /* LD D,IrH */
    case 0x55: SET_D((uint8_t)(*ireg & 0xFF)); z->cycles += 8; break; /* LD D,IrL */
    case 0x5C: SET_E((uint8_t)(*ireg >> 8)); z->cycles += 8; break;   /* LD E,IrH */
    case 0x5D: SET_E((uint8_t)(*ireg & 0xFF)); z->cycles += 8; break; /* LD E,IrL */

    case 0x60: *ireg = ((uint16_t)B << 8) | (*ireg & 0xFF); z->cycles += 8; break;  /* LD IrH,B */
    case 0x61: *ireg = ((uint16_t)C << 8) | (*ireg & 0xFF); z->cycles += 8; break;  /* LD IrH,C */
    case 0x62: *ireg = ((uint16_t)D << 8) | (*ireg & 0xFF); z->cycles += 8; break;  /* LD IrH,D */
    case 0x63: *ireg = ((uint16_t)E << 8) | (*ireg & 0xFF); z->cycles += 8; break;  /* LD IrH,E */
    case 0x64: z->cycles += 8; break; /* LD IrH,IrH (NOP) */
    case 0x65: *ireg = ((uint16_t)(*ireg & 0xFF) << 8) | (*ireg & 0xFF); z->cycles += 8; break; /* LD IrH,IrL */
    case 0x67: *ireg = ((uint16_t)A << 8) | (*ireg & 0xFF); z->cycles += 8; break;  /* LD IrH,A */

    case 0x68: *ireg = (*ireg & 0xFF00) | B; z->cycles += 8; break;  /* LD IrL,B */
    case 0x69: *ireg = (*ireg & 0xFF00) | C; z->cycles += 8; break;  /* LD IrL,C */
    case 0x6A: *ireg = (*ireg & 0xFF00) | D; z->cycles += 8; break;  /* LD IrL,D */
    case 0x6B: *ireg = (*ireg & 0xFF00) | E; z->cycles += 8; break;  /* LD IrL,E */
    case 0x6C: *ireg = (*ireg & 0xFF00) | (uint8_t)(*ireg >> 8); z->cycles += 8; break; /* LD IrL,IrH */
    case 0x6D: z->cycles += 8; break; /* LD IrL,IrL (NOP) */
    case 0x6F: *ireg = (*ireg & 0xFF00) | A; z->cycles += 8; break;  /* LD IrL,A */

    case 0x7C: SET_A((uint8_t)(*ireg >> 8)); z->cycles += 8; break;   /* LD A,IrH */
    case 0x7D: SET_A((uint8_t)(*ireg & 0xFF)); z->cycles += 8; break; /* LD A,IrL */

    /* --- Undocumented ALU with IrH/IrL --- */
    case 0x84: alu_add8(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0x85: alu_add8(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0x8C: alu_adc8(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0x8D: alu_adc8(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0x94: alu_sub8(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0x95: alu_sub8(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0x9C: alu_sbc8(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0x9D: alu_sbc8(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0xA4: alu_and(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0xA5: alu_and(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0xAC: alu_xor(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0xAD: alu_xor(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0xB4: alu_or(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0xB5: alu_or(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;
    case 0xBC: alu_cp(z, (uint8_t)(*ireg >> 8)); z->cycles += 8; break;
    case 0xBD: alu_cp(z, (uint8_t)(*ireg & 0xFF)); z->cycles += 8; break;

    /* --- INC/DEC IrH/IrL (already handled above) --- */

    default:
        /* Non-prefixed opcode: treat as NOP (4 T-states wasted on prefix) */
        z->pc--;
        z->cycles += 4;
        break;
    }
}

/* =========================================================================
 * Main instruction dispatch (base opcodes 0x00-0xFF)
 * ========================================================================= */

static void exec_one(Z80* z)
{
    if (z80_trace_file) fprintf(z80_trace_file, "%04X\n", z->pc);
    uint8_t op = fetch8(z);
    inc_r(z);

    switch (op) {

    /* ========================= 0x00 - 0x0F ========================= */
    case 0x00: /* NOP */          z->cycles += 4; break;
    case 0x01: /* LD BC,nn */     z->bc = fetch16(z); z->cycles += 10; break;
    case 0x02: /* LD (BC),A */    wr(z, z->bc, A); z->cycles += 7; break;
    case 0x03: /* INC BC */       z->bc++; z->cycles += 6; break;
    case 0x04: /* INC B */        SET_B(alu_inc8(z, B)); z->cycles += 4; break;
    case 0x05: /* DEC B */        SET_B(alu_dec8(z, B)); z->cycles += 4; break;
    case 0x06: /* LD B,n */       SET_B(fetch8(z)); z->cycles += 7; break;
    case 0x07: /* RLCA */ {
        uint8_t a = A;
        uint8_t c = (a >> 7) & 1;
        a = (a << 1) | c;
        SET_A(a);
        SET_F((F & (ZF_S | ZF_Z | ZF_PV)) | c | (a & (ZF_3 | ZF_5)));
        z->cycles += 4;
        break;
    }
    case 0x08: /* EX AF,AF' */ {
        uint16_t tmp = z->af; z->af = z->af2; z->af2 = tmp;
        z->cycles += 4;
        break;
    }
    case 0x09: /* ADD HL,BC */    alu_add16(z, &z->hl, z->bc); z->cycles += 11; break;
    case 0x0A: /* LD A,(BC) */    SET_A(rd(z, z->bc)); z->cycles += 7; break;
    case 0x0B: /* DEC BC */       z->bc--; z->cycles += 6; break;
    case 0x0C: /* INC C */        SET_C(alu_inc8(z, C)); z->cycles += 4; break;
    case 0x0D: /* DEC C */        SET_C(alu_dec8(z, C)); z->cycles += 4; break;
    case 0x0E: /* LD C,n */       SET_C(fetch8(z)); z->cycles += 7; break;
    case 0x0F: /* RRCA */ {
        uint8_t a = A;
        uint8_t c = a & 1;
        a = (a >> 1) | (c << 7);
        SET_A(a);
        SET_F((F & (ZF_S | ZF_Z | ZF_PV)) | c | (a & (ZF_3 | ZF_5)));
        z->cycles += 4;
        break;
    }

    /* ========================= 0x10 - 0x1F ========================= */
    case 0x10: /* DJNZ e */ {
        int8_t d = (int8_t)fetch8(z);
        uint8_t b = B - 1;
        SET_B(b);
        if (b != 0) {
            z->pc = (uint16_t)(z->pc + d);
            z->cycles += 13;
        } else {
            z->cycles += 8;
        }
        break;
    }
    case 0x11: /* LD DE,nn */     z->de = fetch16(z); z->cycles += 10; break;
    case 0x12: /* LD (DE),A */    wr(z, z->de, A); z->cycles += 7; break;
    case 0x13: /* INC DE */       z->de++; z->cycles += 6; break;
    case 0x14: /* INC D */        SET_D(alu_inc8(z, D)); z->cycles += 4; break;
    case 0x15: /* DEC D */        SET_D(alu_dec8(z, D)); z->cycles += 4; break;
    case 0x16: /* LD D,n */       SET_D(fetch8(z)); z->cycles += 7; break;
    case 0x17: /* RLA */ {
        uint8_t a = A;
        uint8_t c = (a >> 7) & 1;
        a = (a << 1) | (F & ZF_C);
        SET_A(a);
        SET_F((F & (ZF_S | ZF_Z | ZF_PV)) | c | (a & (ZF_3 | ZF_5)));
        z->cycles += 4;
        break;
    }
    case 0x18: /* JR e */ {
        int8_t d = (int8_t)fetch8(z);
        z->pc = (uint16_t)(z->pc + d);
        z->cycles += 12;
        break;
    }
    case 0x19: /* ADD HL,DE */    alu_add16(z, &z->hl, z->de); z->cycles += 11; break;
    case 0x1A: /* LD A,(DE) */    SET_A(rd(z, z->de)); z->cycles += 7; break;
    case 0x1B: /* DEC DE */       z->de--; z->cycles += 6; break;
    case 0x1C: /* INC E */        SET_E(alu_inc8(z, E)); z->cycles += 4; break;
    case 0x1D: /* DEC E */        SET_E(alu_dec8(z, E)); z->cycles += 4; break;
    case 0x1E: /* LD E,n */       SET_E(fetch8(z)); z->cycles += 7; break;
    case 0x1F: /* RRA */ {
        uint8_t a = A;
        uint8_t c = a & 1;
        a = (a >> 1) | ((F & ZF_C) << 7);
        SET_A(a);
        SET_F((F & (ZF_S | ZF_Z | ZF_PV)) | c | (a & (ZF_3 | ZF_5)));
        z->cycles += 4;
        break;
    }

    /* ========================= 0x20 - 0x2F ========================= */
    case 0x20: /* JR NZ,e */ {
        int8_t d = (int8_t)fetch8(z);
        if (!(F & ZF_Z)) { z->pc = (uint16_t)(z->pc + d); z->cycles += 12; }
        else { z->cycles += 7; }
        break;
    }
    case 0x21: /* LD HL,nn */     z->hl = fetch16(z); z->cycles += 10; break;
    case 0x22: /* LD (nn),HL */ { uint16_t addr = fetch16(z); wr16(z, addr, z->hl); z->cycles += 16; break; }
    case 0x23: /* INC HL */       z->hl++; z->cycles += 6; break;
    case 0x24: /* INC H */        SET_H(alu_inc8(z, H)); z->cycles += 4; break;
    case 0x25: /* DEC H */        SET_H(alu_dec8(z, H)); z->cycles += 4; break;
    case 0x26: /* LD H,n */       SET_H(fetch8(z)); z->cycles += 7; break;
    case 0x27: /* DAA */ {
        uint8_t a = A;
        uint8_t f = F;
        uint8_t correction = 0;
        uint8_t carry = 0;

        if ((f & ZF_H) || ((a & 0x0F) > 9)) {
            correction |= 0x06;
        }
        if ((f & ZF_C) || (a > 0x99)) {
            correction |= 0x60;
            carry = ZF_C;
        }

        if (f & ZF_N) {
            a -= correction;
            f = (f & ZF_N) | carry | sz53p_table[a];
            f |= ((A ^ a) & ZF_H);
        } else {
            a += correction;
            f = carry | sz53p_table[a];
            f |= ((A ^ a) & ZF_H);
        }
        SET_A(a);
        SET_F(f);
        z->cycles += 4;
        break;
    }
    case 0x28: /* JR Z,e */ {
        int8_t d = (int8_t)fetch8(z);
        if (F & ZF_Z) { z->pc = (uint16_t)(z->pc + d); z->cycles += 12; }
        else { z->cycles += 7; }
        break;
    }
    case 0x29: /* ADD HL,HL */    alu_add16(z, &z->hl, z->hl); z->cycles += 11; break;
    case 0x2A: /* LD HL,(nn) */ { uint16_t addr = fetch16(z); z->hl = rd16(z, addr); z->cycles += 16; break; }
    case 0x2B: /* DEC HL */       z->hl--; z->cycles += 6; break;
    case 0x2C: /* INC L */        SET_L(alu_inc8(z, L)); z->cycles += 4; break;
    case 0x2D: /* DEC L */        SET_L(alu_dec8(z, L)); z->cycles += 4; break;
    case 0x2E: /* LD L,n */       SET_L(fetch8(z)); z->cycles += 7; break;
    case 0x2F: /* CPL */          SET_A(~A); SET_F((F & (ZF_S | ZF_Z | ZF_PV | ZF_C)) | ZF_H | ZF_N | (A & (ZF_3 | ZF_5))); z->cycles += 4; break;

    /* ========================= 0x30 - 0x3F ========================= */
    case 0x30: /* JR NC,e */ {
        int8_t d = (int8_t)fetch8(z);
        if (!(F & ZF_C)) { z->pc = (uint16_t)(z->pc + d); z->cycles += 12; }
        else { z->cycles += 7; }
        break;
    }
    case 0x31: /* LD SP,nn */     z->sp = fetch16(z); z->cycles += 10; break;
    case 0x32: /* LD (nn),A */  { uint16_t addr = fetch16(z); wr(z, addr, A); z->cycles += 13; break; }
    case 0x33: /* INC SP */       z->sp++; z->cycles += 6; break;
    case 0x34: /* INC (HL) */   { uint8_t v = rd(z, z->hl); v = alu_inc8(z, v); wr(z, z->hl, v); z->cycles += 11; break; }
    case 0x35: /* DEC (HL) */   { uint8_t v = rd(z, z->hl); v = alu_dec8(z, v); wr(z, z->hl, v); z->cycles += 11; break; }
    case 0x36: /* LD (HL),n */  { uint8_t n = fetch8(z); wr(z, z->hl, n); z->cycles += 10; break; }
    case 0x37: /* SCF */          SET_F((F & (ZF_S | ZF_Z | ZF_PV)) | ZF_C | (A & (ZF_3 | ZF_5))); z->cycles += 4; break;
    case 0x38: /* JR C,e */ {
        int8_t d = (int8_t)fetch8(z);
        if (F & ZF_C) { z->pc = (uint16_t)(z->pc + d); z->cycles += 12; }
        else { z->cycles += 7; }
        break;
    }
    case 0x39: /* ADD HL,SP */    alu_add16(z, &z->hl, z->sp); z->cycles += 11; break;
    case 0x3A: /* LD A,(nn) */ {  uint16_t addr = fetch16(z); SET_A(rd(z, addr)); z->cycles += 13; break; }
    case 0x3B: /* DEC SP */       z->sp--; z->cycles += 6; break;
    case 0x3C: /* INC A */        SET_A(alu_inc8(z, A)); z->cycles += 4; break;
    case 0x3D: /* DEC A */        SET_A(alu_dec8(z, A)); z->cycles += 4; break;
    case 0x3E: /* LD A,n */       SET_A(fetch8(z)); z->cycles += 7; break;
    case 0x3F: /* CCF */ {
        uint8_t f = (F & (ZF_S | ZF_Z | ZF_PV));
        f |= (F & ZF_C) ? ZF_H : 0;
        f |= (F & ZF_C) ? 0 : ZF_C;
        f |= (A & (ZF_3 | ZF_5));
        SET_F(f);
        z->cycles += 4;
        break;
    }

    /* ========================= 0x40 - 0x7F: LD r,r' ========================= */
    case 0x40: /* LD B,B */  z->cycles += 4; break;
    case 0x41: /* LD B,C */  SET_B(C); z->cycles += 4; break;
    case 0x42: /* LD B,D */  SET_B(D); z->cycles += 4; break;
    case 0x43: /* LD B,E */  SET_B(E); z->cycles += 4; break;
    case 0x44: /* LD B,H */  SET_B(H); z->cycles += 4; break;
    case 0x45: /* LD B,L */  SET_B(L); z->cycles += 4; break;
    case 0x46: /* LD B,(HL)*/ SET_B(rd(z, z->hl)); z->cycles += 7; break;
    case 0x47: /* LD B,A */  SET_B(A); z->cycles += 4; break;
    case 0x48: /* LD C,B */  SET_C(B); z->cycles += 4; break;
    case 0x49: /* LD C,C */  z->cycles += 4; break;
    case 0x4A: /* LD C,D */  SET_C(D); z->cycles += 4; break;
    case 0x4B: /* LD C,E */  SET_C(E); z->cycles += 4; break;
    case 0x4C: /* LD C,H */  SET_C(H); z->cycles += 4; break;
    case 0x4D: /* LD C,L */  SET_C(L); z->cycles += 4; break;
    case 0x4E: /* LD C,(HL)*/ SET_C(rd(z, z->hl)); z->cycles += 7; break;
    case 0x4F: /* LD C,A */  SET_C(A); z->cycles += 4; break;

    case 0x50: /* LD D,B */  SET_D(B); z->cycles += 4; break;
    case 0x51: /* LD D,C */  SET_D(C); z->cycles += 4; break;
    case 0x52: /* LD D,D */  z->cycles += 4; break;
    case 0x53: /* LD D,E */  SET_D(E); z->cycles += 4; break;
    case 0x54: /* LD D,H */  SET_D(H); z->cycles += 4; break;
    case 0x55: /* LD D,L */  SET_D(L); z->cycles += 4; break;
    case 0x56: /* LD D,(HL)*/ SET_D(rd(z, z->hl)); z->cycles += 7; break;
    case 0x57: /* LD D,A */  SET_D(A); z->cycles += 4; break;
    case 0x58: /* LD E,B */  SET_E(B); z->cycles += 4; break;
    case 0x59: /* LD E,C */  SET_E(C); z->cycles += 4; break;
    case 0x5A: /* LD E,D */  SET_E(D); z->cycles += 4; break;
    case 0x5B: /* LD E,E */  z->cycles += 4; break;
    case 0x5C: /* LD E,H */  SET_E(H); z->cycles += 4; break;
    case 0x5D: /* LD E,L */  SET_E(L); z->cycles += 4; break;
    case 0x5E: /* LD E,(HL)*/ SET_E(rd(z, z->hl)); z->cycles += 7; break;
    case 0x5F: /* LD E,A */  SET_E(A); z->cycles += 4; break;

    case 0x60: /* LD H,B */  SET_H(B); z->cycles += 4; break;
    case 0x61: /* LD H,C */  SET_H(C); z->cycles += 4; break;
    case 0x62: /* LD H,D */  SET_H(D); z->cycles += 4; break;
    case 0x63: /* LD H,E */  SET_H(E); z->cycles += 4; break;
    case 0x64: /* LD H,H */  z->cycles += 4; break;
    case 0x65: /* LD H,L */  SET_H(L); z->cycles += 4; break;
    case 0x66: /* LD H,(HL)*/ SET_H(rd(z, z->hl)); z->cycles += 7; break;
    case 0x67: /* LD H,A */  SET_H(A); z->cycles += 4; break;
    case 0x68: /* LD L,B */  SET_L(B); z->cycles += 4; break;
    case 0x69: /* LD L,C */  SET_L(C); z->cycles += 4; break;
    case 0x6A: /* LD L,D */  SET_L(D); z->cycles += 4; break;
    case 0x6B: /* LD L,E */  SET_L(E); z->cycles += 4; break;
    case 0x6C: /* LD L,H */  SET_L(H); z->cycles += 4; break;
    case 0x6D: /* LD L,L */  z->cycles += 4; break;
    case 0x6E: /* LD L,(HL)*/ SET_L(rd(z, z->hl)); z->cycles += 7; break;
    case 0x6F: /* LD L,A */  SET_L(A); z->cycles += 4; break;

    case 0x70: /* LD (HL),B */ wr(z, z->hl, B); z->cycles += 7; break;
    case 0x71: /* LD (HL),C */ wr(z, z->hl, C); z->cycles += 7; break;
    case 0x72: /* LD (HL),D */ wr(z, z->hl, D); z->cycles += 7; break;
    case 0x73: /* LD (HL),E */ wr(z, z->hl, E); z->cycles += 7; break;
    case 0x74: /* LD (HL),H */ wr(z, z->hl, H); z->cycles += 7; break;
    case 0x75: /* LD (HL),L */ wr(z, z->hl, L); z->cycles += 7; break;
    case 0x76: /* HALT */
        z->halted = true;
        z->pc--;  /* Re-execute HALT until interrupt */
        z->cycles += 4;
        break;
    case 0x77: /* LD (HL),A */ wr(z, z->hl, A); z->cycles += 7; break;

    case 0x78: /* LD A,B */  SET_A(B); z->cycles += 4; break;
    case 0x79: /* LD A,C */  SET_A(C); z->cycles += 4; break;
    case 0x7A: /* LD A,D */  SET_A(D); z->cycles += 4; break;
    case 0x7B: /* LD A,E */  SET_A(E); z->cycles += 4; break;
    case 0x7C: /* LD A,H */  SET_A(H); z->cycles += 4; break;
    case 0x7D: /* LD A,L */  SET_A(L); z->cycles += 4; break;
    case 0x7E: /* LD A,(HL)*/ SET_A(rd(z, z->hl)); z->cycles += 7; break;
    case 0x7F: /* LD A,A */  z->cycles += 4; break;

    /* ========================= 0x80 - 0xBF: ALU A,r ========================= */
    case 0x80: alu_add8(z, B); z->cycles += 4; break;
    case 0x81: alu_add8(z, C); z->cycles += 4; break;
    case 0x82: alu_add8(z, D); z->cycles += 4; break;
    case 0x83: alu_add8(z, E); z->cycles += 4; break;
    case 0x84: alu_add8(z, H); z->cycles += 4; break;
    case 0x85: alu_add8(z, L); z->cycles += 4; break;
    case 0x86: alu_add8(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0x87: alu_add8(z, A); z->cycles += 4; break;
    case 0x88: alu_adc8(z, B); z->cycles += 4; break;
    case 0x89: alu_adc8(z, C); z->cycles += 4; break;
    case 0x8A: alu_adc8(z, D); z->cycles += 4; break;
    case 0x8B: alu_adc8(z, E); z->cycles += 4; break;
    case 0x8C: alu_adc8(z, H); z->cycles += 4; break;
    case 0x8D: alu_adc8(z, L); z->cycles += 4; break;
    case 0x8E: alu_adc8(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0x8F: alu_adc8(z, A); z->cycles += 4; break;

    case 0x90: alu_sub8(z, B); z->cycles += 4; break;
    case 0x91: alu_sub8(z, C); z->cycles += 4; break;
    case 0x92: alu_sub8(z, D); z->cycles += 4; break;
    case 0x93: alu_sub8(z, E); z->cycles += 4; break;
    case 0x94: alu_sub8(z, H); z->cycles += 4; break;
    case 0x95: alu_sub8(z, L); z->cycles += 4; break;
    case 0x96: alu_sub8(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0x97: alu_sub8(z, A); z->cycles += 4; break;
    case 0x98: alu_sbc8(z, B); z->cycles += 4; break;
    case 0x99: alu_sbc8(z, C); z->cycles += 4; break;
    case 0x9A: alu_sbc8(z, D); z->cycles += 4; break;
    case 0x9B: alu_sbc8(z, E); z->cycles += 4; break;
    case 0x9C: alu_sbc8(z, H); z->cycles += 4; break;
    case 0x9D: alu_sbc8(z, L); z->cycles += 4; break;
    case 0x9E: alu_sbc8(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0x9F: alu_sbc8(z, A); z->cycles += 4; break;

    case 0xA0: alu_and(z, B); z->cycles += 4; break;
    case 0xA1: alu_and(z, C); z->cycles += 4; break;
    case 0xA2: alu_and(z, D); z->cycles += 4; break;
    case 0xA3: alu_and(z, E); z->cycles += 4; break;
    case 0xA4: alu_and(z, H); z->cycles += 4; break;
    case 0xA5: alu_and(z, L); z->cycles += 4; break;
    case 0xA6: alu_and(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0xA7: alu_and(z, A); z->cycles += 4; break;
    case 0xA8: alu_xor(z, B); z->cycles += 4; break;
    case 0xA9: alu_xor(z, C); z->cycles += 4; break;
    case 0xAA: alu_xor(z, D); z->cycles += 4; break;
    case 0xAB: alu_xor(z, E); z->cycles += 4; break;
    case 0xAC: alu_xor(z, H); z->cycles += 4; break;
    case 0xAD: alu_xor(z, L); z->cycles += 4; break;
    case 0xAE: alu_xor(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0xAF: alu_xor(z, A); z->cycles += 4; break;

    case 0xB0: alu_or(z, B); z->cycles += 4; break;
    case 0xB1: alu_or(z, C); z->cycles += 4; break;
    case 0xB2: alu_or(z, D); z->cycles += 4; break;
    case 0xB3: alu_or(z, E); z->cycles += 4; break;
    case 0xB4: alu_or(z, H); z->cycles += 4; break;
    case 0xB5: alu_or(z, L); z->cycles += 4; break;
    case 0xB6: alu_or(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0xB7: alu_or(z, A); z->cycles += 4; break;
    case 0xB8: alu_cp(z, B); z->cycles += 4; break;
    case 0xB9: alu_cp(z, C); z->cycles += 4; break;
    case 0xBA: alu_cp(z, D); z->cycles += 4; break;
    case 0xBB: alu_cp(z, E); z->cycles += 4; break;
    case 0xBC: alu_cp(z, H); z->cycles += 4; break;
    case 0xBD: alu_cp(z, L); z->cycles += 4; break;
    case 0xBE: alu_cp(z, rd(z, z->hl)); z->cycles += 7; break;
    case 0xBF: alu_cp(z, A); z->cycles += 4; break;

    /* ========================= 0xC0 - 0xFF: Control ========================= */
    case 0xC0: /* RET NZ */ if (!(F & ZF_Z)) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xC1: /* POP BC */ z->bc = pop16(z); z->cycles += 10; break;
    case 0xC2: /* JP NZ,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_Z)) z->pc = addr; z->cycles += 10; break; }
    case 0xC3: /* JP nn */  z->pc = fetch16(z); z->cycles += 10; break;
    case 0xC4: /* CALL NZ,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_Z)) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xC5: /* PUSH BC */ push16(z, z->bc); z->cycles += 11; break;
    case 0xC6: /* ADD A,n */ alu_add8(z, fetch8(z)); z->cycles += 7; break;
    case 0xC7: /* RST 00 */ push16(z, z->pc); z->pc = 0x00; z->cycles += 11; break;
    case 0xC8: /* RET Z */  if (F & ZF_Z) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xC9: /* RET */    z->pc = pop16(z); z->cycles += 10; break;
    case 0xCA: /* JP Z,nn */{ uint16_t addr = fetch16(z); if (F & ZF_Z) z->pc = addr; z->cycles += 10; break; }
    case 0xCB: /* CB prefix */ exec_cb(z); break;
    case 0xCC: /* CALL Z,nn */ { uint16_t addr = fetch16(z); if (F & ZF_Z) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xCD: /* CALL nn */ { uint16_t addr = fetch16(z); push16(z, z->pc); z->pc = addr; z->cycles += 17; break; }
    case 0xCE: /* ADC A,n */ alu_adc8(z, fetch8(z)); z->cycles += 7; break;
    case 0xCF: /* RST 08 */ push16(z, z->pc); z->pc = 0x08; z->cycles += 11; break;

    case 0xD0: /* RET NC */ if (!(F & ZF_C)) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xD1: /* POP DE */ z->de = pop16(z); z->cycles += 10; break;
    case 0xD2: /* JP NC,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_C)) z->pc = addr; z->cycles += 10; break; }
    case 0xD3: /* OUT (n),A */ { uint8_t port = fetch8(z); io_wr(z, port, A); z->cycles += 11; break; }
    case 0xD4: /* CALL NC,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_C)) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xD5: /* PUSH DE */ push16(z, z->de); z->cycles += 11; break;
    case 0xD6: /* SUB n */  alu_sub8(z, fetch8(z)); z->cycles += 7; break;
    case 0xD7: /* RST 10 */ push16(z, z->pc); z->pc = 0x10; z->cycles += 11; break;
    case 0xD8: /* RET C */  if (F & ZF_C) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xD9: /* EXX */ {
        uint16_t t;
        t = z->bc; z->bc = z->bc2; z->bc2 = t;
        t = z->de; z->de = z->de2; z->de2 = t;
        t = z->hl; z->hl = z->hl2; z->hl2 = t;
        z->cycles += 4;
        break;
    }
    case 0xDA: /* JP C,nn */ { uint16_t addr = fetch16(z); if (F & ZF_C) z->pc = addr; z->cycles += 10; break; }
    case 0xDB: /* IN A,(n) */ { uint8_t port = fetch8(z); SET_A(io_rd(z, port)); z->cycles += 11; break; }
    case 0xDC: /* CALL C,nn */ { uint16_t addr = fetch16(z); if (F & ZF_C) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xDD: /* DD prefix (IX) */ exec_dd_fd(z, &z->ix); break;
    case 0xDE: /* SBC A,n */ alu_sbc8(z, fetch8(z)); z->cycles += 7; break;
    case 0xDF: /* RST 18 */ push16(z, z->pc); z->pc = 0x18; z->cycles += 11; break;

    case 0xE0: /* RET PO */ if (!(F & ZF_PV)) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xE1: /* POP HL */ z->hl = pop16(z); z->cycles += 10; break;
    case 0xE2: /* JP PO,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_PV)) z->pc = addr; z->cycles += 10; break; }
    case 0xE3: /* EX (SP),HL */ {
        uint16_t val = rd16(z, z->sp);
        wr16(z, z->sp, z->hl);
        z->hl = val;
        z->cycles += 19;
        break;
    }
    case 0xE4: /* CALL PO,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_PV)) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xE5: /* PUSH HL */ push16(z, z->hl); z->cycles += 11; break;
    case 0xE6: /* AND n */  alu_and(z, fetch8(z)); z->cycles += 7; break;
    case 0xE7: /* RST 20 */ push16(z, z->pc); z->pc = 0x20; z->cycles += 11; break;
    case 0xE8: /* RET PE */ if (F & ZF_PV) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xE9: /* JP (HL) */ z->pc = z->hl; z->cycles += 4; break;
    case 0xEA: /* JP PE,nn */ { uint16_t addr = fetch16(z); if (F & ZF_PV) z->pc = addr; z->cycles += 10; break; }
    case 0xEB: /* EX DE,HL */ { uint16_t t = z->de; z->de = z->hl; z->hl = t; z->cycles += 4; break; }
    case 0xEC: /* CALL PE,nn */ { uint16_t addr = fetch16(z); if (F & ZF_PV) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xED: /* ED prefix */ exec_ed(z); break;
    case 0xEE: /* XOR n */  alu_xor(z, fetch8(z)); z->cycles += 7; break;
    case 0xEF: /* RST 28 */ push16(z, z->pc); z->pc = 0x28; z->cycles += 11; break;

    case 0xF0: /* RET P */  if (!(F & ZF_S)) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xF1: /* POP AF */ z->af = pop16(z); z->cycles += 10; break;
    case 0xF2: /* JP P,nn */{ uint16_t addr = fetch16(z); if (!(F & ZF_S)) z->pc = addr; z->cycles += 10; break; }
    case 0xF3: /* DI */     z->iff1 = 0; z->iff2 = 0; z->cycles += 4; break;
    case 0xF4: /* CALL P,nn */ { uint16_t addr = fetch16(z); if (!(F & ZF_S)) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xF5: /* PUSH AF */ push16(z, z->af); z->cycles += 11; break;
    case 0xF6: /* OR n */   alu_or(z, fetch8(z)); z->cycles += 7; break;
    case 0xF7: /* RST 30 */ push16(z, z->pc); z->pc = 0x30; z->cycles += 11; break;
    case 0xF8: /* RET M */  if (F & ZF_S) { z->pc = pop16(z); z->cycles += 11; } else { z->cycles += 5; } break;
    case 0xF9: /* LD SP,HL */ z->sp = z->hl; z->cycles += 6; break;
    case 0xFA: /* JP M,nn */{ uint16_t addr = fetch16(z); if (F & ZF_S) z->pc = addr; z->cycles += 10; break; }
    case 0xFB: /* EI */     z->ei_pending = true; z->cycles += 4; break;
    case 0xFC: /* CALL M,nn */ { uint16_t addr = fetch16(z); if (F & ZF_S) { push16(z, z->pc); z->pc = addr; z->cycles += 17; } else { z->cycles += 10; } break; }
    case 0xFD: /* FD prefix (IY) */ exec_dd_fd(z, &z->iy); break;
    case 0xFE: /* CP n */   alu_cp(z, fetch8(z)); z->cycles += 7; break;
    case 0xFF: /* RST 38 */ push16(z, z->pc); z->pc = 0x38; z->cycles += 11; break;
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void Z80_Init(Z80* z)
{
    init_tables();
    memset(z, 0, sizeof(Z80));
    z->af = 0xFFFF;
    z->sp = 0xFFFF;
    z->im = 1;
}

void Z80_Reset(Z80* z)
{
    z->pc = 0x0000;
    z->af = 0xFFFF;
    z->sp = 0xFFFF;
    z->i = 0;
    z->r = 0;
    z->iff1 = 0;
    z->iff2 = 0;
    z->im = 1;
    z->halted = false;
    z->nmi_pending = false;
    z->irq_line = false;
    z->ei_pending = false;
}

void Z80_NMI(Z80* z)
{
    z->nmi_pending = true;
}

void Z80_IRQ(Z80* z, bool level)
{
    z->irq_line = level;
}

int Z80_Run(Z80* z, int target_cycles)
{
    z->cycles = 0;
    z->target = target_cycles;

    while (z->cycles < z->target) {
        /* --- Handle NMI (highest priority) --- */
        if (z->nmi_pending) {
            z->nmi_pending = false;
            z->halted = false;
            z->iff2 = z->iff1;
            z->iff1 = 0;
            push16(z, z->pc);
            z->pc = 0x0066;
            z->cycles += 11;
            continue;
        }

        /* --- Handle maskable IRQ --- */
        if (z->irq_line && z->iff1 && !z->ei_pending) {
            z->halted = false;
            z->iff1 = 0;
            z->iff2 = 0;
            switch (z->im) {
            case 0:
                /* IM 0: execute instruction on data bus (usually RST 38h) */
                push16(z, z->pc);
                z->pc = 0x0038;
                z->cycles += 13;
                break;
            case 1:
                /* IM 1: RST 38h */
                push16(z, z->pc);
                z->pc = 0x0038;
                z->cycles += 13;
                break;
            case 2: {
                /* IM 2: indirect via I register + data bus byte */
                push16(z, z->pc);
                uint16_t vec_addr = (uint16_t)((z->i << 8) | 0xFF);
                z->pc = rd16(z, vec_addr);
                z->cycles += 19;
                break;
            }
            }
            continue;
        }

        /* --- HALT: burn cycles until interrupt --- */
        if (z->halted) {
            /* Consume remaining cycles as NOP (4 T-states each) */
            int remaining = z->target - z->cycles;
            if (remaining <= 0) break;
            int nops = (remaining + 3) / 4;
            z->cycles += nops * 4;
            /* Keep R advancing */
            z->r = (z->r & 0x80) | ((z->r + (uint8_t)nops) & 0x7F);
            break;
        }

        /* --- Delayed EI: enable interrupts BEFORE the next instruction.
         *     EI sets ei_pending=true.  On the NEXT iteration the IRQ
         *     check is blocked by !ei_pending, then we clear ei_pending
         *     and set iff1/iff2 here, then exec_one runs the instruction
         *     AFTER EI (typically RETI).  Only on the iteration AFTER
         *     that will the IRQ check pass (iff1=1, ei_pending=false). */
        if (z->ei_pending) {
            z->ei_pending = false;
            z->iff1 = 1;
            z->iff2 = 1;
        }

        /* --- Execute one instruction --- */
        exec_one(z);
    }

    return z->cycles;
}
