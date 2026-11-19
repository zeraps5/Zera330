#!/bin/sh
# Gera, pro MESMO vblank da varredura, o snapshot do MAME de cada jogo — pra
# poder comparar pixel a pixel em vez de julgar "parece certo" no olho.
# Apaga o nvram de cada jogo antes de rodar: save antigo muda o caminho de
# boot e polui a comparacao.
# Uso: tools/varredura_mame.sh [vblanks]
set -u
VB=${1:-1200}
M=third_party/mame
OUT=${OUTDIR:-build/varredura_mame}
mkdir -p "$OUT"
SCRIPT="$M/_varre.txt"
i=1
: > "$M/_varre.txt"
while [ $i -le "$VB" ]; do echo gvblank >> "$SCRIPT"; i=$((i+1)); done
printf 'snap\nquit\n' >> "$SCRIPT"

for z in roms/zips/*.zip; do
    g=$(basename "$z" .zip)
    [ -f "$OUT/$g.png" ] && continue
    rm -rf "$M/snap/$g" "$M/nvram/$g"
    ( cd "$M" && MSYS_NO_PATHCONV=1 ./mame.exe "$g" -bios euro -debug \
        -debugscript _varre.txt -window -nomaximize \
        -rompath "roms;../../roms/zips" -skip_gameinfo -nothrottle \
        >/dev/null 2>&1 )
    if [ -f "$M/snap/$g/0000.png" ]; then
        cp "$M/snap/$g/0000.png" "$OUT/$g.png"
        echo "$g ok"
    else
        echo "$g SEM-SNAP"
    fi
done
