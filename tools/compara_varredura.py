"""Compara duas varreduras nossas contra as referências do MAME.
Uso: python tools/compara_varredura.py <vb> <dir_antes> <dir_depois>
  vb = 200 (usa build/v200_mame) ou 1200 (usa build/varredura_mame)
Imprime o histograma de diferença de pixels e quais jogos pioraram/melhoraram
mais de 3000 pixels."""
import glob, os, sys
from PIL import Image

vb, antes, depois = sys.argv[1], sys.argv[2], sys.argv[3]
ref = 'build/v200_mame' if vb == '200' else 'build/varredura_mame'
T = 71680

def diff(a, b):
    x = Image.open(a).convert('RGB').tobytes()
    y = Image.open(b).convert('RGB').tobytes()
    return sum(1 for i in range(0, len(x), 3) if x[i:i+3] != y[i:i+3])

rows = []
for f in sorted(glob.glob(ref + '/*.png')):
    g = os.path.basename(f)[:-4]
    a, d = '%s/%s.bmp' % (antes, g), '%s/%s.bmp' % (depois, g)
    if os.path.exists(a) and os.path.exists(d):
        rows.append((g, diff(a, f), diff(d, f)))

def hist(i):
    return (sum(1 for r in rows if r[i] == 0), sum(1 for r in rows if 0 < r[i] <= T * .02),
            sum(1 for r in rows if T * .02 < r[i] <= T * .5), sum(1 for r in rows if r[i] > T * .5))

print('vb%s, %d jogos' % (vb, len(rows)))
print('  antes : 0px=%d <=2%%=%d 2-50%%=%d >50%%=%d' % hist(1))
print('  depois: 0px=%d <=2%%=%d 2-50%%=%d >50%%=%d' % hist(2))
print('  pioraram  :', [(g, a, d) for g, a, d in rows if d > a + 3000])
print('  melhoraram:', [(g, a, d) for g, a, d in rows if a > d + 3000])
