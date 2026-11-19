"""Compara o custo em ciclos de cada opcode do 68000 medido no MAME com a
nossa tabela gerada do Musashi (core/src/cpu/m68k/m68k_cycles_000.inc).

Entrada: traces do MAME feitos com
    trace arq,maincpu,noloop,{tracelog "%d ",totalcycles}
em build/cyc/<jogo>.tr, mais build/cyc/bios.bin e build/cyc/<jogo>_p.bin
(debug_wram_at ... b / p) pra achar o opcode de cada PC.

Só considera instruções SEM ajuste dinâmico (fora Bcc/DBcc/Scc/shift em
registrador/MOVEM/MULx/DIVx) e sem interrupção no meio. Uso:
    python tools/cycles_vs_mame.py garou mslug doubledr ...
"""
import collections, re, sys

tab = [int(x) for x in re.findall(r'\d+', open('core/src/cpu/m68k/m68k_cycles_000.inc').read().split('{', 1)[1])]
assert len(tab) == 65536
bios = open('build/cyc/bios.bin', 'rb').read()

def dynamic(op):
    hi = op >> 12
    if hi == 0x6: return True
    if hi == 0x5 and (op & 0xC0) == 0xC0: return True
    if hi == 0xE and ((op >> 6) & 3) != 3: return True
    if hi == 0x4 and (op & 0xFB80) == 0x4880: return True
    if hi in (0x8, 0xC) and (op & 0x1C0) in (0xC0, 0x1C0): return True  # MULx/DIVx
    if op in (0x4E73, 0x4E75, 0x4E77, 0x4E40) or (op & 0xFFF0) == 0x4E40: return False
    return False

res = collections.defaultdict(collections.Counter)
mn_of = {}
for g in sys.argv[1:]:
    prom = open('build/cyc/%s_p.bin' % g, 'rb').read()
    rows = []
    for line in open('build/cyc/%s.tr' % g, encoding='latin-1'):
        m = re.match(r'^(\d+) ([0-9A-F]{6}): (\S+)', line)
        if m:
            rows.append([int(m.group(1)), int(m.group(2), 16), m.group(3), False])
        elif 'interrupted' in line and rows:
            rows[-1][3] = True
    for i in range(len(rows) - 1):
        cyc, pc, mn, irq = rows[i]
        if irq or rows[i + 1][3]:
            continue
        if pc >= 0xC00000:
            o = pc - 0xC00000
            if o + 1 >= len(bios): continue
            op = (bios[o] << 8) | bios[o + 1]
        elif pc < 0x100000 and pc >= 0x80:
            if pc + 1 >= len(prom): continue
            op = (prom[pc] << 8) | prom[pc + 1]
        else:
            continue
        if dynamic(op):
            continue
        res[op][rows[i + 1][0] - cyc] += 1
        mn_of[op] = mn

bad = []
for op, cnt in res.items():
    mame = cnt.most_common(1)[0][0]
    if len(cnt) == 1 and mame != tab[op]:
        bad.append((sum(cnt.values()), op, mn_of[op], mame, tab[op]))
bad.sort(reverse=True)
print('opcodes medidos: %d | com custo diferente (único no MAME): %d' % (len(res), len(bad)))
for n, op, mn, m, t in bad:
    print('  %04X %-8s mame=%3d nosso=%3d  (x%d)' % (op, mn, m, t, n))
multi = [(op, dict(c)) for op, c in res.items() if len(c) > 1]
print('opcodes com mais de um custo no MAME (dependem de dado/EA):', len(multi))
for op, c in multi[:30]:
    print('  %04X %-8s %s nosso=%d' % (op, mn_of[op], c, tab[op]))
