#!/bin/sh
# Compara a RAM do Z80 ($F800-$FFFF) com a do MAME no mesmo vblank, pra uma
# lista de jogos. É a validação de ÁUDIO mais barata que existe: se o driver
# de som do Z80 anda igual, a RAM dele bate byte a byte.
# Uso: tools/z80ram_vs_mame.sh <vblank> jogo1 jogo2 ...
set -u
VB=$1; shift
M=third_party/mame
OUT=build/z80ram
mkdir -p "$OUT"
: > "$M/_z80.txt"
i=1; while [ $i -le "$VB" ]; do echo gvblank >> "$M/_z80.txt"; i=$((i+1)); done
echo "save _z80_out.bin,0xf800:audiocpu,0x800" >> "$M/_z80.txt"
echo quit >> "$M/_z80.txt"
for g in "$@"; do
    if [ ! -f "$OUT/${g}_mame_$VB.bin" ]; then
        rm -rf "$M/nvram/$g" "$M/_z80_out.bin"
        ( cd "$M" && MSYS_NO_PATHCONV=1 ./mame.exe "$g" -bios euro -debug -debugscript _z80.txt \
            -window -nomaximize -rompath "roms;../../roms/zips" -skip_gameinfo -nothrottle >/dev/null 2>&1 )
        [ -f "$M/_z80_out.bin" ] && mv "$M/_z80_out.bin" "$OUT/${g}_mame_$VB.bin"
    fi
    ./build/core/debug_wram_at.exe "roms/zips/$g.zip" roms/neogeo "$VB" "$OUT/${g}_ours_$VB.bin" z >/dev/null 2>&1
    python -c "
import sys,os
a='$OUT/${g}_ours_$VB.bin'; b='$OUT/${g}_mame_$VB.bin'
if not os.path.exists(b): print('%-10s sem dump do MAME'%'$g'); sys.exit()
x=open(a,'rb').read(); y=open(b,'rb').read()
d=sum(1 for i in range(min(len(x),len(y))) if x[i]!=y[i])
print('%-10s z80 ram: %4d de %d bytes diferentes'%('$g',d,len(y)))"
done
