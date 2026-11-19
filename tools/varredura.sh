#!/bin/sh
# Varredura de compatibilidade: roda cada ZIP de roms/zips por N vblanks e
# salva um BMP em build/varredura/. A classificacao (preto / poucas cores /
# tela real) fica pro varredura_relatorio.py, que le os BMPs.
# Uso: tools/varredura.sh [vblanks]
set -u
VB=${1:-500}
OUT=${OUTDIR:-build/varredura}
mkdir -p "$OUT"
: > "$OUT/log.txt"
for z in roms/zips/*.zip; do
    g=$(basename "$z" .zip)
    r=$(./build/core/debug_screenshot.exe "$z" roms/neogeo "$VB" "$OUT/$g.bmp" 2>&1 | grep -E "^(ok|erro)" | head -1)
    echo "$g|$r" >> "$OUT/log.txt"
    echo "$g|$r"
done
