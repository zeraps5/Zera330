#!/bin/sh
# Compara a P ROM já DECIFRADA pelo nosso carregador com o que o MAME tem no
# espaço do 68000 logo no início: área fixa ($80-$FFFFF) e banco padrão
# ($200000-$2FFFFF = nosso p[$100000...]). É a validação mais forte possível
# de uma descriptografia de programa.
# Uso: tools/prom_vs_mame.sh jogo1 jogo2 ...
set -u
M=third_party/mame
OUT=build/prom
mkdir -p "$OUT"
printf 'save _pf.bin,0x80,0xfff80\nsave _pb.bin,0x200000,0x100000\nquit\n' > "$M/_prom.txt"
for g in "$@"; do
    rm -rf "$M/nvram/$g" "$M/_pf.bin" "$M/_pb.bin"
    ( cd "$M" && MSYS_NO_PATHCONV=1 ./mame.exe "$g" -bios euro -debug -debugscript _prom.txt \
        -window -nomaximize -rompath "roms;../../roms/zips" -skip_gameinfo -nothrottle >/dev/null 2>&1 )
    ./build/core/debug_wram_at.exe "roms/zips/$g.zip" roms/neogeo 0 "$OUT/${g}_p.bin" p >/dev/null 2>&1
    python -c "
import os
g='$g'
if not os.path.exists('$M/_pf.bin'): print('%-9s sem dump do MAME'%g); raise SystemExit
p=open('$OUT/'+g+'_p.bin','rb').read(); f=open('$M/_pf.bin','rb').read(); b=open('$M/_pb.bin','rb').read()
df=sum(1 for i in range(len(f)) if 0x80+i>=len(p) or f[i]!=p[0x80+i])
db=sum(1 for i in range(len(b)) if 0x100000+i>=len(p) or b[i]!=p[0x100000+i])
print('%-9s fixo: %7d bytes diferentes | banco: %7d bytes diferentes'%(g,df,db))"
done
