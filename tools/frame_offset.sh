#!/bin/sh
# Mede o deslocamento de ciclo (MAME totalcycles - nosso master_cycles) num
# vblank, alinhando os dois traces pela N-ésima passagem de um PC. Se o
# deslocamento muda ~202752 entre dois vblanks, um dos emuladores perdeu um
# quadro no meio.
# Uso: tools/frame_offset.sh <jogo> <vblank> <pc_ancora> [quadros=3]
set -u
G=$1; VB=$2; PC=$3; N=${4:-3}
M=third_party/mame
( cd "$M" && rm -rf "nvram/$G" "_fo.txt" && {
    i=1; while [ $i -le "$VB" ]; do echo gvblank; i=$((i+1)); done
    echo 'trace _fo.txt,maincpu,noloop,{tracelog "%d ",totalcycles}'
    i=1; while [ $i -le "$N" ]; do echo gvblank; i=$((i+1)); done
    echo traceoff; echo quit; } > _fo_script.txt &&
  MSYS_NO_PATHCONV=1 ./mame.exe "$G" -bios euro -debug -debugscript _fo_script.txt -window -nomaximize \
    -rompath "roms;../../roms/zips" -skip_gameinfo -nothrottle -sound none >/dev/null 2>&1 )
NEOGEO_TRACE_CYC=build/_fo_ours.txt ./build/core/debug_jump.exe "roms/zips/$G.zip" roms/neogeo "$VB" ${OURS_N:-150000} ffffff ffffff >/dev/null 2>&1
python tools/trace_diverge.py "$M/_fo.txt" build/_fo_ours.txt "$PC" 1 | head -1
