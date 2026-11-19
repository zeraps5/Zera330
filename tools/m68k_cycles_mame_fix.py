"""Achado #96: aplica sobre a tabela gerada do Musashi as correções de custo
MEDIDAS contra o núcleo 68000 do MAME (tools/cycles_vs_mame.py em 6 jogos,
~2 milhões de instruções). A tabela do Musashi é inconsistente justamente
nos casos das notas de rodapé da tabela da Motorola ("total de 8 quando o
endereço efetivo é registrador direto" etc) — dá pra ver pelas irmãs:
addq.w em An = 4 mas subq.w em An = 8; andi.l = 14 mas ori/eori/addi/subi.l = 16.

Regras (generalizadas por família, cada uma com evidência medida):
  ADD.L/SUB.L/AND.L/OR.L  <Rn>,Dn      -> 8   (medido add.l/sub.l/or.l: 8, tabela 6)
  ADDA.L/SUBA.L           <Rn>,An      -> 8   (medido adda.l/suba.l: 8, tabela 6)
  ADDA.W/SUBA.W           #imm,An      -> 12  (medido adda.w #: 12, tabela 14)
  ADDQ.W                  #,An         -> 8   (medido: 8, tabela 4; igual subq.w)
  ANDI.L                  #,Dn         -> 16  (medido: 16, tabela 14; igual ori.l)
  BCLR                    #n,Dn        -> 12  (medido: 12, tabela 14)
  BSET/BCHG               #n,Dn        -> 10  (medido bset: 10, tabela 12)
Reescreve core/src/cpu/m68k/m68k_cycles_000.inc a partir de
core/src/cpu/m68k/m68k_cycles_musashi.inc (a saída pura do m68kmake)."""
import re

src = 'core/src/cpu/m68k/m68k_cycles_musashi.inc'
tab = [int(x) for x in re.findall(r'\d+', open(src).read().split('{', 1)[1])]
assert len(tab) == 65536
n = 0
for op in range(65536):
    hi = op >> 12
    mode = (op >> 3) & 7
    size = (op >> 6) & 3
    new = None
    # ADD/SUB/AND/OR <ea>,Dn long (bit8 = 0), ea registrador direto
    if hi in (0xD, 0x9, 0xC, 0x8) and (op & 0x0100) == 0 and size == 2 and mode in (0, 1):
        if not (hi in (0xC, 0x8) and mode == 1):  # AND/OR não aceitam An
            new = 8
    # ADDA.L / SUBA.L <Rn>,An  (1101/1001 rrr 111 mmm)
    if hi in (0xD, 0x9) and (op & 0x01C0) == 0x01C0 and mode in (0, 1):
        new = 8
    # ADDA.W / SUBA.W #imm,An  (rrr 011 111 100)
    if hi in (0xD, 0x9) and (op & 0x01FF) == 0x00FC:
        new = 12
    # ADDQ.W #,An  (0101 ddd 0 01 001 rrr)
    if (op & 0xF1F8) == 0x5048:
        new = 8
    # ANDI.L #,Dn
    if (op & 0xFFF8) == 0x0280:
        new = 16
    # BCLR / BSET / BCHG #n,Dn
    if (op & 0xFFF8) == 0x0880:
        new = 12
    if (op & 0xFFF8) in (0x08C0, 0x0840):
        new = 10
    if new is not None and tab[op] != 0 and tab[op] != new:
        tab[op] = new
        n += 1

out = ('/* GERADO de Musashi m68kops.c (m68kmake) + correções medidas contra o\n'
       ' * 68000 do MAME (tools/m68k_cycles_mame_fix.py, Achado #96). Nao editar\n'
       ' * a mao. */\nstatic const unsigned char m68k_cycles_000[65536] = {\n' +
       '\n'.join(','.join(str(v) for v in tab[a:a + 32]) + ',' for a in range(0, 65536, 32)) + '\n};\n')
open('core/src/cpu/m68k/m68k_cycles_000.inc', 'w').write(out)
print('opcodes corrigidos:', n)
