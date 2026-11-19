"""Acha a PRIMEIRA divergência (PC ou ciclo) entre um trace do MAME e o nosso.

MAME: trace arq,maincpu,noloop,{tracelog "%d ",totalcycles}
Nosso: NEOGEO_TRACE_CYC=arq debug_jump <zip> <bios> <vb> <n> ffffff ffffff
Alinha na 2ª passagem por um PC-âncora (padrão: vetor de IRQ da BIOS
$C00438 — o começo do trace do MAME, logo após um gvblank, é atípico), fixa
o deslocamento de ciclo e avisa quando o PC diverge ou o deslocamento muda
(instrução com custo diferente ou IRQ atendida em outro ponto).
Uso: python tools/trace_diverge.py <mame.txt> <nosso.txt> [ancora_hex]"""
import re, sys

m = []
for line in open(sys.argv[1], encoding='latin-1'):
    r = re.match(r'^(\d+) ([0-9A-F]{6}):\s*(.*)', line)
    if r:
        m.append((int(r.group(1)), r.group(2), r.group(3).strip()))
o = []
for line in open(sys.argv[2]):
    p = line.split()
    if len(p) >= 3:
        o.append((int(p[2]), p[0]))

ancora = sys.argv[3].upper() if len(sys.argv) > 3 else 'C00438'
ocorr = int(sys.argv[4]) if len(sys.argv) > 4 else 2


def nth(seq, pc, n=2):
    c = 0
    for i, e in enumerate(seq):
        if e[1] == pc:
            c += 1
            if c == n:
                return i
    raise SystemExit('ancora %s nao aparece %d vezes' % (pc, n))


mi, oi = nth(m, ancora, ocorr), nth(o, ancora, ocorr)
off = m[mi][0] - o[oi][0]
print('alinhado em %s: mame[%d] nosso[%d]  deslocamento de ciclo=%d' % (ancora, mi, oi, off))
n = min(len(m) - mi, len(o) - oi)
skip = 0  # passos nossos que sao so a entrada de IRQ (o MAME nao registra)
irqs = 0
for k in range(n):
    if oi + k + skip + 1 >= len(o):
        break
    (mc, mpc, mdis), (oc, opc) = m[mi + k], o[oi + k + skip]
    if mpc != opc and o[oi + k + skip + 1][1] == mpc and m[mi + k][0] - o[oi + k + skip + 1][0] == off:
        skip += 1; irqs += 1
        (oc, opc) = o[oi + k + skip]
    if mpc != opc or mc - oc != off:
        kind = 'PC' if mpc != opc else 'CICLO (desloc %d -> %d)' % (off, mc - oc)
        print('DIVERGE no passo %d (%s), ciclo nosso %d' % (k, kind, oc))
        print('  (IRQs casadas ate aqui: %d)' % irqs)
        for j in range(max(0, k - 12), min(n - skip, k + 6)):
            a, b = m[mi + j], o[oi + j + skip]
            print('  %s mame %11d %s %-30s | nosso %11d %s' % ('>>' if j == k else '  ', a[0] - off, a[1], a[2][:30], b[0], b[1]))
        break
else:
    print('identico em', n - skip, 'instrucoes (%d IRQs casadas)' % irqs)
