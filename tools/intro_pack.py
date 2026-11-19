"""Empacota o vídeo de abertura do zera330 no formato .z3i lido pelo frontend.

Entrada: a saída do tools/intro_extract.c (quadros RGB24 + WAV PCM 16 bits).
Saída:   assets/zera330_intro.z3i

Formato (little-endian):
  "Z3IN" u16 versão=1 u16 largura u16 altura u16 quadros u16 fps_num u16 fps_den
  u32 taxa_audio u16 canais u16 bits_cor(7) u32 bytes_audio u32 bytes_audio_z
  áudio s16 comprimido (zlib) | por quadro: u32 tamanho_z + zlib(delta)
O delta é (quadro >> 1) - (anterior >> 1) por byte, módulo 256; o decodificador
soma e volta pra 8 bits com (v << 1) | (v >> 6).

Uso: python tools/intro_pack.py <prefixo_extraido> [largura=640] [altura=360]
"""
import struct
import sys
import wave
import zlib

import numpy as np
from PIL import Image


def main():
    pre = sys.argv[1]
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 640
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 360
    sw, sh, n, fn, fd, rate, ch = map(int, open(pre + '.txt').read().split())
    with wave.open(pre + '.wav') as wf:
        pcm = wf.readframes(wf.getnframes())
    az = zlib.compress(pcm, 9)
    out = bytearray(b'Z3IN')
    out += struct.pack('<HHHHHH', 1, w, h, n, fn, fd)
    out += struct.pack('<IHHII', rate, ch, 7, len(pcm), len(az))
    out += az
    fs = sw * sh * 3
    prev = np.zeros((h, w, 3), np.uint8)
    with open('%s_%dx%d.rgb' % (pre, sw, sh), 'rb') as f:
        for _ in range(n):
            im = Image.frombytes('RGB', (sw, sh), f.read(fs)).resize((w, h), Image.LANCZOS)
            q = np.asarray(im) >> 1
            z = zlib.compress((q - prev).astype(np.uint8).tobytes(), 9)
            out += struct.pack('<I', len(z)) + z
            prev = q
    open('assets/zera330_intro.z3i', 'wb').write(out)
    print('assets/zera330_intro.z3i: %dx%d, %d quadros, %.2f MB' % (w, h, n, len(out) / 1e6))


if __name__ == '__main__':
    main()
