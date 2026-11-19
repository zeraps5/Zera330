"""Compara, instrução a instrução, o custo em ciclos do nosso 68000 com o do
MAME em traces ALINHADOS (mesmo PC na mesma sequência), até a primeira
divergência de caminho. Pega também as instruções de custo dinâmico.
Uso: python tools/cycles_aligned.py <trace_mame_com_totalcycles> <nosso NEOGEO_TRACE_CYC>"""
import collections, re, sys

m = []
for line in open(sys.argv[1], encoding='latin-1'):
    mm = re.match(r'^(\d+) ([0-9A-F]{6}): (\S+)(.*)$', line.rstrip())
    if mm:
        m.append([int(mm.group(1)), int(mm.group(2), 16), mm.group(3) + mm.group(4), False])
    elif 'interrupted' in line and m:
        m[-1][3] = True
o = []
for line in open(sys.argv[2]):
    pc, c, t = line.split()
    o.append((int(pc, 16), int(c), int(t)))

W = 48
idx = {}
for k in range(min(len(o) - W, 1500000)):
    idx.setdefault(tuple(p for p, _, _ in o[k:k + W]), k)
anc = None
for s in range(min(len(m) - W, 1500000)):
    w = tuple(r[1] for r in m[s:s + W])
    if len(set(w)) >= 20 and w in idx:
        anc = (s, idx[w]); break
if not anc:
    print('sem ancora'); sys.exit()
s, k = anc
bad = collections.Counter(); ex = {}
n = 0; i = 0
while s + i + 1 < len(m) and k + i < len(o):
    mr = m[s + i]; orow = o[k + i]
    if mr[1] != orow[0]:
        print('caminho diverge em', i, 'mame %06X nosso %06X' % (mr[1], orow[0])); break
    if not mr[3] and not m[s + i + 1][3]:
        mc = m[s + i + 1][0] - mr[0]
        oc = orow[1]
        n += 1
        if mc != oc:
            key = (mr[2].split()[0], mc, oc)
            bad[key] += 1
            ex.setdefault(key, '%06X %s' % (mr[1], mr[2]))
    i += 1
print('instrucoes comparadas:', n)
for (mn, mc, oc), c in bad.most_common(40):
    print('  %-8s mame=%3d nosso=%3d x%-6d ex: %s' % (mn, mc, oc, c, ex[(mn, mc, oc)]))
