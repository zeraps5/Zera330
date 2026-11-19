"""Compara a SAÍDA DE ÁUDIO nossa com a do MAME (-wavwrite -samplerate 55555).

1. alinha no tempo por correlação cruzada do envelope (atraso de início);
2. estima o ganho (o MAME mistura com volumes próprios: FM/SSG/ADPCM);
3. mede, em janelas de 50 ms: correlação da forma de onda, erro residual
   relativo e diferença de energia — e aponta onde piora.
Uso: python tools/audio_vs_mame.py <mame.wav> <nosso.wav> [segundos_ignorar]"""
import sys, wave
import numpy as np

def load(p):
    w = wave.open(p)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
    a = a.reshape(-1, w.getnchannels())
    return a.mean(axis=1), w.getframerate()

m, rm = load(sys.argv[1]); o, ro = load(sys.argv[2])
assert rm == ro, (rm, ro)
rate = rm
skip = int(float(sys.argv[3]) * rate) if len(sys.argv) > 3 else 0

# alinhamento grosso pelo envelope (blocos de 1 ms), depois fino na forma de onda
blk = rate // 1000
def env(x):
    n = len(x) // blk
    return np.abs(x[:n * blk]).reshape(n, blk).mean(axis=1)
em, eo = env(m), env(o)
n = min(len(em), len(eo))
best, lag_b = -1, 0
for lag in range(-300, 301):
    a = em[max(0, lag):n + min(0, lag)]
    b = eo[max(0, -lag):n - max(0, lag)]
    if len(a) < 100 or a.std() == 0 or b.std() == 0: continue
    c = np.corrcoef(a, b)[0, 1]
    if c > best: best, lag_b = c, lag
coarse = lag_b * blk
best2, fine = -1, 0
seg = slice(skip + rate, skip + rate + rate * 3)
for d in range(coarse - blk * 2, coarse + blk * 2 + 1):
    a = m[seg.start + max(0, d): seg.stop + max(0, d)]
    b = o[seg.start + max(0, -d): seg.stop + max(0, -d)]
    L = min(len(a), len(b))
    if L < 1000 or a[:L].std() == 0 or b[:L].std() == 0: continue
    c = np.corrcoef(a[:L], b[:L])[0, 1]
    if c > best2: best2, fine = c, d
d = fine
A = m[max(0, d):]; B = o[max(0, -d):]
L = min(len(A), len(B)); A = A[skip:L]; B = B[skip:L]
gain = float(np.dot(A, B) / max(np.dot(B, B), 1e-9))
print('atraso MAME-nosso: %d amostras (%.2f ms) | ganho MAME/nosso: %.3f' % (d, d * 1000.0 / rate, gain))
print('correlacao global da forma de onda: %.4f' % np.corrcoef(A, B)[0, 1])
res = A - gain * B
print('erro residual relativo global: %.1f%%' % (100 * np.sqrt((res ** 2).mean() / max((A ** 2).mean(), 1e-9))))
win = rate // 20
rows = []
for i in range(0, L - skip - win, win):
    a = A[i:i + win]; b = B[i:i + win] * gain
    if a.std() < 20: continue
    c = np.corrcoef(a, b)[0, 1] if b.std() > 0 else 0
    rows.append((i / rate, c, 100 * np.sqrt(((a - b) ** 2).mean() / (a ** 2).mean()), 20 * np.log10((b.std() + 1e-9) / a.std())))
rows = np.array(rows)
print('janelas de 50 ms com som: %d | correlacao mediana %.3f | erro mediano %.1f%% | energia mediana %+.2f dB'
      % (len(rows), np.median(rows[:, 1]), np.median(rows[:, 2]), np.median(rows[:, 3])))
bad = rows[rows[:, 1] < 0.9]
print('janelas com correlacao < 0.9: %d' % len(bad))
for t, c, e, db in bad[:12]:
    print('   t=%6.2fs corr=%.3f erro=%.0f%% energia=%+.1f dB' % (t, c, e, db))
