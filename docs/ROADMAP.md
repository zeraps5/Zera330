# Roadmap

Projeto grande, multi-sessão. Cada fase deve terminar com algo que compila e,
quando possível, um teste automatizado.

## Fase 0 — Esqueleto (EM ANDAMENTO)
- [x] Estrutura de diretórios (`core/`, `platform/`, `docs/`, `tools/`)
- [x] `docs/ARCHITECTURE.md` com decisões tomadas
- [ ] CMake raiz compilando uma lib `neocore` vazia + teste "hello"
- [ ] `.gitignore` (build/, roms/, *.crdownload)
- [ ] `git init` + primeiro commit

## Fase 1 — CPU 68000 (CONCLUÍDA via portabilidade — refinamento pendente)
- [x] Core portado de `third_party/m68k` (projeto Mega Drive do autor) —
      registradores, barramento via callbacks, reset, fetch/decode,
      exceções, IRQ autovetorado. Ver docs/ARCHITECTURE.md.
- [x] Smoke test (`core/tests/test_m68k_core.c`) passando
- [ ] Ajustar tabela de timing pra cycle-accurate real (hoje é aproximado)
- [ ] Validar contra test suite de referência (ex: Tom Harte 68000 tests)
- [ ] Revisar os ~5% de instruções/edge cases faltantes conforme
      `M68K_CPU_STATUS.md` de origem (trace mode, bus error automático —
      provavelmente não críticos pro Neo-Geo, mas checar)

## Fase 2 — CPU Z80 (som) (CONCLUÍDA via portabilidade — validação pendente)
- [x] Core portado de `third_party/z80` (mesmo projeto) — conjunto completo
      de instruções + prefixos, IM 0/1/2, NMI, portas de I/O
- [x] Smoke test (`core/tests/test_z80.c`) passando
- [ ] Validar timing (T-states) contra referência — origem já afirma
      cycle-accurate, mas não foi re-verificado neste projeto
- [ ] Conectar IRQ modo 1 ao esquema de interrupção de som do Neo-Geo
      (diferente do Mega Drive: sem VDP/banking do lado Z80, bankswitch de
      ROM de som é outro mecanismo)

## Fase 3 — Mapa de memória e sistema (ESQUELETO PRONTO — refinamento pendente)
- [x] Carregador de ROM (`core/src/system/rom_loader.c`), convenção de nomes
      MAME/FBNeo (p1/p2, s1, m1, v1-v4, c1/c2), com tolerância a algumas
      variações de extensão
- [x] Mapeamento do 68000 (`core/src/system/neogeo.c`): BIOS @0x000000,
      Work RAM @0x100000, P ROM do cartucho @0x200000, Palette RAM
      @0x400000 — todos espelhados corretamente na janela de endereço
- [x] Mapeamento do Z80: RAM (2KB, espelhada em 0x0000-0x7FFF), M1 fixo em
      0x8000-0xFFFF
- [x] Latch de comunicação 68000->Z80 via porta de I/O 0x00 (68k escreve em
      0x3E0000, Z80 lê com `IN A,(0)`) — testado ponta a ponta
      (`core/tests/test_neogeo.c`)
- [x] Scheduler básico (`neogeo_run_slice`): roda 68000 por um orçamento de
      ciclos, depois Z80 na proporção 3:1 (12MHz:4MHz)
- [ ] **TODO de precisão** (documentado como comentário no próprio
      `neogeo.c`, não adivinhado):
  - Bankswitch de P ROM > 1MB (cada cartucho usa um chip de proteção/banking
    diferente — CMC42/50, SMA, PVC, etc.) — hoje mapeia direto módulo o
    tamanho da ROM, errado pra jogos grandes
  - Bankswitch de M1 > 64KB no lado Z80 — hoje só os primeiros 32KB fixos
  - Endereço exato do latch de som (0x3E0000) e semântica real da porta de
    I/O 0x00 do Z80 — plausíveis mas não confirmados contra o MAME
  - Registradores de 0x300000-0x3FFFFF (dipswitches, watchdog, coin
    counters, regs do LSPC2) — todos "open bus"/ignorados por ora
- [ ] NVRAM/memory card
- [ ] Validar layout do carregador de ROM contra um romset real quando você
      tiver os arquivos prontos (ver pasta `roms/`)

## Fase 4 — Vídeo (LSPC2) (fix layer pronta — sprites pendentes)
- [x] Registradores confirmados contra a NeoGeo Development Wiki (não mais
      palpite): VRAMADDR/VRAMRW/VRAMMOD/LSPCMODE/TIMERHIGH/TIMERLOW/
      IRQACK/TIMERSTOP @ 0x3C0000-0x3C000E, REG_SOUND @ 0x320000
      (`core/include/neocore/neogeo.h`, `core/src/system/neogeo.c`)
- [x] VRAM interna do LSPC2 (68KiB) com protocolo de endereço/auto-incremento
      real (`vram_addr`/`vram_mod`)
- [x] IRQ de vblank (nível 1) disparado periodicamente em `neogeo_run_slice`,
      acknowledged via IRQACK — timing (~200000 ciclos/frame) é aproximado,
      não calibrado ainda
- [x] NMI de som real: escrita em REG_SOUND dispara `Z80_NMI` de verdade
      (antes só setava um latch sem interromper o Z80)
- [x] Fix layer (texto/HUD, tiles 8x8 4bpp) — `core/src/video/lspc_fix.c`,
      `neogeo_render_fix_layer()`. Testado ponta a ponta via registradores
      reais (`core/tests/test_video.c`)
- [x] Conversão de cor de paleta (dark bit + RGB 5-5-5) -> RGB888
- [ ] **TODO de precisão** (ver comentários no topo de `lspc_fix.c` e
      `neogeo.c`):
  - Seleção de banco de paleta (PALBANK0/1 @ 0x3A000F/0x3A001F) — fixo no
    banco 0 por ora
  - Recorte vertical exato da fix layer (32 linhas de tile na VRAM, só ~28
    visíveis — offset de overscan não confirmado)
  - Efeito exato do "dark bit" na cor final (assumido: divide RGB por 2)
  - Fix bankswitching (Type 2, mencionado na wiki) — não implementado
  - Timing exato do vblank (60 vs 59.99Hz, ciclos/frame calibrados)
- [ ] Sprite layer (16x16 tiles, até 384 sprites, zoom por scanline,
      "sticky sprites", paletas 0-255) — ainda não iniciado, é a parte mais
      trabalhosa da Fase 4. C ROMs (c1-c8) já são carregadas pelo rom_loader
      mas ainda não decodificadas (formato de tile de sprite é diferente do
      fix layer — 4bpp mas layout de bits distinto, conferir
      "Sprite graphics format" na wiki antes de implementar)
- [ ] Timer interrupt (nível 2) — registrado mas não gera IRQ ainda

## Fase 5 — Áudio (YM2610) (EM ANDAMENTO — infra pronta, sem som ainda)
- [x] Portas $04-$07 do Z80 roteadas pro chip (antes as escritas eram
      DESCARTADAS). Confirmado com `NEOGEO_TRACE_YM=1` que o Z80 roda o
      driver de som REAL do cartucho: sequência de init coerente (key-off
      dos 6 canais ADPCM-A, volume mestre, volumes SSG, timers A/B).
- [x] ADPCM-A (6 canais) implementado em `core/src/audio/ym2610.c` —
      tabela de passos, acumulador de 12 bits com wrap, volume/pan por
      canal, mixagem estéreo. FONTE: `ymfm` de Aaron Giles
      (`src/ymfm_adpcm.cpp` / `.h`), algoritmo e mapa de registrador
      copiados da referência, não deduzidos.
- [x] `neogeo_audio_render()` + `core/tools/debug_audio.c` (grava .wav pra
      ouvir/verificar offline, sem depender de placa de som).
- [x] NMI de som do Z80 ($08 liga / $18 desliga) — antes disparávamos NMI
      incondicionalmente a cada comando do 68k, o que re-entra no handler
      no meio do processamento.
- [x] **PRIMEIRO SOM SAIU** (ver Achados #55 e #56 abaixo).
- [ ] FM (4 canais) — não começado
- [ ] SSG (3 canais) e ADPCM-B — não começados
- [ ] Saída SDL2 (device de áudio + ring buffer) — não começada

## Fase 6 — Frontend SDL2 (Windows) (ADIANTADA — funcional básico)
- [x] SDL2 vendorizado em `third_party/SDL2-2.32.10` (baixado de
      github.com/libsdl-org/SDL/releases, licença zlib, com sua permissão)
- [x] `platform/sdl2/main.c`: janela + blit do framebuffer (via
      `neogeo_render_frame`), aceita `<dir_cartucho> [dir_bios]`
- [ ] Input (teclado/joystick), áudio, sincronização de vsync real — ainda
      não feitos (sem áudio ainda faz sentido, Fase 5 nem começou)

## Fase 7 — Integração e primeiro boot (EM ANDAMENTO — boot real avança, mas trava)
Romset de teste: Double Dragon (MVS) em `roms/doubledr/`, BIOS em
`roms/neogeo/` (sp-s2.sp1) e `roms/neogeo_uni/` (Universe BIOS 4.0, cópia de
`uni-bios_4_0.rom` renomeada pra `neogeo.rom` — só pra teste, não é a BIOS
"oficial"). Ferramenta de diagnóstico: `core/tools/debug_boot.c` (`debug_boot
<dir_cartucho> <dir_bios> [frames]`, imprime PC/SR/estado de exceção a cada
"frame" de ~200000 ciclos).

### Bugs reais encontrados e corrigidos (nessa ordem, cada um destravou o próximo)
1. **BIOS com bytes trocados em pares** — o arquivo `sp-s2.sp1` (e o uni-bios)
   vêm "byte-swapped". Detectado comparando o PC inicial resultante ($000204,
   sem sentido — código ali era 0xFF) com o valor documentado na wiki
   ($00C00402). Corrigido com detecção automática em `rom_loader.c`
   (verifica `bios[4]==0xC0 && bios[5]==0x00`, desfaz a troca na imagem
   inteira se detectado).
2. **Faltava mapear a BIOS em `0xC00000-0xC1FFFF`** — os vetores de exceção
   realmente apontam pra lá (confirmado na wiki, "Boot process"), não só pra
   `0x000000`. Sem isso, todo fetch de código real caía em open-bus e
   travava em loop de exceção (vetor 11, Line 1111 Emulator, sobre 0xFFFF).
   Corrigido em `neogeo.c` (`is_bios_addr`/`bios_offset`).
3. **`REG_STATUS_B` ($380000)** retornava "open bus" (tudo 1), o que sinaliza
   "sem memory card"/erro nos bits 5-4. Implementado retornando um valor
   padrão plausível (`0x8F`: MVS, sem proteção, card OK, nada pressionado).
4. **Scheduler rodava o frame inteiro (200000 ciclos) numa chamada só** —
   isso fazia `REG_LSPCMODE` (que devíamos calcular dinamicamente a partir
   do progresso do frame, pro bit de scanline) parecer estático durante
   TODO o frame pro código que fazia polling nele, e o próprio mecanismo de
   "detecção de loop estável" do core (m68k_core.c, otimização de
   performance) via isso e pulava ciclos sem re-executar nada. Corrigido
   dividindo `neogeo_run_slice` em fatias de 1 scanline
   (`NEOGEO_CYCLES_PER_SCANLINE`), atualizando `cycles_since_vblank` a cada
   fatia — isso destravou um loop de polling real (`LSR.W #7` +
   `SUBI.W #$F8` esperando a scanline chegar a 248).
5. **Bankswitch do P ROM** (`0x2FFFF0-0x2FFFFF`, confirmado contra o driver
   `neogeo.cpp` do MAME): implementado (`check_p_bankswitch` em `neogeo.c`).
   Correto e necessário pra cartuchos >1MB (Double Dragon tem 2MB), mas não
   foi o que causou o travamento atual (testado — o travamento é idêntico
   com e sem essa correção).

### Onde trava agora (não resolvido)
Depois dos 5 fixes acima, tanto a BIOS `sp-s2.sp1` quanto a Universe BIOS já
escrevem dados reais na fix layer e na paleta (progresso visível pela
primeira vez!). Mas ambas ficam presas num padrão idêntico: um loop de 2
instruções (`MOVE.B D0,$300001` — kick do watchdog — seguido de `BRA` de
volta pra si mesmo, incondicional). Esse loop só pode terminar via uma
interrupção (a IRQ de vblank, nível 1) — mas o `SR` da CPU nunca sai de
"máscara=7" (todas as interrupções bloqueadas) do reset até esse ponto, em
nenhum dos +400 frames testados (confirmado varrendo todos os valores de SR
da execução: só o byte alto 0x27 aparece, nunca mudando de máscara).

Duas hipóteses não resolvidas:
- É um travamento **intencional** (alguma verificação de hardware/cartucho
  que ainda falha silenciosamente por causa de outro registrador que não
  implementamos, e a BIOS trava de propósito em vez de seguir com estado
  inválido); ou
- Existe uma instrução mais adiante que LIGARIA as interrupções, mas o fluxo
  nunca chega até ela por causa de outro registrador/condição que estamos
  simulando errado.

### Atualização: comparado contra trace real do MAME (muito mais eficiente)
Instalamos o MAME oficial (`third_party/mame/mame.exe`, baixado de
github.com/mamedev/mame/releases, ~88MB, com sua permissão) e geramos um
trace de execução real via `-debug -debugscript` (comando debugger `trace
arquivo.log,maincpu` + `go`). Também criamos `core/tools/trace_boot.c`
(`trace_boot <cart> <bios> <n_passos>` — usa `m68k_core_step` direto, uma
linha de PC por instrução) pra comparar PC a PC contra o trace do MAME.

**Resultado**: nosso 68000 executa em **lockstep perfeito** com o MAME por
milhões de instruções — mesma sequência exata de PCs, incluindo os mesmos
loops de espera (`C0996E`/`C09980`, o polling de LSPCMODE) que o MAME também
visita e sai deles do mesmo jeito. Isso valida fortemente que o core 68000
portado e nosso mapa de memória básico estão corretos.

A divergência real acontece bem mais adiante, em `0xC05BE2` (Universe BIOS):
a BIOS lê a palavra de VRAM em `$7151` (dentro do fix map, via
VRAMADDR=$7151/VRAMRW) e faz `BTST #4,D0` / `BTST #5` / `BTST #7` num loop
(`BRA -24` se todos os bits testados vierem 1). O preenchimento do fix map
que já observamos (tudo `$00FF`) tem bits 4,5,7 todos = 1 — então o loop
nunca sai, porque nada em nossa emulação nunca escreve um valor diferente
nessa posição específica da VRAM depois do preenchimento inicial. No
hardware real, presumivelmente alguma outra sub-rotina/task (talvez ligada
a DMA de sprite, ou um handshake com o vídeo que não implementamos) atualiza
essa palavra especifica. **Não identificamos ainda qual mecanismo escreve
lá** — é o próximo ponto a investigar.

### Mais 2 bugs reais encontrados e corrigidos comparando trace a trace
6. **`m68k_core_run_cycles` tem uma otimização de "detecção de loop estável"
   insegura pra registradores com efeito colateral por acesso** (como
   VRAMRW, que auto-incrementa `vram_addr` a cada leitura/escrita — ver
   `vram_rw_read`/`vram_rw_write`). Comparar `debug_boot.c` (usava
   `run_cycles`) contra `trace_boot.c` (usa `m68k_core_step` direto) mostrou
   os dois travando em PONTOS DIFERENTES pra uma quantidade de trabalho
   equivalente — a otimização estava alterando o comportamento observável.
   Corrigido: `neogeo_run_slice` agora usa `m68k_core_step` diretamente,
   sem o atalho. Custa performance, ganha corretude.
7. **Backup RAM (`0xD00000-0xD0FFFF`) nunca foi implementada** — todo
   acesso caía em open-bus. A BIOS testa/formata essa RAM logo no boot
   (documentado na wiki, seção "Boot process"). Implementado como RAM
   simples de 64KB, zerada em `neogeo_init` (não persiste entre execuções
   do processo ainda — TODO se quisermos fidelidade de save/config).

### Metodologia validada: comparar trace a trace contra o MAME funciona
Confirmamos (via `core/tools/trace_boot.c`, que usa `m68k_core_step()` e
imprime um PC por linha) que nosso 68000 roda em **lockstep perfeito** com
o MAME por **centenas de milhares de instruções**, incluindo pontos de
verificação bem específicos (leitura de STATUS_A/RTC, escrita em
PALBANK0/1, chamadas de sub-rotina exatas). Isso valida fortemente o core
68000 portado e o mapa de memória.

### Onde paramos: divergência em `0xC125DE` — provavelmente não é bug
Em `0xC125DE` (`bne $c125e6`, parte de um loop `cmpm.b` comparando 16 bytes
entre uma tabela na ROM e `$D00010`, dentro de backup RAM), nós tomamos o
caminho "não bateu" (RAM em branco, esperado num boot "de fábrica"),
enquanto o MAME toma o caminho "bateu". **Motivo provável, não bug nosso**:
o NVRAM do MAME já vem pré-formatado/válido por padrão (comportamento do
próprio driver/MAME pra conveniência), então ele **nunca executa** a rotina
de formatação da backup RAM que nossa emulação (corretamente, com RAM em
branco) é forçada a percorrer. `0xC05BE2` (onde ficamos presos) pode ser
parte LEGÍTIMA dessa rotina de formatação, não necessariamente um bug —
só não conseguimos confirmar isso terminando de desmontar a rotina inteira
nesta sessão.

- [x] ~~Testado: pré-popular `backup_ram` com "BACKUP RAM OK !" (os 16 bytes
      exatos que a tabela em `$C125B8` espera)~~ — **não resolveu**. Com a
      Backup RAM "pré-formatada", o boot toma um caminho ligeiramente
      diferente logo no início (confirmado, frame 1 já mostra PC diferente),
      mas CONVERGE pro mesmo travamento em `0xC05BE2` mais adiante. Ou seja,
      `0xC05BE2` não é exclusivo do caminho de formatação — é alcançado dos
      dois jeitos. A hipótese "backup RAM em branco causa isso" está
      **descartada**.
- [x] ~~Testado: será que só precisa de mais tempo (como o loop do
      LSPCMODE)?~~ — **não**. Rodamos até 5000 frames (~1 bilhão de ciclos,
      bem mais que os ~9s emulados que o MAME levou pra chegar no menu) e
      o PC nunca sai de `0xC05BE2`/`0xC09712`. É um loop genuinamente
      infinito, não uma espera longa.
- [ ] **Estado real**: não sabemos por que o MAME nunca visita `0xC05BE2`
      nem `0xC0581C` em ~9s emulados, mas nosso caminho (idêntico ao dele
      por centenas de milhares de instruções, confirmado) chega lá e trava
      pra sempre. A causa exata ainda não foi encontrada.

### Tentativa com NVRAM real do MAME + BIOS `sp-s2.sp1`/euro
`debug_boot.c` e `trace_boot.c` ganharam suporte a carregar a Backup RAM
inteira de um arquivo via a variável de ambiente `NEOGEO_BACKUP_RAM_FILE`
(com correção de byte-swap automática) — útil pra testar hipóteses, ex:
```
NEOGEO_BACKUP_RAM_FILE="third_party/mame/nvram/doubledr/saveram" ./debug_boot.exe <cart> <bios> [frames]
```
Usamos o `saveram` real que o MAME salvou (64KB, não só os 16 bytes da
assinatura). Resultado: o caminho de boot muda visivelmente (mais dados de
paleta escritos, confirmando que a Backup RAM importa), mas AINDA trava —
num trap idêntico (`kick watchdog + BRA` incondicional), só que em
`0xC16BAA` (BIOS `sp-s2.sp1`) em vez de `0xC05BE2` (Universe BIOS). Ou seja,
a Backup RAM era **uma das causas, não a única** — existe pelo menos mais
um registrador/condição faltando.

Tentamos repetir a comparação trace-a-trace com a BIOS `sp-s2.sp1` (MAME
`-bios euro`, já que `sp-s2.sp1` está associado ao bios "euro" no XML do
MAME — confirmado via `-listxml neogeo`), mas os endereços de checkpoint que
funcionavam pra Universe BIOS (`C1BACA`, `C05798`, etc.) são específicos
daquele binário — **não existem** no binário `sp-s2.sp1` (são BIOS
diferentes, código diferente). Precisa remapear os checkpoints do zero pra
essa BIOS especificamente (achamos que `C1313C` e `C130AA` batem, mas ainda
não characterizamos o suficiente pra achar a divergência real).

### Teste conclusivo: o travamento NÃO é relacionado ao cartucho
Rodamos `debug_boot` apontando pra um "cartucho" sintético (`roms/cart_empty/`,
só um `p1.bin` de 4 bytes, sem C/S/M/V ROMs de verdade) contra a BIOS
`sp-s2.sp1`. Resultado: **travamento idêntico, byte a byte** — mesmo PC final
(`0xC16BAA`), mesmos valores de paleta, mesmo fix map, comparado com o
Double Dragon real. Isso prova que a causa raiz está inteiramente dentro da
BIOS (provavelmente no autoteste de hardware — VRAM, RTC completo, ou o
checksum), **antes** dela sequer tentar acessar o cartucho. Descarta de vez
bankswitch/C-ROM/S-ROM como suspeitos.

### Achado #6: bug real no auto-incremento de VRAMRW (RESOLVIDO)
Usando o desmontador do próprio MAME (`dasm` no debugger, muito mais confiável
que decodificar hex à mão), rastreamos `0xC16ADA` até sua causa real: é o
handler de ERRO do teste de VRAM da BIOS (`0xC130AA`, chamado 2x — uma vez pra
VRAM baixa $0000-$7FFF, outra pra alta $8000-$85FF, exatamente como a wiki
descreve). A rotina escreve `$5555`/`$AAAA` em cada palavra via
VRAMADDR/VRAMRW/VRAMMOD, depois lê de volta E reescreve o mesmo valor (mesma
posição) pra verificar — usando a MESMA porta VRAMRW pras duas operações.

**O bug**: nosso `vram_rw_read` auto-incrementava `vram_addr` (igual
`vram_rw_write`), mas a wiki só documenta o auto-incremento "depois de uma
ESCRITA". Com leitura TAMBÉM incrementando, o endereço avançava 2x mais
rápido que o esperado durante a fase de verificação (leitura+escrita por
iteração = 2 incrementos, contra 1 na fase de escrita), fazendo a
comparação bater com a posição errada da VRAM e falhar sempre. Corrigido
removendo o incremento de `vram_rw_read` em `core/src/system/neogeo.c`.

Confirmado com trace: depois do fix, o teste de VRAM passa (não pulamos mais
pra `0xC16ADA` como erro) e, com o pulso do RTC também correto, o teste de
RTC (`0xC11D94`, detecção de borda de subida no bit 6 de STATUS_A) também
passa. **Mas ainda travamos em `0xC16BAA`** — mais um bloqueio adiante, ainda
não identificado. Progresso real, mas o boot completo ainda não foi
alcançado.

### Achado #7: Memory Card nunca foi implementada (RESOLVIDO)
Mesma metodologia (dasm do MAME): depois do fix de VRAM/RTC, o próximo salto
pra `0xC16ADA` vinha de outro teste de memória, dessa vez em `$800000`.
Confirmado via busca web: **`$800000-$800FFF` é o Memory Card (2KB, 8-bit)**
— região que nunca tínhamos mapeado (caía em open-bus). Implementado como
RAM simples de 2KB, endereçada por `(addr>>1)` já que só o byte baixo de
cada word é conectado ao cartão real (o teste da BIOS só confere via
`cmp.b`, consistente com isso). Ver `is_memcard_addr`/`memcard_index` em
`neogeo.c`.

### Achado #8: bloqueio novo — handshake de comunicação com o Z80 (NÃO RESOLVIDO)
Depois do fix da memory card, avançamos mais — mas ainda caímos no mesmo
`0xC16ADA`/`0xC16BAA`. Desta vez a causa é diferente de tudo anterior: o
68000 escreve o comando `1` em `REG_SOUND` ($320000, dispara NMI no Z80),
depois faz polling em `REG_SOUND` esperando o Z80 ecoar `1` de volta, com
timeout de 50000 tentativas (`0xC12012-0xC12038`). Como nunca recebemos a
resposta certa, dá timeout e cai no erro.

Isso é esperado dado o estado atual: nosso Z80 mapeia só os primeiros 32KB
do M1 (128KB) fixos em 0x8000-0xFFFF, sem o bankswitch real (TODO desde a
Fase 3) — se o driver de som real precisa de código além desses 32KB, o Z80
está executando lixo. Também não implementamos o YM2610 (Fase 5) nem as
portas de I/O reais que o driver pode checar antes de responder.

**Isso desloca o foco da investigação**: os próximos 8 achados foram todos
do lado 68000/sistema; agora o bloqueio é do lado Z80/som — uma frente
diferente (Fase 2b refinamento + Fase 3b bankswitch de som + possivelmente
parte da Fase 5). Provavelmente vale desmontar o M1 ROM real (mesma técnica
`dasm` do MAME, mas com CPU "soundcpu" em vez de "maincpu") pra entender o
que o driver espera antes de ecoar a resposta.

### Achado #9: mapa de memória do Z80 usava a ROM ERRADA (RESOLVIDO — grande)
Ao comparar trace-a-trace (nova ferramenta `core/tools/trace_z80.c`, usa
`Z80_Run(z,1)` pra andar 1 instrução por vez) contra o `dasm` do MAME na
`audiocpu`, descobrimos que nosso Z80 executava puro lixo (RAM zerada) nos
primeiros bytes — porque **o mapa de memória do Z80 usava a M1 do cartucho
em 0x0000-0x7FFF**, mas o hardware real usa uma **"Audio BIOS" compartilhada
(`sm1.sm1`)** ali — exatamente o mesmo padrão do 68000 (BIOS fixa +
cartucho via bankswitch), só que ninguém tinha percebido isso pro lado Z80.
Confirmado byte a byte: o conteúdo que o MAME executa em 0x0000-0x001F bate
EXATAMENTE com `sm1.sm1`, não com `082-m1.rom`.

Corrigido: `neogeo_romset_t` ganhou `audiobios`/`audiobios_size` (carregado
de `sm1.sm1` no `rom_loader.c`, sem byte-swap — confirmado no source do
MAME, `ROM_LOAD` simples, sem `WORD_SWAP`), e o mapa do Z80 agora usa:
- `0x0000-0x7FFF`: Audio BIOS (fixa, compartilhada)
- `0x8000-0xF7FF`: janelas de bankswitch (TODO: real, por ora M1 do
  cartucho mapeada direto, banco 0 sempre — mesma abordagem provisória do
  P ROM antes do bankswitch real)
- `0xF800-0xFFFF`: Work RAM (2KB)

Depois do fix, o Z80 executa em **lockstep com o MAME** processando o
comando 1 do 68k de verdade (pula pro handler certo, reinicializa o driver
de som, chega até um loop de espera auto-modificado em `$FFFD` que é
LEGÍTIMO — o próprio driver escreve `JP $FFFD` ali de propósito como
"parque" até a próxima NMI, não é bug).

### Ainda não resolvido: eco de volta pro 68k
O 68k espera ler `1` de volta em `REG_SOUND` depois de mandar o comando `1`
(timeout de 50000 tentativas). Rastreamos toda a execução do Z80 até ele
"estacionar" no loop auto-modificado e `sound_latch_to_68k` NUNCA mudou de
0 pra 1 nesse caminho. Hipóteses: (a) o eco acontece bem mais fundo na
rotina de reinicialização que ainda não desmontamos, (b) precisa do
bankswitch real da M1 do cartucho (código específico do jogo que só existe
lá), ou (c) mal-entendido nosso sobre a condição de sucesso do lado 68k.

### Achado #10: porta de resposta pro 68k estava errada (RESOLVIDO)
Confirmado contra a NeoGeo Development Wiki ("Z80 port map"): a resposta pro
68k vai na **porta $0C** (escrita), não na porta $00 como eu tinha
implementado (só tinha visto $00 documentado pro lado de LEITURA — pegar o
comando —, e assumi por engano que era bidirecional). Corrigido em
`z80_io_write_cb`. Resultado imediato: `sound_latch_to_68k` finalmente vira
`1` (confirmado com `trace_z80`), e o timeout de 50000 tentativas do 68k
não trava mais — **o boot avança muito além do ponto onde ficava preso
desde o início desta investigação**.

### Achado #11: bankswitch real da M1 implementado (RESOLVIDO)
Confirmado contra a mesma página da wiki: portas $08-$0B (leitura) escolhem
o banco de cada uma das 4 janelas ($F000/$E000/$C000/$8000), usando um
truque conhecido do Z80 — em `IN A,(n)`, o valor ATUAL do acumulador A fica
no barramento de endereço durante o ciclo de leitura, então o código faz
`LD A,banco` seguido de `IN A,(porta)`, e o hardware usa esse A como número
do banco (o dado retornado pela porta não importa). Como nosso core chama a
callback de I/O ANTES de sobrescrever A com o resultado lido
(`SET_A(io_rd(z,port))` em `z80.c`), dava pra implementar isso sem mudar a
assinatura da callback — só lendo `ng->z80.af>>8` dentro dela. Implementado
em `z80_io_read_cb`/`z80_bank_offset[4]`/`z80_read_cb`.

### Estado atual: boot avança muito mais, novo ponto de investigação
Depois dos achados #10/#11, o boot deixou de travar no loop antigo
(`C16BAA`) e agora navega por endereços variados por milhares de frames —
VRAM com conteúdo real e variado (não mais um preenchimento uniforme),
paleta com cores reais, e a tela (ver screenshot do frontend) mostra um
padrão estruturado tipo grade, bem diferente do texto esparso de antes.

Acabou presa num novo polling em `0xC18714` (`TST.B $10FE8C` / `BNE
$c18714` — espera um byte de work RAM virar zero) depois de ~3000 frames.
Notável: `SR` agora mostra máscara de interrupção **0** (antes sempre 7) —
interrupções estão habilitadas de verdade pela primeira vez nesta
investigação. O byte esperado provavelmente é limpo por uma rotina de
interrupção (vblank ou timer) que ainda não estamos disparando/processando
do jeito certo, ou por um mecanismo de DMA/vídeo que ainda não implementamos.

### Investigação do polling em 0xC18714 (aprofundada, não resolvida)
Achamos a ISR de vblank real (vetor nível 1 → `$C00438` → `$C11646`).
Ela só faz o trabalho de verdade (que inclui `clr.b $10FE8C`, a flag que
nosso loop espera virar zero) se `$10FEE3==0` — confirmamos que bate
(`wram[0xFEE3]=00`). O contador que a ISR incrementa SEMPRE (mesmo se pular
o trabalho), `$10FEE4`, sobe a cada frame (`0x40` em 300 frames, `0xCD` em
3000) — ou seja, **a interrupção está disparando e sendo atendida
periodicamente, de verdade**. Mas `$10FE8C` nunca vira zero mesmo assim.

Isso é intrigante: se a condição do gate bate e a ISR roda, o `clr.b` que
zeraria a flag deveria executar toda vez. Hipótese mais provável agora:
um bug de pilha na nossa exceção (RTE/autovetorada) faz o `RTS` de dentro
da sub-rotina chamada pela ISR (`jsr $c1871e`) retornar pro lugar errado,
"escapando" da ISR antes de chegar no `clr.b`. Não confirmamos isso ainda —
precisaria de outro trace-a-trace focado especificamente na pilha durante
o atendimento da interrupção (comparar valores de A7 entre nosso core e o
MAME logo após a IRQ ser reconhecida).

### Atualização: o loop C18714 NÃO é infinito no hardware real (achado importante)
Nova ferramenta `core/tools/debug_vblank.c` (roda em fatias de 1 scanline,
loga toda entrada na ISR e toda mudança de `wram[0xFE8C]`) revelou que a
flag SIM alterna entre 0 e 1 continuamente — meu diagnóstico anterior
("nunca zera") estava errado, era só uma coincidência da amostragem
(1x por frame batendo sempre na mesma fase do ciclo). O sistema está de
fato ciclando repetidamente por um loop MAIOR (`$C17E4C` chama o wait
`$C1870C` repetidamente), não travado numa única instrução.

Comparado contra o trace mais longo do MAME (`trace_euro2.log`, 59s reais):
o MAME visita esse MESMO loop só até a linha 259128 de 10.7 milhões — bem
menos de 1% do trace —, depois escapa **de vez** via um dispatch baseado
num índice de estado (`wram[0x9D8]`) e nunca mais volta, indo pro código
real de inicialização do jogo. Ou seja: **esse loop tem duração finita no
hardware real** (provavelmente a animação do logo da BIOS), e nosso
emulador nunca satisfaz a condição de saída, ficando preso nele por
"sempre" (testamos até 2 milhões de scanlines, ~126s emulados, bem além
dos ~26s que o MAME levou pra escapar).

Localizamos as 3 condições de saída do loop (`$C17E6C-$C17E94`) e seus
valores atuais no nosso emulador:
- Condição 1: `wram[0x10FD82]!=0` (bate: `=0x80`) **E**
  `backup_ram[$D00034..35]!=0` (não bate: `=0000` — mas confirmamos que o
  próprio save real do MAME também tem esse valor zerado aqui, então não é
  bug nosso, essa condição também falharia no hardware real neste estado)
- Condição 2: nibble alto de `wram[0x10FBEB]!=0` (não bate: `=00`)
- Condição 3: bit0 de `wram[0x10FDAD]==1` (não bate: `=00`)

Nenhuma bate. Além disso, `wram[0x9D8]` (índice de um "estado" que escolhe
qual sub-rotina rodar via jump table em `$C17EC0`) **nunca sai de 0**
mesmo depois de milhares de chamadas — sugerindo que o handler do estado 0
tem sua própria condição interna (ainda não localizada) que nunca é
satisfeita, e é ela quem deveria eventualmente setar uma das 3 flags acima
(ou avançar `wram[0x9D8]` direto).

- [x] ~~Localizar e desmontar o handler do "estado 0"~~ — feito: é
      `$C17F0E`, e ele seta `wram[0x9D8]=1` **incondicionalmente** antes de
      retornar (não devia nunca ficar "preso" no estado 0). Ou seja, se o
      dispatch (`$C17EC4`, jump table indexado por `wram[0x9D8]`) fosse
      alcançado nem que fosse uma vez, o estado teria avançado.
- [x] Construímos `core/tools/debug_bp.c` — breakpoint de verdade (passo a
      passo com `m68k_core_step`, sem perder nenhuma instrução, ao
      contrário da amostragem por intervalo das ferramentas anteriores).
      `C17EC4`/`C17F0E` (do loop `C17E4C`) realmente nunca são alcançados
      em 30 milhões de instruções — MAS descobrimos que esse não é sequer
      o loop certo: **existe outro loop de espera bem parecido, em
      `$C17140-$C1715E`**, usando a MESMA variável de estado (`(A5+$9D8)`)
      só que com sua própria tabela de dispatch em `$C17160`. É ESSE que
      estamos executando de verdade.

### Loop real identificado: `$C17140`, preso no "estado 1" (`$C1724C`)
Descoberto um bug na PRÓPRIA ferramenta de diagnóstico: os endereços tipo
`($xxx,A5)` no disassembly do MAME são RELATIVOS a A5 (=`$10F300` na
prática), não absolutos — eu estava lendo o offset como se fosse o
endereço direto. Corrigido em `debug_vblank.c`.

Com a leitura corrigida, confirmamos com precisão:
- `wram[A5+$9D8] = 1` → estado 1, dispatch vai pro handler em `$C1724C`
- `$C1724C: btst #0,($bdd,A5) / beq $c1725a` → só avança pro estado 2 se o
  **bit 0 de `wram[$10FEDD]` (A5+$BDD) estiver setado**. Está sempre `00`.

Tentamos achar quem deveria setar esse bit via watchpoint de escrita no
MAME (`wpset program,0x10fedd,1,w,...`) rodando até 30s reais (~12s
emulados) — **zero escritas capturadas nesse intervalo**, então ou demora
mais que isso pra acontecer no hardware real, ou o watchpoint precisa
rodar por mais tempo ainda. Não confirmamos o mecanismo exato.

- [x] Rodamos o watchpoint por 90s reais (~15 milhões de linhas de trace) —
      **zero disparos**, mesmo pra um endereço que sabemos ser escrito o
      tempo todo (`$10FE8C`, confirmado via `debug_vblank`). Conclusão: o
      mecanismo `wpset`+`tracelog` do MAME não está funcionando do jeito
      que esperávamos nesta configuração — não é confiável, abandonamos
      essa abordagem.
- [x] Achamos os 2 locais que ESCREVEM em `($bdd,A5)` via busca direta no
      disassembly (`C12AD0`: sempre escreve 0; `C185AE`: escreve D3, vindo
      de uma rotina de debounce de input em `$C1862E`/`$C18656` operando
      sobre estruturas em `($a94,A5)`/`($a9a,A5)`). Essas estruturas só são
      inicializadas por `$C18472` (chamada só uma vez, bem cedo no boot,
      via `$C1113E→$C18462`) — confirmado que o MAME chama isso na linha
      53539 do trace real. **Nosso emulador nunca chama `$C18472`** (zero
      hits em 30 milhões de instruções com breakpoint de verdade), ou seja
      a causa raiz está ainda mais cedo no boot do que tudo que já
      investigamos.

### Achado #12/#13: mais 2 registradores nunca implementados (RESOLVIDO)
Rastreando pra trás a partir de `$C18472`, achamos o código que decide se
pula o debounce (lê `$300001`, testa bit2) e, próximo dali, outro registro
nunca implementado: **`$300081` (REG_SYSTYPE** — bit7=botão de test,
bit6=tipo de cabinet 2/4/6 slots, confirmado na wiki). Implementamos os
dois (`NEOGEO_DIPSW_DEFAULT`, `NEOGEO_SYSTYPE_DEFAULT` em `neogeo.c`).

### Anomalia encontrada: `debug_boot` e `trace_boot` divergem entre si (não resolvida)
Depois dos fixes acima, `trace_boot` (30 milhões de instruções) passa pela
checagem de RTC e cai no **mesmo trap de timeout do handshake Z80** que já
tínhamos "resolvido" antes (`C12038→C16ADA`, comando enviado ao Z80 nunca
ecoado) — mas dessa vez para um comando/contexto diferente do original.
Já `debug_boot` (usa a MESMA lógica de step/scheduler, só que via
`neogeo_run_slice`), rodado por **50000 frames = 10 bilhões de ciclos**
(muito mais tempo emulado que os 30 milhões de instruções do
`trace_boot`), **nunca** sai do loop `C18714` nem chega perto de
`C16ADA`. As duas ferramentas usam código de scheduling estruturalmente
idêntico (`m68k_core_step` por instrução, mesmo racionamento de Z80,
mesmo gatilho de vblank/RTC) — não deveriam divergir. Não descobrimos a
causa dessa discrepância ainda; é uma pista nova e genuína, não só "precisa
de mais tempo".

### Achado #14: máquina de estados do boot travava pra sempre esperando uma borda no bit 0 de REG_STATUS_B (RESOLVIDO PARCIALMENTE)
Ferramenta nova: `core/tools/debug_checkpoints.c` — roda `neogeo_run_slice` até
contagens fixas de vblank e imprime `wram[$FCD8/$FCD9]` (índice de uma
máquina de estados do boot em `$C17150`, relativo a A5=`$10F300`) junto com
alguns flags relacionados. Comparamos linha a linha contra dumps de WRAM
reais tirados do MAME **rodando explicitamente com `-bios euro`** (comando
`save arquivo.bin,100000,10000` no debugger do MAME, depois de N `gvblank`
— **muito** mais rápido e confiável que `trace`/`print` pra esse tipo de
amostragem, já que a saída de `print`/`tracelog` não é capturável via
stdout neste MAME/ambiente).

Resultado: até o frame 220 nosso emulador bate 100% com o MAME real (mesmo
padrão de teste de RAM 0x5555/0xAAAA, mesmo teste de endereço). No frame
~240 ambos entram no "estado 1" da máquina de estados (`$C1724C`). A
PARTIR DAÍ diverge: o MAME real avança pros estados 2,3,4,5... e chega no
estado 7 por volta do frame 500 (título deve aparecer por volta do frame
~1500-1800, confirmado com screenshot real do MAME em 30s = tela do
Double Dragon); o nosso ficava **travado no estado 1 para sempre** (testado
até 20000 frames — tela nunca sai de um grid estático de auto-teste).

Causa raiz: o código em `$C1850E` lê `REG_STATUS_B` ($380000) duas vezes
por frame, inverte os bits e computa `(anterior XOR atual) AND atual` —
uma técnica de debounce/detecção de borda de subida, igual à usada pros
botões Start/Select (bits 1-4). O estado 1 (e o estado 3, mais adiante)
ficam num loop testando `btst #0` desse resultado, ou seja, esperando uma
borda especificamente no **bit 0** de STATUS_B — bit esse que a NeoGeo
Development Wiki documenta como `?` (não confirmado/não mapeado a nenhum
botão real). No hardware real (e no MAME), esse bit muda algumas vezes a
cada ~10-30 frames durante o boot; no nosso emulador ele era 100%
constante (`NEOGEO_STATUS_B_DEFAULT`), então a borda nunca acontecia e o
estado 1 travava para sempre.

Fix (`neogeo_status_b()` em `core/src/system/neogeo.c`): como não dá pra
saber a fonte elétrica exata desse bit "?" (provavelmente um pino
flutuante/não conectado, e o jogo usa a borda dele como uma espécie de
"espera pseudo-aleatória"), implementamos ruído determinístico derivado de
`master_cycles` (hash tipo Knuth, sem precisar de estado novo no struct)
só nesse bit, mantendo os outros bits de STATUS_B (memcard, Start/Select
reais) estáveis. **Resultado imediato**: a máquina de estados agora avança
de verdade — vimos índices 3, 5, 8, 9, 10 em vez de travar em 1 pra sempre
(`debug_checkpoints` até 2000 frames). A tela mudou de um grid estático
pra texto/glifos reconhecíveis espalhados pela tela (fix layer desenhando
algo de verdade), mas ainda NÃO forma a tela de título coerente que o MAME
mostra — o ruído não é bit-exato ao hardware real, então algum outro
timing sensível a esse bit provavelmente ainda diverge (posições de
sprite/texto parecem aleatórias, não alinhadas).

- [x] Ruído do bit 0 ajustado pra mudar só a cada ~10-20 frames (shift de
      `master_cycles` aumentado de `>>10` pra `>>21`) — a primeira versão
      mudava a cada ~1024 ciclos, então as DUAS leituras de STATUS_B que o
      jogo faz por frame (com um delay pequeno entre elas) quase sempre
      viam valores diferentes, gerando uma borda todo frame em vez de a
      cada 10-30 como no hardware real.

### Achado #15: fix layer usava ordem de endereço ERRADA (row-major em vez de column-major) — RESOLVIDO
Depois do Achado #14 destravar o boot, a tela passou a mostrar
texto/glifos reconhecíveis mas ESPALHADOS pela tela em vez de formar uma
mensagem coerente (screenshot comparado com o usuário, que confirmou "tá
meio misturado"). Suspeita: conteúdo do tile certo, POSIÇÃO errada.

Conferimos a página "Displaying text" da NeoGeo Development Wiki, que traz
um exemplo de código calculando o endereço VRAM de um tile do fix layer
como `FIXMAP + Y + ((X+1)*32)` — ou seja, `map_idx = coluna*32 + linha`
(**column-major**: 32 words alocados por coluna, só 28 visíveis), NÃO
`linha*40+coluna` (row-major, como um framebuffer raster comum) que
tínhamos implementado em `core/src/video/lspc_fix.c`. Com a fórmula
errada, cada tile continuava sendo DECODIFICADO certo (por isso os
glifos eram reconhecíveis), mas caía na CÉLULA errada da tela — exatamente
o sintoma visto.

Fix: `map_idx = FIX_MAP_BASE + col * 32u + row` em
`neogeo_render_fix_layer()`. Os 5 testes continuam passando (o teste de
vídeo só usa a posição (linha 0, coluna 0), que é índice 0 nas duas
convenções, então não pegava esse bug — ponto fraco de cobertura, ver
pendências abaixo).

- [ ] Adicionar um teste que escreva um tile em (linha>0, coluna>0) e
      confirme a posição de pixel certa — o teste atual não detectaria uma
      regressão nessa ordem de endereço (só testa índice 0)

### Achado #16: bitplanes do tile de sprite (C ROM) vinham AGRUPADOS em vez de INTERCALADOS por linha — RESOLVIDO
O usuário reparou, olhando o screenshot do Achado #15, que "as letras dos
sprites estão quase se encaixando" — glifos reconhecíveis mas com um efeito
de eco/sombra deslocada, como se cada caractere estivesse desenhado duas
vezes ligeiramente desalinhado.

Conferimos a página "Sprite graphics format" da NeoGeo Development Wiki:
dentro de cada bloco 8x8 (dos 4 que formam um tile 16x16), o layout de
bytes por linha é `[linha0,plano_baixo], [linha0,plano_alto],
[linha1,plano_baixo], [linha1,plano_alto], ...` — ou seja, os 2 bitplanos
de uma ROM (odd: planos 0-1; even: planos 2-3) vêm **intercalados por
linha** (2 bytes consecutivos por linha). Tínhamos assumido o oposto: 8
bytes de plano baixo (linhas 0-7) seguidos de 8 bytes de plano alto
(também linhas 0-7) — um agrupamento por PLANO, não por LINHA. Com a
suposição errada, cada tile lia o byte do plano alto de uma linha como se
fosse o plano baixo de OUTRA linha (e vice-versa) — visualmente, metade
dos bits de cor de cada tile saíam da linha errada, dando exatamente o
efeito de "eco"/glifo duplicado e quase-alinhado que o usuário notou.

Fix em `sprite_tile_pixel()` (`core/src/video/lspc_sprites.c`):
`low_off = block*16 + ly*2` (era `block*16 + ly`), `high_off = low_off+1`
(era `block*16+8+ly`). `test_sprites.c` tinha construído seu tile sintético
assumindo o layout errado (agrupado); atualizado pra intercalado (byte
par=plano baixo, ímpar=plano alto, dentro do tile inteiro — a aritmética
faz isso valer pra qualquer bloco). Os 5 testes voltam a passar depois do
ajuste.

- [x] Confirmado visualmente: reconstruído e checado — o efeito de eco NÃO
      sumiu com o fix #16 sozinho (ver Achado #17 abaixo: a mensagem
      "BACK-UP RAM CLEAR" é desenhada pela FIX LAYER, não por sprites — o
      fix #16 estava correto mas não afetava essa tela).

### Achado #17: fix layer também tinha bitplanes/nibbles na ordem errada — RESOLVIDO (era a causa real do "eco")
Depois do Achado #16, gerei um screenshot headless novo (ferramenta nova
`core/tools/debug_screenshot.c` — renderiza direto com `neogeo_render_frame`
e salva BMP sem depender do frontend SDL2, que estava MUITO lento/instável
pra rodar milhares de frames neste ambiente — mais um achado incidental,
ver pendências) e o eco continuava IDÊNTICO na tela "BACK-UP RAM CLEAR".
Isso prova que esse texto é desenhado pela FIX LAYER (`lspc_fix.c`), não
por sprites — o fix #16 estava certo mas não tinha nada a ver com essa tela.

A wiki "Fix graphics format" só dá resumos vagos/ambíguos (a ferramenta de
fetch usada nesta sessão só retorna paráfrases de IA, nunca o texto
original completo, então não dava pra confiar 100%). Em vez de adivinhar
de novo, buscamos o `charlayout` REAL usado pelo driver `neogeo.cpp` do
MAME pra decodificar fix tiles (fonte primária, dado bruto, não resumo):
`xoffset` (em nibbles) = `{33,32,49,48,1,0,17,16}` pros 8 pixels x=0..7,
`yoffset` = múltiplos de 8 bits (1 byte por linha). Isso dá dois fatos que
CONTRADIZIAM nossa implementação (baseada no resumo da wiki):
1. A ordem de armazenamento das 4 colunas de par-de-pixel dentro do tile é
   x{4,5}, x{6,7}, x{0,1}, x{2,3} — a METADE DIREITA (x4-7) vem ANTES da
   esquerda no endereço (confirma o "H:half" da wiki, mas a wiki resumida
   não deixava claro qual half vem primeiro).
2. Dentro de cada par, o pixel de X PAR fica no NIBBLE ALTO do byte, o
   ÍMPAR no baixo — o OPOSTO do que o resumo da wiki tinha sugerido (e do
   que tínhamos implementado: nibble par=baixo, ímpar=alto).

Fix em `fix_tile_nibble()` (`core/src/video/lspc_fix.c`): recalculado
`byte_col`/`byte_off` conforme a ordem real, e invertida a seleção
alto/baixo do nibble. `test_video.c` usa um tile com os dois nibbles iguais
(0x11 em todo byte), então não pegava esse bug — não precisou de ajuste.
Os 5 testes continuam passando. **Resultado confirmado por screenshot**:
o texto "BACK-UP RAM CLEAR" / "PRESS A,B,C BUTTONS AT ONE TIME" agora
aparece limpo e legível, sem nenhum eco/duplicação.

- [x] Investigado o "travamento" aparente além de ~15000-20000 vblanks:
      **falso alarme**. `core/tools/debug_checkpoints.c` ganhou timing por
      checkpoint (`clock()`) e confirmou taxa ESTÁVEL de ~9ms/vblank do
      início ao fim (sem nenhum salto/cliff) — o que parecia "travado" era
      só várias ferramentas rodando ao mesmo tempo brigando por CPU, ou
      impaciência (esperar menos que o necessário pra 20000+ vblanks reais
      levarem ~3min). O frontend SDL2 continua não investigado a parte
      (pode ter overhead próprio de vsync/janela), mas o CORE em si escala
      normalmente. Preferir `debug_screenshot` (headless) pra iteração.
- [x] Confirmado: avançando mais (9000 vblanks com `debug_screenshot`),
      chegamos na tela de TESTE DE COR da BIOS (self-test/diagnóstico),
      com rótulos "RED"/"GREEN"/"BLUE"/"WHITE" e uma caixa colorida abaixo
      de cada um — prova que o boot passou de verdade da tela de aviso de
      backup RAM. Só que as CORES das caixas não batem com os rótulos
      (ex.: caixa de "RED" saiu verde) — indica um bug na decodificação da
      paleta (`neogeo_color_to_rgb888` em `lspc_fix.c`) ou em como a BIOS
      escreve as entradas de teste, ainda não investigado.
### Achado #18: formato da word de paleta estava totalmente errado — RESOLVIDO
Tínhamos assumido 3 campos de 5 bits contíguos (bit15=dark, bits14-10=R,
bits9-5=G, bits4-0=B). O formato real (wiki "Colors"): `CrgbRRRRGGGGBBBB`
— bit15=C(dark/comum), bit14=r(LSB do vermelho), bit13=g(LSB do verde),
bit12=b(LSB do azul), bits11-8=RRRR, bits7-4=GGGG, bits3-0=BBBB — o LSB de
cada componente fica isolado no topo da word, longe dos outros 4 bits do
mesmo componente. Fix em `neogeo_color_to_rgb888()` (`lspc_fix.c`);
`test_video.c`/`test_sprites.c` tinham palavras de paleta construídas no
formato errado, atualizadas. Os 5 testes passam.

**Confirmado por screenshot**: a tela de teste de cor da BIOS (rótulos
RED/GREEN/BLUE/WHITE com caixa colorida abaixo) agora mostra as cores
CERTAS pros rótulos certos (antes: "RED" saía verde, etc). Essa tela por
si só é uma confirmação forte de que o boot está bem avançado — é uma
tela de diagnóstico que só aparece depois de passar por vários
subsistemas da BIOS.

- [ ] Investigar o uso real do bit "dark"/comum (bit15) — a wiki menciona
      que é "ignorado" no cálculo normal de RGB555 (usado só pra efeito de
      sombra de sprite?), mas mantemos a aproximação antiga (metade do
      brilho) sem confirmação

### Achado #19: input real (REG_P1CNT) nunca chega no gate da tela "BACKUP CLEAR" (EM ANDAMENTO, não resolvido)
Pedido do usuário: avançar até ver o logo NEO-GEO. Confirmamos primeiro
que o "travamento" reportado em vblanks 20000-30000 era falso alarme —
`debug_checkpoints` (agora com timing por checkpoint via `clock()`) provou
taxa estável de ~7-9ms/vblank do início ao fim, sem nenhum salto real;
rodar em `run_in_background` deixava as coisas artificialmente mais lentas
(provavelmente prioridade de CPU menor), gerando falsos alarmes de
travamento. Corrigido/documentado — usar sempre foreground pra medir.

Avançando de verdade (headless, `debug_screenshot`, novo: renderiza 1
frame e salva BMP sem depender do SDL2), confirmamos MUITO mais telas
reais e corretas da BIOS em sequência: aviso "BACKUP CLEAR" (~4500vb) →
teste de cor RED/GREEN/BLUE/WHITE (~9000vb, cores certas depois do Achado
#18) → ajuste de calendário/RTC (~12000vb) → teste de som com cursor
(~15000vb) → teste de memory card "INSERT MEMORY CARD" (~20000vb) → de
volta pro aviso "BACKUP CLEAR" (~30000vb) — confirma que é um CICLO real
de self-test da BIOS (esperado quando o backup RAM não é validado), não
um bug de boot.

Pra sair desse ciclo de vez (e chegar no logo/attract mode), a BIOS exige
pressionar A+B+C simultaneamente na tela de aviso. Implementamos
`neogeo_t.p1_buttons` (REG_P1CNT injetável, default 0xFF) e um parâmetro
`press_from_vblank` no `debug_screenshot`. Achamos via disassembly real
(MAME, `-bios euro`) o código exato do gate, em `$C17AD8` (estado 11 da
máquina de estados de boot):
```
C17AD8: btst #0,(bdd,A5)             ; se setado, pula pro proximo estado direto
C17AE0: move.b (a96,A5),D0; andi #$70; cmpi #$70; bne c17b0e
        ; ^ entra no modo "mostrar aviso + esperar" SO SE (a96,A5)==0x70
        ;   (0x70 = bits4-6 todos 1 = A,B,C todos SOLTOS — condicao de entrada)
C17AFA: jsr c1870c (espera 1 vblank)
C17AFE: move.b (a95,A5),D0; andi #$70; bne c17afa
        ; ^ sai do loop quando (a95,A5)&0x70==0 (A,B,C todos PRESSIONADOS)
C17B08: move.w #$c,(9d8,A5)          ; avanca pro estado 12
```
Confirmamos com `wram[$10F395]`/`wram[$10F396]` forçados (0x00 e 0x70
respectivamente, na ordem certa) que o PC realmente passa por `$C17AFE`
com os valores esperados — mas mesmo assim o boot **não avançou de forma
estável**: rodando bem mais frames depois, o índice de estado (`idx`)
volta a mostrar 11 e a tela volta a mostrar a MESMA mensagem "BACKUP
CLEAR". Duas hipóteses não resolvidas: (a) avançou e completou uma volta
inteira do ciclo de self-test, voltando "naturalmente" pro mesmo estado
(já que forçamos os endereços WRAM e nada mais os sobrescreve depois, eles
ficam "grudados" nos valores que forçamos, então uma re-entrada no estado
11 seria satisfeita trivialmente de novo); (b) não avançou de verdade e o
`bne` não se comportou como o esperado por algum motivo não identificado
(possível bug de flags no core `m68k_core` portado, ou os endereços
`(a95,A5)`/`(a96,A5)` NÃO SÃO o que a REG_P1CNT real preenche — nunca
achamos no disassembly quem ESCREVE nesses dois endereços especificamente,
só quem LÊ.

- [ ] Achar quem ESCREVE em `wram[$10F395]`/`$10F396` (buscar em regiões de
      código ainda não disassembladas, ou usar breakpoint de escrita real
      no MAME) — vai esclarecer se é derivado de REG_P1CNT/REG_P2CNT ou de
      outra fonte, e por que nosso `p1_buttons` não propaga até lá
      naturalmente mesmo com o pipeline de input (`C1850E`) parecendo
      correto depois dos Achados #16/#17
- [ ] Fazer uma injeção de botão mais realista: 1 frame solto, depois N
      frames pressionado, depois solto de novo (edge real), em vez de
      forçar WRAM direto — mais fiel ao hardware e evita o problema de
      "valor grudado" que pode estar causando o comportamento observado
- [ ] Meta pendente do usuário: ver o logo NEO-GEO / tela de título real.
      Ainda não alcançado nesta sessão — próximo passo natural pra
      próxima sessão, com um depurador mais robusto (Achado #12/#13 já
      sugeria isso) ou testando as hipóteses acima.

**Atualização (mesma sessão, continuando a investigação):**
1. Confirmamos que a ASSINATURA de backup RAM ("BACKUP RAM OK !" em
   `$D00010`, 16 bytes, comparada contra a ROM em `$C125B8` via
   `cmpm.b` em `$C125D0-C125E4`) bate **byte a byte, perfeitamente**,
   entre nosso `backup_ram` carregado (com correção de byte-swap) e a
   BIOS real. Ou seja: **não é a validação de assinatura que nos força
   pro modo self-test** — essa hipótese está descartada.
2. Tentamos achar quem escreve `wram[$10F395]`/`$10F396` via watchpoint
   real do MAME (`wpset ...,w,1,{ação}`) — reconfirma o achado antigo já
   registrado nesta sessão: **`wpset` não dispara neste MAME/ambiente**
   (zero hits mesmo num teste de 5000 vblanks completos).
3. Tentamos gerar um trace de instruções completo do MAME numa janela
   estreita (300 vblanks ao redor do vblank ~4400-4700, onde NÓS
   atingimos o estado 11) e buscar por referências a esses endereços —
   também zero ocorrências. Conclusão: **a comparação vblank-a-vblank
   entre nossa execução (com o `saveram` externo carregado) e a execução
   natural do MAME (nvram própria, "fresca") não é válida** — são dois
   cenários de boot DIFERENTES (RAM de backup carregada de formas
   diferentes), então não há por que os timings baterem. Precisaríamos
   fazer o MAME carregar o MESMO arquivo `saveram` (não tentamos ainda —
   MAME usa seu próprio formato/diretório de nvram automaticamente, não
   aceita um arquivo externo diretamente via linha de comando tão
   fácil) pra ter uma comparação de verdade válida.

**Correção importante**: `nvram/doubledr/saveram` JÁ É o nome/formato
nativo que o MAME usa pra esse driver (foi o próprio MAME que gerou esse
arquivo em sessão anterior) — quando rodamos o MAME normalmente (sem
nenhuma variável de ambiente nossa), ele JÁ carrega esse mesmo arquivo
automaticamente. Confirmado desde o início desta investigação
(`mame_30s.png`): o MAME, com esse EXATO arquivo, vai direto pro título em
30s — **nunca entra no modo self-test**. Como o conteúdo do backup RAM já
bate byte a byte com nosso `backup_ram` carregado, a causa não é o backup
RAM em si.

### Achado #20: divergência real localizada entre vblank ~200-300 (PRÓXIMO PASSO CLARO, não resolvido)
Comparação direta e limpa: dump de WRAM real do MAME (`save`, sem forçar
NADA, só `-bios euro` + o `saveram` nativo) nos mesmos checkpoints que
nosso `debug_checkpoints`:

| vblank | índice real (MAME) | índice nosso |
|---|---|---|
| 200  | `0CD8` (ainda no teste de endereço) | `FCD8` (idem, diferença de timing normal) |
| 300  | **`0000`** | `0003` |
| 500-10000 | **`0000` (sempre)** | `000D,000B,0001,0009,...` (continua ciclando) |

O MAME **sai** da máquina de estados de self-test (`$C17150`) por volta do
vblank 300 e NUNCA MAIS volta — enquanto nós continuamos ciclando por ela
pra sempre. Achamos um candidato forte pro ponto de decisão: logo no
início do wrapper que entra nessa máquina de estados (`$C17134-C17146`):
```
C17134: move SR,#$2000
C17138: tst.b $d0005e.l        ; testa 1 byte do BACKUP RAM
C1713E: bmi  c17146             ; se negativo (bit7=1): NAO pula, entra no self-test
C17140: move.w #$c,(9d8,A5)     ; se NAO-negativo: pula DIRETO pro estado 12 (pula tudo!)
C17146: jsr (c1870c,PC)
```
Conferimos `backup_ram[0x5E]` no nosso `saveram` carregado: **`0xFF`**
(negativo) — o que bate com NÓS tomando o caminho "entra no self-test".
Mas o MAME, com o MESMO arquivo, evita o self-test — ou seja, **ou esse
não é o ponto de decisão real (só coincidência), ou o MAME lê um valor
DIFERENTE nesse endereço** (região de RAM sobreposta por outra coisa
antes de chegar aqui? bankswitch? tradução de endereço $D0005E diferente
da nossa?). Não confirmamos qual das duas ainda.

- [ ] Confirmar se `$D0005E` é REALMENTE o ponto de decisão: usar `dasm`
      no MAME pra achar TODAS as referências a esse endereço específico
      (pode ter mais de um lugar escrevendo nele antes da checagem)
- [ ] Se for o ponto certo: entender POR QUE o MAME lê um valor diferente
      ali com o mesmo arquivo de backup RAM — comparar o mapeamento de
      endereço $D00000-$D0FFFF entre nossa implementação e o driver real
      do MAME (offset errado? região espelhada diferente?)
- [x] Ponto de decisão exato ACHADO (ver detalhes logo abaixo) — mas
      ainda não é suficiente sozinho pra evitar o self-test.

**Continuação — ponto de decisão real localizado:** usando a ferramenta
`debug_bp` (nova: agora imprime PCs anteriores e endereço de retorno da
pilha ao bater um breakpoint) achamos a cadeia exata que leva ao
self-test, em `$C11D4A-C11D88`:
```
C11D4A: jsr (c11db0,PC)          ; processa 1 "entrada" de bookkeeping
C11D4E: lea $d00124.l, A0        ; A0 = backup RAM offset 0x124
C11D54: moveq #0, D0
C11D56: tst.w (A0)               ; testa a word em A0
C11D58: bne c11d88               ; se != 0: pula self-test (vai pra c11028)
C11D5A: addq.l #4, A0
C11D5C: addq.b #1, D0
C11D5E: cmp.b $d00047.l, D0      ; limite do loop = backup_ram[0x47]
C11D64: bne c11d56               ; repete enquanto D0 != limite
C11D66: move.b $320001.l, D0     ; STATUS_A
C11D6C: btst #2, D0              ; bit2 = botão SERVICE
C11D70: bne c11d84               ; self-test se SERVICE nao pressionado
C11D72: jsr $c1b6fe.l            ; senao, mais uma checagem (D5)...
C11D7A: beq c11d84               ; ...que tambem pode cair no self-test
C11D7E: jmp $c1b4f0.l            ; caminho normal (jogo de verdade!)
C11D84: jmp (c17062,PC)          ; ENTRADA DO SELF-TEST
C11D88: jmp (c11028,PC)          ; caminho alternativo (nao investigado ainda)
```
No arquivo `saveram` ESTÁTICO, `backup_ram[0x124..0x125]` = `0082`
(não-zero) e `backup_ram[0x47]`=2 (limite do loop) — pela lógica acima,
isso DEVERIA cair em `bne c11d88` na primeira iteração e EVITAR o
self-test. Mas conferimos com `debug_bp` que, no momento real da
checagem (por volta da instrução 5.5 milhões, dentro do 1º vblank),
`backup_ram[0x124..0x125]` já está **zerado** — alguma coisa entre o
carregamento do arquivo e esse ponto (provavelmente a própria sub-rotina
chamada em `C11D4A`/`c11db0`, uma função de "processar 1 entrada de
bookkeeping") já consumiu/zerou essa entrada, fazendo cair no caminho de
self-test mesmo com o arquivo "válido".

**Dois experimentos que NÃO resolveram** (revertidos, código de volta ao
normal):
1. Forçar `backup_ram[0x124..0x125]` != 0 continuamente durante o
   primeiro vblank (`NEOGEO_FORCE_BKRAM_NONZERO=1` no `debug_screenshot`,
   ferramenta mantida pra próxima sessão) — ainda assim acabou voltando
   pro self-test por volta do vblank 1800 (`idx=0001`), só que com um PC
   final diferente (`C18714` em vez do padrão), sugerindo que o caminho
   muda mas AINDA existe outro jeito de cair no self-test depois.
2. Tratar o memory card como "presente" (reverter temporariamente
   `NEOGEO_STATUS_B_DEFAULT` pra `0x8F`, desfazendo o Achado #3/#14) —
   resultado idêntico ao experimento 1 (`idx=0001` no vblank 1800). Não é
   isso. Revertido de volta pra `0x9F` (valor validado pelo Achado #14
   contra um trace real do MAME).

- [ ] Investigar o caminho `C11D88 → jmp c11028` (ainda não
      desmontado/entendido) — é o caminho que DEVERIA ser tomado se
      `backup_ram[0x124]` permanecesse não-zero; entender o que ele faz
      pode revelar por que os dois experimentos acima não bastaram
- [ ] Entender a sub-rotina `C11DB0` (chamada logo antes da checagem) —
      ela roda ANTES do teste e pode ser o que zera `backup_ram[0x124]`;
      desmontar ela inteira pra achar exatamente onde/por que isso
      acontece
- [ ] Considerar que pode haver MÚLTIPLOS pontos de decisão
      convergindo pro self-test (não só este) — os dois experimentos
      mudaram o comportamento (PC final diferente) mas não o resultado
      final (`idx` ainda não-zero), sugerindo isso
- [ ] Ferramentas novas desta investigação, reaproveitáveis: `debug_bp`
      agora mostra PCs anteriores + endereço de retorno da pilha ao
      bater um breakpoint (bem mais útil que antes pra achar chamadores);
      `NEOGEO_DEBUG_LAYER`, `NEOGEO_DUMP_FIXMAP`, `NEOGEO_DUMP_PALETTE`,
      `NEOGEO_FORCE_BKRAM_NONZERO` no `debug_screenshot`

### CORREÇÃO IMPORTANTE: `bpset`/watchpoint com AÇÃO não funciona neste MAME (retifica achados anteriores)
Pedido do usuário: comparar nossos cores de CPU/Z80 contra um emulador de
referência (MAME) pra ver se o bug está aí. Ao tentar montar essa
comparação, testamos `bpset <endereço>,1,{ação; g}` num endereço que
**tem** que disparar todo frame (a entrada da ISR de vblank, `$C11646`) —
zero disparos em 3 vblanks. Isso prova que o mecanismo de AÇÃO em
breakpoints/watchpoints não funciona neste MAME/ambiente (reforça o
achado antigo sobre `wpset`, mas agora sabemos que vale pra `bpset` com
ação também). **Isso significa que as conclusões dos Achados #19/#20 que
usaram esse mecanismo (ex.: "MAME nunca executa `$C17102`/`$C11D4E`")
NÃO SÃO CONFIÁVEIS** — podem ser um artefato da ferramenta quebrada, não
um fato real sobre a execução do MAME. As conclusões baseadas em `save`
(dump de memória puro, sem ação de breakpoint) continuam válidas.

### Achado #23: cycles-per-frame errado (RESOLVIDO — bug real de timing, achado comparando WRAM completa)
Método que REALMENTE funciona pra comparar contra o MAME: dump de WRAM
COMPLETA (64KB, `save` simples) em vários checkpoints de vblank, dos dois
lados, comparados byte a byte com Python. Ferramenta nova:
`core/tools/debug_wramdump.c`.

Resultado revelador: nos checkpoints iniciais (1,2,3,13,30 vblanks) havia
bastante diferença (até ~5700 bytes), mas em VÁRIOS outros checkpoints
(5,8,20,50,80,120) os dumps batiam **100% idênticos, byte a byte, nos
64KB inteiros**. Isso já indicava que o core 68000 está fundamentalmente
correto (um bug de instrução/flag não permitiria ressincronizar
perfeitamente desse jeito) — a diferença nos outros pontos era só
DESALINHAMENTO DE FASE dentro do mesmo loop (RAM test), não bug real.

A partir do vblank ~180 surge uma diferença pequena e PERSISTENTE (3
bytes) que não desaparece mais. Um dos bytes é `wram[$10FEE4]`
(contador incrementado 1x por interrupção de vblank, sempre) — nosso
valor ficava 1 A MAIS que o do MAME. Isso apontou direto pro problema:
`NEOGEO_CYCLES_PER_FRAME` (usado pra disparar a IRQ periódica) estava
**hardcoded em 200000**, um valor já documentado como "não calibrado"
desde a Fase 4. Calculamos o valor real (NTSC): 384x264 pixels/frame, 4
mclk (24MHz)/pixel, 68000 a 12MHz (metade do mclk) →
`384*264*4/2 = 202752` ciclos/frame — confirmado contra a NeoGeo
Development Wiki. Nosso valor era **1.4% rápido demais**.

Fix: `NEOGEO_CYCLES_PER_FRAME` em `neogeo.c` (202752), e todas as
ferramentas de debug que reimplementavam a lógica de vblank manualmente
(`debug_bp.c`, `trace_boot.c`) ou passavam o budget errado pro
`neogeo_run_slice` (`debug_boot.c`, `debug_checkpoints.c`,
`debug_screenshot.c`, `debug_wramdump.c`, `debug_vblank.c`) — todas
atualizadas pra manter "1 chamada = 1 vblank real". Resultado: as
diferenças nos checkpoints de fase inicial caíram bastante (ex.: vblank1
de 194→18 bytes diferentes; vblank30 de 5715→872). Os 5 testes continuam
passando.

**Ainda não 100% resolvido**: mesmo com o fix, a partir do vblank~180 o
mesmo trio de bytes continua divergindo por exatamente os mesmos valores
(`wram[$10FEE4]` ainda 1 a mais) — sugere que um ÚNICO tick extra de
IRQ acontece bem cedo no boot (dentro da janela de fase 0-30 vblanks,
onde ainda há desalinhamento residual) e depois disso a execução
resincroniza perfeitamente, só que permanentemente deslocada por esse 1
tick. Não é mais um bug de "velocidade" (esse já foi corrigido) — é
provavelmente uma condição de borda específica (ex.: exatamente quando
durante o loop de teste de RAM o IRQ chega, um ciclo antes/depois do que
deveria) causando esse único tick extra uma vez só.

- [x] Localizado com precisão: comparando checkpoints cada vez mais finos
      (300→180→150→145..150), achamos que o primeiro byte diferente
      aparece EXATAMENTE no vblank 150 (vblank149: 0 diferenças em 64KB
      inteiros; vblank150: já diferem). 150 = 30×5 — bate exatamente com
      a 5ª troca do nosso pulso de RTC (alternava a cada 30 vblanks).

### Achado #24: pulso do RTC contava FRAMES em vez de CICLOS reais (RESOLVIDO — mas não foi a causa raiz do #20)
Hipótese natural dado o achado acima: `rtc_pulse` alternava a cada 30
*vblanks* (`vblank_count_for_rtc >= 30`), mas 30 frames a 202752 ciclos
cada = 6.082.560 ciclos — enquanto 0.5s reais a 12MHz são exatamente
6.000.000 ciclos. Essa diferença (o RTC é um oscilador PRÓPRIO,
independente do clock de vídeo) acumula e cruza a fronteira de 1 frame
por volta da 5ª troca — bate certinho com o vblank 150.

Fix: `rtc_pulse` agora alterna baseado em `master_cycles` (contagem real
de ciclos) comparado contra `rtc_next_toggle_cycles` (começa em
6.000.000, soma 6.000.000 a cada troca), independente da contagem de
frames — ver `neogeo_run_slice()` e o novo campo `rtc_next_toggle_cycles`
em `neogeo.h`. Mais correto/fiel ao hardware real (mantido mesmo sem
resolver o #20), 5 testes continuam passando.

**Resultado**: NÃO resolveu a divergência do vblank150 — na verdade
`wram[$10FEE4]` no vblank180 ficou MAIS longe do MAME (diferença foi de
+1 pra +4). Isso mostra que o pulso de RTC realmente influencia quando o
código roda (mudança de comportamento real, não around bug nosso), mas
a CAUSA RAIZ da divergência de 1-tick não é (só) o RTC — tem mais alguma
coisa. Mantido o fix mesmo assim por ser mais correto.

- [ ] A causa raiz exata do desvio no vblank150 continua em aberto.
      Comparar o card Z80 (proporção de clock 12MHz:4MHz = 3:1, usada em
      `neogeo_run_slice`) contra o real pode valer a pena — pedido
      original do usuário foi comparar CPU E Z80; só comparamos o lado
      68000/WRAM até agora
- [ ] Considerar também: nosso agendador roda o Z80 em "orçamentos" de
      `done/3` a cada passo do 68000 (proporção certa, mas granularidade
      pode diferir de como o hardware real intercala os dois — vale
      comparar contra o `z80_pc` que já imprimimos em várias ferramentas)
- [ ] Depois de achar a causa raiz de verdade, repetir a checagem se
      `$C11D4E`/o self-test (Achado #20) ainda é alcançado

### Achado #25: comparação do Z80 — divergência é CONSEQUÊNCIA do #20, não bug novo no core do Z80
Pedido do usuário: repetir a comparação byte-a-byte agora pro lado do
Z80. Estendemos `debug_wramdump` pra também salvar a RAM de trabalho do
Z80 (`sound_ram`, 2KB em `$F800-$FFFF`) e comparamos contra
`save arquivo,f800:audiocpu,800` no MAME (sintaxe `endereço:tag_da_cpu`
funciona bem pra mirar o espaço de endereço de uma CPU específica, não só
a `maincpu`).

Resultado: no vblank 1, a RAM do Z80 bate **100% idêntica** entre nós e o
MAME (incluindo uma área que parece pilha/registradores salvos por um
handler de NMI, em `$FFDE-$FFFA` — sinal de que o primeiro comando de som
foi processado de forma idêntica dos dois lados). A partir do vblank 2,
o MAME continua atualizando essa área (comandos de som novos chegando),
enquanto a NOSSA fica **congelada exatamente no valor do vblank 1** por
centenas de frames (checamos até vblank 120+).

**Correção da hipótese inicial**: pensamos primeiro que era consequência
do 68000 preso no self-test (Achado #20), só mandando 1 comando genérico.
Testamos isso direto — adicionamos um contador de escritas reais em
REG_SOUND (`sound_latch_to_z80_write_count` em `neogeo.h`/`neogeo.c`) e
descobrimos que **não mandamos NENHUM comando de som antes do vblank
250** (a primeira escrita real só acontece entre vblank180 e 250)! Ou
seja, a divergência do Z80 que aparece já no vblank1-2 não pode vir de
um comando do 68000 — nenhum dos dois lados tem comando nenhum ainda
nesse ponto.

**Nova explicação, mais provável**: o Z80 no Neo-Geo interage com o chip
de som YM2610 como parte da própria rotina de boot/self-test dele — e
**não implementamos o YM2610 nem um pouco (0%, Fase 5)**. Se o Z80 lê um
registrador de status do YM2610 esperando um valor específico (tipo
"chip pronto") e a gente devolve lixo/open-bus fixo em vez do valor real,
o PRÓPRIO código do Z80 pode tomar um caminho diferente bem cedo, sem
nenhum comando do 68000 estar envolvido. Isso explicaria a divergência
aparecer tão cedo (vblank1-2) e de forma tão estável (o Z80 fica preso
num estado próprio, não relacionado ao 68000).

- [x] Confirmado (contador de escritas): NÃO é consequência do #20 —
      zero comandos de som são enviados antes do vblank 180-250 dos dois
      lados, mas o Z80 já diverge no vblank 1-2. As duas investigações
      (#20 e #25) são independentes, não uma causa da outra.
- [x] Testado: fizemos as portas $04/$06 (status do YM2610, par A/B)
      responderem "nunca ocupado" (`0x00`) em vez do open-bus `0xFF`
      genérico (`z80_io_read_cb` em `neogeo.c`) — mantido por ser mais
      correto de qualquer forma, mas **não mudou nada** na comparação de
      RAM do Z80 (exatamente os mesmos 32 bytes/mesmos valores
      divergentes). Então a causa raiz não é (só) isso.
- [ ] Causa raiz do #25 continua em aberto. Próximas hipóteses: outras
      portas de I/O do Z80 não tratadas (dados do YM2610 em $05/$07,
      habilita/desabilita NMI em $08/$18 — ainda TODO), ou simplesmente
      o Z80 rodando rápido demais/devagar demais igual o achado #23 do
      68000 (será que a proporção 12MHz:4MHz=3:1 usada em
      `neogeo_run_slice` bate exato? vale conferir contra a wiki)

### Achado #21: estrutura de "blocos" do sprite estava errada (RESOLVIDO, mas não era a causa do bug visto)
Usuário reparou olhando a janela SDL2 ao vivo: o cursor "►" do menu de
teste de som saía serrilhado/quebrado, com um pixel solto do lado.
Conferimos `neosprite_regular_device::draw_pixel` (fonte primária real do
MAME, `src/mame/snk/neogeo_spr.cpp`) e achamos que a suposição de "4
blocos 8x8 com ordem TR,BR,TL,BL" (do resumo da wiki, Achado #16) estava
ERRADA: na verdade são só 2 METADES (esquerda x0-7, direita x8-15), cada
uma cobrindo as 16 linhas inteiras (sem dividir topo/base). Corrigido em
`sprite_tile_pixel()` (`lspc_sprites.c`) — mas isso sozinho NÃO mudou o
visual do cursor (ver Achado #22 abaixo: o cursor não é sprite). Mantido
porque é uma correção real e vai importar quando sprites de jogo de
verdade aparecerem na tela.

### Achado #22: cursor/texto vermelho do menu vem da fix layer, não de sprite — causa raiz ainda aberta
Isolamos as camadas (nova env var `NEOGEO_DEBUG_LAYER=sprites|fix`,
`neogeo_render_frame` em `lspc_sprites.c`) e confirmamos: com só sprites
ligados, a tela fica **completamente preta** — não existe nenhum sprite
ativo nessa cena. O cursor "►" E o texto "SOUND OFF" em vermelho vêm 100%
da fix layer.

Investigando mais: dump do fix map (`NEOGEO_DUMP_FIXMAP`) mostra que o
cursor é um glifo customizado de 2 tiles de altura (códigos `0x111`
topo, `0x211` base, banco de paleta 1) — e confirma que os tiles de texto
usam código = ASCII direto (`0x53`='S', `0x4F`='O' etc, igual no
"SOUND"/"SOUND OFF"). Dump da paleta (`NEOGEO_DUMP_PALETTE`) mostra que o
**banco 1** (usado pelo texto vermelho) só tem a entrada de índice 1
definida (vermelho puro, `0x0E00`) — todas as OUTRAS 15 entradas são
`0x0000` (preto). Se o glifo (seta ou letra) usa algum nibble diferente
de 0 (transparente) ou 1 (vermelho) — comum em fontes com contorno/leve
anti-aliasing — esses pixels caem em preto (igual ao fundo) e viram
"buracos" no desenho, dando exatamente o efeito de serrilhado visto.

Duas hipóteses não resolvidas:
- [ ] O decode da fix layer ainda tem um bug residual gerando nibbles
      espúrios (>1) onde deveria só ter 0/1 — comparar os nibbles
      decodificados pixel a pixel pro tile `0x111` contra uma referência
      real (MAME) confirmaria isso
- [ ] É só uma questão de TIMING: talvez o jogo ainda não tenha terminado
      de escrever as outras entradas do banco 1 de paleta no frame exato
      em que tiramos o screenshot (nosso ruído de `STATUS_B`, Achado #14,
      muda o timing do boot frame a frame de um jeito que o hardware real
      não faria exatamente igual) — testar em frames vizinhos pra ver se
      a paleta "se completa" um pouco depois
- [ ] Comparar contra o dump de paleta REAL do MAME no mesmo estado
      (usar a metodologia de `save` já validada nesta sessão) seria o
      jeito mais rápido de resolver de vez
- [ ] Investigar se ainda sobra algum problema de posicionamento/timing
      depois desses quatro fixes (#14/#15/#16/#17) — o texto da BIOS já
      está legível; próximo objetivo depois do bug de paleta é avançar até
      o logo/título de verdade e checar sprites reais (não só texto)
      contra o MAME
- [ ] Investigar por que `debug_boot`/`neogeo_run_slice` e
      `trace_boot`/loop manual produzem trajetórias DIFERENTES apesar de
      lógica aparentemente idêntica — comparar estado completo (não só PC)
      instrução a instrução entre as duas ferramentas pra achar onde
      exatamente elas se separam
- [ ] Cuidado ao ler offsets `(A5+N)` no disassembly do MAME — sempre somar
      o valor REAL de A5 (variável entre contextos, ~`$10F300`), não tratar
      o offset como endereço absoluto (bug já cometido uma vez nesta
      investigação)
- [ ] SEMPRE passar `-bios euro` explicitamente ao rodar o MAME pra
      comparação (`sp-s2.sp1` = bios "euro" no XML) — sem isso o MAME pode
      rodar com uma BIOS diferente (mesmo default aparente) e os endereços
      de código não batem, gerando comparações inúteis (perdemos bastante
      tempo nesta sessão por causa disso)
- [ ] `print`/`tracelog` do debugger do MAME não aparecem no stdout capturado
      por esta ferramenta/ambiente — usar `save arquivo.bin,addr,tamanho`
      (dump binário de memória) + `gvblank` (nome certo, não `go_vblank`)
      pra amostragem programática de estado, é muito mais rápido que um
      trace de instrução completo pra períodos longos (produzia GBs de log)
- [ ] Considerar implementar o timer interrupt (nível 2) de verdade —
      registrado mas nunca dispara ainda (Fase 4 original já apontava isso)
- [ ] Depurador básico persistente (os `debug_boot.c`/`trace_boot.c` atuais
      são one-shot; dava pra evoluir pra algo com breakpoints) — provavelmente
      o investimento mais valioso pra próxima sessão, dado quanto trabalho
      manual de desmontagem essa investigação já exigiu

### Ferramenta nova adquirida: Neo Geo Monitor Test Tool v1.3 (útil pro futuro, não destravou o #20)
Usuário indicou https://www.neohomebrew.com/projects/neo-geo-monitor-test-tool.php
— um cart homebrew com 11 telas de teste (barras de cor, padrões de
sprite, etc), disponível como romset pronto pro MAME. Site com
certificado SSL expirado (bloqueava nossas ferramentas de fetch/browser);
baixado com `curl --insecure` (autorizado pelo usuário) e extraído em
`roms/monitortest_cart/` (p1/s1/m1/v1/c1/c2, formato Neo-Geo padrão,
compatível com nosso `rom_loader.c` sem alteração nenhuma).

Testamos carregando esse cart no lugar do Double Dragon — resultado:
**ainda entra na mesma máquina de estados de self-test da BIOS** (telas
de teste de cor/memory card idênticas às que já víamos), mesmo com um
cart completamente diferente. Isso CONFIRMA que o Achado #20 é 100% um
problema em nível de BIOS, antes de qualquer código do cart rodar — não
é nada específico do Double Dragon. Uma vez resolvido o #20, essa
ferramenta vai ser ótima pra validar vídeo/sprite/paleta com padrões
conhecidos em vez de depender do conhecimento específico do Double
Dragon.

### Achado #26: ACHADO #20 RESOLVIDO — exige botão SERVICE + memory card válido
Pedido do usuário: atacar o Achado #20 de vez. Usamos `debug_bp` (agora
com histórico de PCs anteriores + endereço de retorno da pilha) pra
rastrear a cadeia de decisão inteira, instrução a instrução, na NOSSA
própria execução (confiável, ao contrário de breakpoints no MAME).

Achamos DOIS requisitos, ambos precisam ser satisfeitos:

1. **`$320001` (STATUS_A) bit2 precisa ser 0** — o código em `$C11D70`
   (`bne c11d84`) pula direto pro self-test se esse bit estiver em 1. A
   wiki descreve esse bit como "botão SERVICE" (ativo-baixo), mas o
   comportamento real exigido (sempre 0 pra não cair em self-test)
   sugere que ou não é isso mesmo, ou é uma convenção específica desse
   ponto do boot — não confirmamos a semântica real ainda, só o efeito
   observável.
2. **Memory card válido e formatado** — `$C11D72` chama `$C1B6FE`, que
   testa: STATUS_B bits5-4=00 (card presente), a assinatura ASCII
   "NEO-GEO" nos bytes reais (ímpares) de `$800041/45/49/4D/51/55/59/5D`
   (passo 4 bytes = 2 words), um byte de formato em `$800010` (nibble
   alto = 2), e um valor não-zero em `$800074`. Sem isso, a função
   retorna D5=0 e TAMBÉM cai no self-test mesmo com o bit2 certo.

Implementamos os dois: `neogeo_status_a()` com bit2=0
(`core/src/system/neogeo.c`), e um cartão de memória "de fábrica" válido
inicializado em `neogeo_init()` (assinatura NEO-GEO + formato válido).
Isso também exigiu REVERTER o Achado #14 (`NEOGEO_STATUS_B_DEFAULT`
voltou de `0x9F` pra `0x8F`, bits5-4=00/card presente) — documentamos no
código que o Achado #14 comparou contra um trace do MAME feito ANTES de
sabermos que precisa passar `-bios euro` explicitamente, então pode ter
sido uma comparação inválida (BIOS errada).

**Resultado confirmado com `debug_bp`**: alcançamos `$C11D7E`
(`jmp $c1b4f0.l`, o caminho NORMAL) em vez de `$C11D84` (self-test) — e
`wram[$FCD8]` (índice de estado do self-test) fica em `0x0000` até pelo
menos vblank 9000, batendo com o comportamento real do MAME.

**A tela mudou pra algo NUNCA visto antes nesta sessão inteira**:
"CABINET No. 0" / "SLOT 1" — uma tela real de configuração de cabinet do
boot normal, não mais o grid de self-test. Mas essa tela fica PARADA de
vblank 1800 até pelo menos 9000 (idêntica) — mais um gate de
input/confirmação, parecido com o padrão do Achado #19.

- [x] Investigado o novo gate ("CABINET No./SLOT"): usa `wram[$10FEEA]`
      (`bea,A5`) — precisa dos bits4-7 TODOS setados (`&0xF0==0xF0`) pra
      confirmar e sair da tela (`$C1B636: beq c1b7aa`). Testado com
      `NEOGEO_HOLD_BUTTONS`/`NEOGEO_HOLD_BUTTONS` novo em `debug_bp` e
      `NEOGEO_DIRECTION_SEQUENCE` novo em `debug_screenshot`:
  - Cada direção isolada marca só seu próprio bit (UP=0x18, DOWN=0x20,
    LEFT=0x40, RIGHT=0x80) — não é cumulativo entre frames (testamos
    apertar em sequência, uma de cada vez, soltando entre elas — não
    funcionou, o valor não "gruda").
  - Segurando as 4 direções AO MESMO TEMPO (`p1_buttons=0xF0`) o valor
    vira `0xF8`, satisfazendo a checagem — MAS isso leva a um estado de
    erro logo depois (`$C1B848: move.b D0,$300001; bra $c1b848` — um
    loop infinito de "kick watchdog", padrão já visto bem no início
    desta investigação como sinal de erro/trava, não confirmado ainda se
    é fatal de verdade ou só mais um estado de espera legítimo).
  - Não é realista fisicamente segurar 4 direções de um joystick de
    verdade ao mesmo tempo — suspeita: talvez `(bea,A5)` não seja
    realmente sobre as 4 direções do P1, e sim outra coisa (DIP
    switches? múltiplos jogadores?) que só COINCIDE em posição de bit
    com as direções na nossa implementação simplificada de input.
- [x] Testado sem nenhum input por 15000 vblanks (250s emulados): a tela
      "CABINET No./SLOT" **não tem timeout automático** — fica parada
      pra sempre sem confirmação. Mapeamos `(bea,A5)` bit a bit
      testando cada botão isolado: A→bit0, B→bit1, C→bit2, UP→bit4,
      DOWN→bit5, LEFT→bit6, RIGHT→bit7 (D→nenhum efeito). Confirma que
      é mesmo P1 (não DIP switch nem outra coisa) — mas o código
      realmente exige as 4 direções simultâneas (fisicamente irreal)
      pra setar `bits4-7` e confirmar, e isso leva a um trap depois
      (`$C1B848`, padrão "watchdog kick infinito" já visto antes).
- [ ] Hipótese mais provável agora: essa tela só deveria aparecer UMA
      VEZ (configuração de fábrica) e depois fica "lembrada" no memory
      card — nosso cartão fake não tem esse campo "já configurado", daí
      a tela aparece sempre. Precisa achar QUAL campo do memory card
      guarda isso (provavelmente perto da assinatura NEO-GEO ou do byte
      de formato em `$800010`) e pré-preencher com um valor "já
      confirmado" em vez de tentar simular o input de confirmação
- [ ] Alternativa: aceitar que essa tela precisa de input real e
      implementar um input handler de verdade (não só simulação pontual
      pra debug) no frontend SDL2, e testar com teclado de verdade —
      pode ser que o combo real (não as 4 direções ao mesmo tempo) só
      fique óbvio testando interativamente
- [ ] Considerar que o "confirm" talvez não seja sobre o joystick do P1
      de jeito nenhum — pode ser derivado de DIP SWITCHES (configuração
      física do operador, que já vem "certa" sem precisar de input
      nenhum na inicialização normal); vale conferir se algum bit do
      nosso `NEOGEO_DIPSW_DEFAULT` deveria alimentar esse cálculo

Desmontamos mais o caminho de "confirmado" (`$C1B7AA` em diante): carrega
dados do memory card em 3 blocos (via rotina genérica `$C1B850`, copia
words fazendo "kick" no watchdog a cada iteração) — o último bloco sozinho
lê ~4336 words (8672 bytes), MUITO além dos 2KB reais de um memory card
(nosso `is_memcard_addr` só cobre `$800000-$800FFF`, 4KB de espaço de
endereço = 2KB reais) — bytes além disso viram open-bus (0xFF) no nosso
emulador. Provavelmente é assim mesmo no hardware real (espaço de
endereço maior que o cartão físico, por design/expansão futura), não
parece ser a causa do travamento.

Depois desse carregamento, cai direto no loop `$C1B848: move.b D0,$300001;
bra $c1b848` — só kickando o watchdog, SEM nenhuma condição de saída
visível no código ao redor. Isso não parece um "espera por evento", parece
um estado que o jogo não esperava alcançar por esse caminho (confirma que
forçar as 4 direções simultâneas não é o gatilho real, é só um jeito de
"forçar passagem" que quebra algo depois).
- [ ] Confirmar/entender por que bit2 de STATUS_A precisa ser 0 —
      pesquisar mais a função real desse bit nesse contexto específico
      (pode não ser literalmente o botão SERVICE físico)
- [ ] Revisar se o Achado #14 (branch em `$C1336A`) ainda faz sentido
      com STATUS_B bits5-4=00 agora — testar se isso reabre algum
      problema resolvido antes
- [ ] Depois de destravar essa tela: comparar novamente contra o MAME
      real (idx, WRAM completa) pra confirmar que continuamos em
      lockstep até o título/logo de verdade
- [ ] Testar o Monitor Test Tool de novo agora que o self-test da BIOS
      não trava mais — deve chegar nas telas de teste reais dele

### Achado #27: fix layer sempre lia a S ROM do CARTUCHO, nunca a SFIX própria da BIOS — RESOLVIDO (bug real, mas não é a causa completa do "CABINET/SLOT" corrompido)
Usuário reparou (visualmente, na tela "CABINET No. 0"/"SLOT 1" nova do
Achado #26) que os glifos saem cheios de "buracos"/quebrados — pior do que
qualquer coisa vista antes na sessão. Investigando com um dump bruto dos
bytes do tile em VRAM (`NEOGEO_DUMP_FIXMAP` ampliado + novo
`NEOGEO_DUMP_TILE`, ambos em `debug_screenshot.c`), achamos que essa tela
usa uma "fonte grande" (2 tiles de 8x8 empilhados por caractere, códigos
`0x100+ascii` em cima e `0x200+ascii` embaixo — técnica padrão, não é bug)
— OK até aqui.

O problema real: `neogeo_romset_t.s`/`s_size` (usado por `fix_tile_nibble`
em `lspc_fix.c`) SEMPRE vinha do arquivo `s1` do CARTUCHO
(`rom_loader.c`) — a ROM `sfix.sfix` da BIOS (SFIX, fix graphics PRÓPRIA
do sistema, usada pra desenhar as telas que a própria BIOS desenha:
self-test, memory card, CABINET/SLOT etc) nunca era sequer carregada.
Confirmamos com a fonte real do MAME (`src/mame/snk/neogeo.cpp`,
baixado via `curl` e conferido com `grep`, não resumo de wiki):

```c
void neogeo_base_state::set_use_cart_audio(int state)
{
    m_use_cart_audio = state;
    m_sprgen->set_fixed_layer_source(state);   // 0=BIOS SFIX, 1=cart S1
    m_bank_audio_main->set_entry(m_use_cart_audio);
}
...
m_systemlatch->q_out_cb<5>().set(FUNC(ngarcade_base_state::set_use_cart_audio));
```

Ou seja: existe um latch de hardware real (HC259, endereços
`$3A0000-$3A001F`, cada escrita seta/limpa 1 de 8 bits — o BIT usado vem
do ENDEREÇO, não do valor escrito: `write_bit(offset&7, (offset&8)>>3)`)
cujo **bit5** troca JUNTO o banco de ROM de áudio do Z80 e a fonte da fix
layer entre BIOS (SFIX/audio BIOS compartilhada) e cartucho (S1/M1) — e
esse bit começa em **0 (BIOS)** no reset (`uint8_t m_use_cart_audio = 0;`
no header do MAME). Ou seja: bit1 do mesmo latch (`use_cart_vectors`) é
outra coisa (troca o mapeamento de vetores em `$0-$FFFFF`, ainda não
implementado no nosso mapa de memória — TODO futuro), não confundir os
dois como quase fizemos ao ler um resumo de IA da mesma fonte primeiro
(a primeira tentativa via `WebFetch` resumiu ERRADO, dizendo que
`set_use_cart_vectors` controlava a fix layer — só bateu depois de ler o
`.cpp` bruto com `grep`, mais uma vez confirmando o padrão desta sessão
de não confiar em resumo de IA pra detalhe de bit-a-bit).

Implementado:
- `neogeo_romset_t.sfix`/`sfix_size` (`neogeo.h`) + carregamento de
  `sfix.sfix`/`sfix.bin` do diretório de BIOS em `rom_loader.c` (mesmo
  padrão do `audiobios` já existente — opcional, não falha se ausente).
- `neogeo_t.use_cart_fix` (`neogeo.h`), default 0 no reset
  (`neogeo_reset`, `neogeo.c`).
- Handler novo pro range `$3A0000-$3A001F` em `m68k_write16_cb`
  (`neogeo.c`): decodifica bit/valor do OFFSET do endereço (não do dado
  escrito, replicando o HC259 real) e atualiza `use_cart_fix` no bit5.
- `fix_tile_nibble` (`lspc_fix.c`) agora escolhe `rom.sfix` ou `rom.s`
  conforme `use_cart_fix` (com fallback pra `rom.s` se `sfix` não foi
  carregado, pra não quebrar os testes sintéticos que só populam `s`).

**Resultado real, verificado byte a byte**: comparamos o screenshot antes
e depois do fix (`cmp`/diff em Python) — 1974 bytes (658 pixels) mudaram,
todos na linha "SLOT 1" (os tiles da BIOS SFIX e do S1 do cartucho não
são idênticos ali, confirmado comparando os arquivos `.rom` crus). Ou
seja, o bug era real e o fix funciona — MAS o resultado visual continua
com os mesmos "buracos" característicos, porque:

**Causa raiz do "buraco" em si é OUTRA, ainda aberta — é a mesma classe
do Achado #22, agora confirmada também no banco 0 de paleta**: dump dos
nibbles crus do tile `0x143` (topo do "C" de CABINET), já lendo
corretamente da SFIX real da BIOS, mostra uma grade 8x8 cheia de valores
`1` E `2` (nenhum `0` — glifo sólido, sem fundo transparente dentro do
tile). A paleta do banco 0 nesse momento só tem a entrada de índice 1
definida (`0x0EEE`, branco); a entrada 2 é `0x0000` (preto, igual ao
fundo) — exatamente o mesmo padrão que o Achado #22 já tinha achado no
banco 1 (cursor "►"). Ou seja: a fonte grande da BIOS parece ser um desenho de
2 cores DE VERDADE (preenchimento + contorno, por exemplo), e falta essa
segunda cor ser escrita na paleta.

Testamos a hipótese de ser só timing (esperar mais frames): rodamos até
3000 e 5000 vblanks — a paleta NÃO muda, porque o PC já está parado no
loop de espera `$C18714`/`$C1890C` (mesmo "kick watchdog" genérico do
Achado #26/#22), esperando confirmação de input que nunca chega nesse
teste. Então dá pra descartar "só precisa esperar mais um pouco" NESSE
estado parado especificamente — mas continua em aberto se:
- [ ] O código que escreveria a 2ª cor da paleta só roda DEPOIS da
      confirmação da tela CABINET/SLOT (Achado #26, ainda não resolvido)
      — ou seja, os "buracos" podem ser 100% esperados/reais enquanto a
      tela está parada esperando input, e sumir assim que destravarmos
      o gate de confirmação
- [ ] Ou é um bug de decode ainda não achado, específico dessa fonte
      grande (2 tiles empilhados) — precisaria comparar nibble a nibble
      contra uma referência real (MAME rodando essa MESMA tela) pra
      descartar de vez, o que exigiria primeiro reproduzir esse estado
      em MAME de verdade (não simulado via Achado #26) — não tentado
      ainda nesta sessão
- [ ] Revisitar Achado #22 (cursor/"SOUND OFF" em vermelho, banco 1) à
      luz disso: mesma causa provável (paleta incompleta por estar
      parado esperando input), não um bug de decode como suspeitávamos

### Achado #28: RESOLVIDO — a ordem dos nibbles da fix layer estava invertida (era a causa real dos "buracos")
Usuário insistiu que continuava errado depois do Achado #27 ("viu tem
erro.. ficou até pior") e pediu pra resolver "os frames corretos". Voltamos
à fonte primária de verdade — não o `charlayout` (uma tabela de offsets,
fácil de interpretar errado, como o Achado #17 já tinha feito uma vez) mas
o CÓDIGO DE RENDER de fato: `neosprite_base_device::draw_fixed_layer_2pixels`
em `src/mame/snk/neogeo_spr.cpp` (baixado da fonte real do MAME no GitHub):

```c
inline void neosprite_base_device::draw_fixed_layer_2pixels(u32*&pixel_addr, int offset, u8* gfx_base, const pen_t* char_pens)
{
    const u8 data = gfx_base[offset];
    if (data & 0x0f) *pixel_addr = char_pens[data & 0x0f];        // 1o pixel do par (x par)
    pixel_addr++;
    if (data & 0xf0) *pixel_addr = char_pens[(data & 0xf0) >> 4]; // 2o pixel do par (x impar)
    pixel_addr++;
}
```

Isso é inequívoco: **x par = nibble BAIXO, x ímpar = nibble ALTO** — o
OPOSTO do que o Achado #17 tinha concluído (`x par = alto, ímpar = baixo`).
A parte das 4 byte-colunas (ordem `x{4,5},x{6,7},x{0,1},x{2,3}`) do Achado
#17 estava certa (bate com `pix_offsets[] = {0x10,0x18,0x00,0x08}` do mesmo
arquivo); só a ordem dos nibbles dentro de cada par estava trocada.

Corrigido em `fix_tile_nibble()` (`core/src/video/lspc_fix.c`). Resultado:
a tela "CABINET No. 0"/"SLOT 1" (mesmo estado do Achado #27) agora renderiza
os glifos LIMPOS, sem nenhum buraco — texto perfeitamente legível,
confirmado visualmente. Isso explica retroativamente todos os "buracos
seguindo o formato de um glifo quase reconhecível" vistos desde o Achado
#17 (cursor "►" serrilhado do Achado #21/#22, texto "SOUND OFF"
vermelho) — não eram problema de paleta incompleta, eram cada par de
pixels saindo espelhado.

**Lição sobre o `test_video.c` existente**: o teste de fumaça usava um
tile sintético "todo byte 0x11" — totalmente simétrico (nibble alto = nibble
baixo = 1 em todo lugar), então passava IGUAL com a ordem certa ou
invertida, e não pegou esse bug nem teria pego o do #17. Reforçado pra
codificar o índice X em cada nibble (pixel x recebe valor x+1), travando
byte-colunas E ordem de nibble ao mesmo tempo — ver comentário no topo do
arquivo.

- [ ] Revisitar Achado #22/#27 à luz disso: o "buraco" ali pode ter sido
      SÓ esse bug de nibble invertido (já resolvido), não paleta
      incompleta — testar de novo o cursor "►"/"SOUND OFF" agora
- [ ] Conferir se `lspc_sprites.c` (sprites de verdade, não fix layer) usa
      a mesma convenção de nibble par/ímpar ou se é independente — o
      formato de sprite é decodificado por bitplane (`draw_pixel` do MAME),
      não por nibble direto, mas vale checar se não há um erro análogo lá

### Achado #29: mapeado o resto do fluxo CABINET/SLOT — o "watchdog kick infinito" é quase certamente um HALT DE ERRO real da BIOS, não um bug nosso de trigger
Continuando o Achado #26 a pedido do usuário. Desmontamos por completo
`$C1B4F0` até `$C1B850` (usando os dumps já salvos em
`third_party/mame/dasm_c1b4f0_full.txt`/`dasm_c1b6fe_full.txt`) e
decodificamos os "opcodes ilegais" que víamos antes como o que realmente
são: **strings ASCII embutidas no meio do código** (`$C1B6C4` = literalmente
os bytes "NEO-GEO"; `$C1B6CC`/`$C1B6EA` = texto "CABINET No"/"SLOT";
`$C1B85A` = mais uma string, não um jump-alvo). Um sistema de fila de
mensagens em `($abe,A5)` (bump-pointer em WRAM $10FF00+) guarda entradas
`{ponteiro_da_string, ...}` que `$C1871E`/`$C1870C` processam pra
desenhar o texto — por isso o `move.l #$c1b85a,(A0)+` que parecia
"escrever um endereço de salto" na real só está enfileirando mais uma
mensagem de texto. **Isso invalida a suspeita anterior de que o código
"pulava pra dentro de FFFF de padding" — não pula, só enfileira.**

Confirmamos por completo a lógica de `$C1B6FE` (chamada em 2 pontos do
fluxo): é EXATAMENTE o teste de memory card do Achado #26 (assinatura
"NEO-GEO", formato, byte não-zero) — **nada a ver com botão**. Como nosso
cartão fake já é válido desde o boot, essa checagem passa na primeira
tentativa em ambos os pontos — ou seja, o loop de espera nunca trava
esperando ISSO.

O gate de confirmação real é só em `$C1B62E-$C1B63A`:
```
move.b ($bea,A5), D0
andi.b #$f0, D0
cmpi.b #$f0, D0
beq    $c1b7aa        ; TODAS as 4 direções (bits4-7) -> confirma e sai
```
`($beb,A5)` bits0-1 fazem o ajuste de número de slot (LEFT/RIGHT),
limitado por `($74,A6)` = memcard byte `$3A` (o mesmo byte que preenchemos
com `0x01` no Achado #26 só pra passar o teste de "não-zero" — e que aqui
é reaproveitado como "quantidade máxima de slots").

Depois de confirmar, `$C1B7AA` monta A4=`$D08000` (dentro da nossa
backup RAM), zera SR pra `$2700` (**mascara TODAS as interrupções**,
inclusive vblank) e faz 3 cópias em bloco (`jsr $c1b850` x3, rotina de
cópia simples SEM nenhuma validação do conteúdo) totalizando ~9KB lidos
sequencialmente do memory card a partir de `$800100+slot*0x2800` — bem
além dos 2KB reais do nosso cartão simulado (o resto vira open-bus). Essa
cópia sempre TERMINA (não tem branch condicional dentro dela), e cai
direto, sem nenhum teste no meio, em `jsr $c1871e` seguido do loop
`$C1B848: kick watchdog; bra $c1b848`.

**Conclusão**: `$300001` é o registrador real de watchdog reset (kick).
Um loop que SÓ kicka o watchdog pra sempre, com TODAS as interrupções
mascaradas (`$2700`) e sem nenhum outro efeito colateral, é a assinatura
clássica de um "halt de erro seguro" em firmware 68k dessa época — trava
o sistema de propósito, sem resetar, esperando o operador perceber que
algo deu errado (nunca reseta sozinho porque o watchdog é kickado
manualmente). Como a cópia dos ~9KB não tem NENHUM branch condicional, o
halt acontece **sempre**, incondicionalmente, depois de confirmar — ou
seja, não é uma questão de "os dados copiados estarem errados", é o
próprio fluxo currentemente entendido que sempre desemboca nesse halt.
Isso sugere fortemente que falta alguma coisa ANTES desse ponto (um
desvio que não achamos ainda, provavelmente dependente de algum campo
específico do memory card de ~9KB que nosso cartão fake mínimo não
preenche) — não que o gate de "4 direções" esteja errado em si.

- [ ] Achar o desvio que EVITA cair em `$C1B848` — provavelmente uma
      checagem em algum ponto de `$C1B850`→`$C1871E` sobre o conteúdo
      recém-copiado (ou uma call que `c1871e` faz e nós não seguimos)
      que decide "config de cabinet válida" vs "erro". Precisa de mais
      desmontagem a partir de `$C1871E` pra frente (ainda não feita)
- [ ] Considerar buscar documentação da comunidade sobre o formato
      "estendido"/"linked cabinet" do memory card (~9KB por slot) em vez
      de continuar só por desmontagem crua — pode já existir descrição
      disso em fóruns/wikis de hardware MVS
- [ ] Alternativa mais pragmática: já que essa tela parece só existir pra
      cabines MVS linkadas multi-slot (o Double Dragon é um jogo comum,
      não deveria precisar disso pra rodar), reconsiderar se o Achado #26
      (bit2=0 fixo) está simulando um estado de hardware que nunca
      aconteceria numa cabine MVS single-slot normal — talvez bit2 devesse
      ser 0 só condicionalmente, e não permanentemente

### Achado #30: revertido bit2 de STATUS_A pra 1 — self-test agora CICLA de verdade (não trava mais!), mas isso não é o caminho pro jogo
A pedido do usuário, reconsideramos o Achado #26: `neogeo_status_a()`
voltou a ter bit2=1 (valor original `0x9Fu`, "SERVICE solto"/default),
mantendo o memory card válido (Achado #26) e as correções de fix layer
(Achados #27/#28). Resultado, visto com `debug_checkpoints`/
`debug_screenshot`: o índice de estado (`wram[$FCD8..9]`) agora **CICLA**
entre vários valores diferentes (`0003→000d→0002→000b→0001→0005...`) em
vez de ficar travado num só — e as telas por trás disso são telas de
self-test **genuínas e limpas** (graças ao Achado #28): barras RED/GREEN/
BLUE/WHITE, grade de convergência, "SETTING UP THE CALENDAR" (ajuste de
data/hora), "I/O CHECK" (mostra estado de P1/P2, DIP switches 1-8, TEST,
COIN1/2, SERVICE, LEDs). Nenhum buraco/serrilhado nos textos — confirma
que o Achado #28 resolveu geral, não só a tela CABINET/SLOT.

Isso é uma vitória de qualidade de emulação (o self-test da BIOS real
funciona, com boa confiança), mas **não é o caminho pra chegar no jogo**:
o self-test, uma vez entrado, cicla pra sempre entre suas próprias telas
(esse é o comportamento REAL — em hardware de verdade também não existe
"saída automática" do self-test, é preciso desligar a DIP switch física
de TEST e reiniciar). A tela "I/O CHECK" mostra um campo `TEST=1`
separado de `SERVICE=0` — ou seja, existe uma DIP switch de TEST
distinta do bit "SERVICE" que estivemos mexendo, e é PROVAVELMENTE essa
DIP (não STATUS_A bit2) que decide "entra em self-test" de verdade no
hardware real. `NEOGEO_DIPSW_DEFAULT` (`0xFBu`, ver `neogeo.c`) foi um
palpite de que bit2 do DIPSW controla um "pula debounce" — não bate
obviamente com o campo "DIP 12345678" mostrado na tela I/O CHECK
(`00100000`, bit3 setado só), então a fonte real da DIP de TEST ainda
não está confirmada.

- [ ] Achar QUAL bit (STATUS_A, DIPSW ou outro registrador) faz o campo
      "TEST" da tela I/O CHECK mostrar 0 em vez de 1 — provavelmente essa
      é a DIP switch física que, desligada, evita entrar no self-test de
      vez, sem precisar do hack de bit2 nem do fluxo CABINET/SLOT
- [ ] Voltar bit2 de STATUS_A pro que a wiki diz (`SERVICE`, sem alterar)
      e testar create com a DIP de TEST identificada e desligada — ver se
      o boot vai direto pro jogo sem passar nem por self-test nem por
      CABINET/SLOT
- [ ] Enquanto isso, aproveitar que o self-test agora funciona bem pra
      validar mais a fundo a fix layer (todas as telas vistas aqui) e,
      quando chegar na tela de teste de som, conferir se o cursor "►"
      (Achado #22) saiu limpo também — CONFIRMADO limpo (ver Achado #30).

### Achado #31: bypass pragmático do sistema de "cabine linkada" — chegamos no MENU DE OPERADOR da BIOS, ainda não no jogo
A pedido do usuário ("libera pro jogo e vamos ver"), em vez de continuar
tentando satisfazer o fluxo CABINET/SLOT manualmente, desmontamos o
resto de `$C11D4A-$C11D88` (a decisão real antes do self-test) e achamos
uma TERCEIRA saída nunca vista antes: `$C11D88: jmp $c11028` — tomada
quando um SCAN em `backup_ram+$124` (tabela de até `backup_ram[0x47]`
entradas de 4 bytes) acha uma palavra não-zero, interpretada como "outra
cabine respondeu no link". Confirmamos com `debug_bp` que `$C11DB0`
(chamada logo antes do scan) SEMPRE invalida essa tabela (zera as
entradas dentro do próprio limite que ela mesma define) — ou seja, esse
caminho não é alcançável sem simular hardware de link real de verdade
entre cabines MVS, e testamos à exaustão que pré-preencher
`backup_ram[0x124]`/`[0x47]` antes do boot não adianta (a rotina
reescreve tudo, inclusive com backup_ram vazio/zerado do zero).

Aplicamos dois desvios pragmáticos e claramente documentados como
gambiarra em `neogeo_run_slice()` (`core/src/system/neogeo.c`):
1. **Achado #31**: PC força de `$C11D66` pra `$C11D88` (pula a decisão
   self-test/CABINET-SLOT inteira, direto pro "cabine detectada").
2. **Achado #31b**: achamos que esse atalho sozinho cai numa SEGUNDA
   checagem idêntica mais adiante (`$C123DE-$C123F4`, mesma tabela) que
   trava pra sempre (testado com 500 MILHÕES de instruções sem sair,
   com E sem backup_ram carregado — mesmo resultado). Forçamos
   `backup_ram[0x124]=0xFFFF` bem antes dessa checagem específica
   (`pc==$C123F0`), simulando a "resposta" que não temos como dar de
   verdade.

**Resultado**: o boot agora AVANÇA de verdade — sai da rotina de espera
infinita e entra em ciclo normal de vblank (`idx=0000`, PC variando
entre `$C1850x/$C185xx/$C1871x`, exatamente como o self-test fazia
quando funcionando). A tela mostra o **MENU DE OPERADOR da BIOS**:
"HARDWARE TEST / SETTING UP THE HARD DIP / SETTING UP THE SOFT DIP /
BOOK KEEPING / SETTING UP THE CODE NUMBER / SETTING UP THE CALENDAR /
EXIT" — um menu de configuração real (diferente do self-test cíclico),
renderizado limpo (Achado #28 confirmado de novo aqui). Fica parado
esperando input, sem timeout automático (testado até 10000 vblanks).

Tentamos navegar até "EXIT" com `NEOGEO_MENU_START`/
`NEOGEO_MENU_DOWN_TAPS` (novo em `debug_screenshot.c`: N toques
discretos de DOWN + toque de A) — 6 toques de DOWN + A por volta do
vblank 1000. Resultado: o sistema **reseta** (idx volta pro valor de
boot inicial `fcd8`) mas depois de reiniciar cai de novo no MESMO MENU
DE OPERADOR (cursor de volta em "HARDWARE TEST") em vez de ir pro jogo.
Não confirmamos ainda se "EXIT" reinicia E deveria continuar pro jogo
numa segunda passada (só que nosso reset também reaplica os bypasses
#31/#31b, então cai nesse mesmo menu de novo — pode ser um "loop" real
enquanto os bypasses estiverem ativos), ou se o timing da navegação
(vblank 1000 pode ser cedo/tarde demais relativo ao boot real) não
selecionou EXIT de verdade.

- [ ] Investigar por que o menu de operador aparece: pode ser que os
      bypasses #31/#31b, além de evitar o travamento, acidentalmente
      LIGUEM alguma flag que força esse menu (ele normalmente só aparece
      segurando um botão físico durante o boot em hardware real) — vale
      conferir `wram`/`backup_ram` perto do que decide mostrar esse menu
      especificamente
- [ ] Refinar o timing/sequência de navegação até EXIT — testar
      vblanks de início diferentes, confirmar se A é mesmo o botão de
      seleção certo (ou se é preciso soltar D0/D-pad de forma diferente)
- [ ] Considerar logar/imprimir toda vez que os bypasses #31/#31b disparam
      (contador de quantas vezes cada um ativa por boot) pra entender se
      o "loop" após EXIT é os MESMOS bypasses disparando de novo, ou algo
      novo
- [ ] Objetivo imediato: sair desse menu de operador e alcançar o
      primeiro frame de gameplay real do Double Dragon, pra finalmente
      validar sprites de jogo de verdade contra o Achado #21

Confirmamos com `debug_screenshot` (novo `NEOGEO_MENU_START`/
`NEOGEO_MENU_DOWN_TAPS`, toques discretos de DOWN+A) que a navegação por
botão FUNCIONA de verdade: 1 toque de DOWN + A entra corretamente em
"SETTING UP THE HARD DIP" (submenu de configuração, também limpo). Com 6
toques de DOWN (confirmado, `idx` sobe 1 por toque: 0001→0002→...→0006,
0006="EXIT") + A, o cursor chega em EXIT e confirma — e o sistema
**reseta de verdade** (PC volta pra rotina de reset em `$C1319x`, idx
volta pro valor sentinela `$FCD8` de boot bem no início). Só que, depois
de reiniciar, cai EXATAMENTE no mesmo menu de operador de novo (não no
jogo) — porque nosso bypass #31/#31b são checks de ENDEREÇO de PC/dados
fixos em `neogeo_run_slice`, então disparam de novo a cada boot,
incondicionalmente.

Testamos a hipótese do Achado #30 (DIP de TEST separada do que
mexíamos) trocando `NEOGEO_DIPSW_DEFAULT` de `0xFB` pra `0xFF` (bit2→1)
— **não mudou nada**, revertido. A DIP/flag real que faz esse menu
aparecer ainda não foi encontrada.

- [ ] Hipótese mais provável agora: o menu de operador não é causado por
      uma DIP incorreta, e sim é o próprio `$C11028` (destino do nosso
      bypass #31) que, no fluxo real "cabine linkada confirmada", faz
      parte de uma sequência de configuração de link (não é "pula direto
      pro jogo" como assumimos) — ou seja, o Achado #31 nos tirou do
      travamento mas nos colocou n uma ramificação de CONFIGURAÇÃO, não
      na ramificação de JOGO. Precisa desmontar mais adiante de `$C11028`
      (e o que `$C13144`/`$C13196` fazem, os PCs vistos logo após
      confirmar EXIT) pra achar OUTRO ponto de saída que realmente carregue
      o cart e comece o gameplay
- [ ] Considerar reverter completamente os bypasses #31/#31b e, em vez
      disso, investir na outra ponta (achar o formato de ~9KB do memory
      card pro fluxo CABINET/SLOT manual terminar sem travar em
      `$C1B848`, Achado #29) — pode ser o caminho mais curto de verdade,
      já que aquele SIM é o fluxo "normal" documentado pelo próprio jogo
      (não uma ramificação de cabine linkada)

### Achado #32: achado o gate real do menu de operador — DIPSW ($300001) bit0
A pedido do usuário ("desmontar mais além de $C11028"), seguimos o fluxo
completo: `$C11028→$C11038` (init genérico, igual ao de `$C1B4F0`) →
`$C11136` → `$C1115C: jmp` via TABELA DE SALTOS indexada por `(a81,A5)`
(0,1,2 → `$C11174`/`$C111A0`/`$C111EA`, reconstruída lendo os bytes crus
do BIOS diretamente em Python, já que o disassembly linear confunde a
própria tabela de endereços com código "ilegal"). `(a81,A5)=0` por
padrão (nunca escrito antes disso, zerado no clear geral) leva a
`$C11174`, que checa `backup_ram[0x109]==8` (bate com nosso save real:
`8`) → `$C11368` → outro dispatch aninhado no MESMO `(a81,A5)` →
`$C1139C`: checa `backup_ram[0x109]==8` de novo → `$C113DE`:

```
move.b $300001.l, D0   ; DIPSW
andi.b #1, D0          ; bit0
beq    $c18f74         ; bit0==0 -> pula direto pra outra rotina de boot
```

**`NEOGEO_DIPSW_DEFAULT` (`0xFB`) tinha bit0=1** — por isso sempre
caíamos na continuação "fica no menu" em vez de pular pra `$C18F74`.
Mudado pra `0xFA` (bit0=0). Confirmado com `debug_bp`: agora
ALCANÇAMOS `$C18F74` de verdade (era só um TESTE de bit — nenhum bypass
de PC/memória como os achados #31/#31b, esse é limpo).

`$C18F74` faz um init genérico (igual `$C1B4F0`/`$C11038` — ler
BIOS/RAM várias vezes é normal, todos os pontos de entrada fazem isso)
e continua até `$C19082`, que monta uma tela BEM diferente da
CABINET/SLOT e do menu de operador: enfileira 3 valores lidos de
`(ad2,A5)/(ad3,A5)/(ad4,A5)` formatados como dígitos (rotina
`$C1915C`), seguido de mais uma mensagem — **muito provavelmente uma
tela de crédito/high-score** (o tipo de coisa que aparece ANTES do
attract mode/logo). Depois disso, cai num loop em `$C190DE` esperando
uma mudança em `(a97,A5)` bits0-1 ou bit4 — um loop de espera de INPUT
normal (`btst #4,D0; bne $c1927e` / `andi.b #3,D0; beq $c190de`), não um
travamento tipo watchdog.

**Confirmamos com `debug_bp` que esse loop É alcançado de verdade**
(instrução ~5.65 milhões, `master_cycles≈47 milhões` ≈ vblank 230-ish).

- [ ] Achado #33 abaixo explica por que `debug_screenshot` (via
      `neogeo_run_slice`, o mesmo caminho do frontend SDL2) NÃO mostra
      essa tela nos mesmos vblanks — é uma divergência real de
      trajetória entre ferramentas, não um erro de raciocínio aqui
- [ ] Descobrir o que `(a97,A5)` representa e o que faz o loop
      `$C190DE` avançar — provavelmente outro campo de input (talvez
      START, já que vimos "START 1" pro P1 na tela I/O CHECK antes)
- [ ] Seguir montando o mapa a partir de `$C1927E` (destino do
      `bne` quando bit4 de `(a97,A5)` está setado) — pode ser literalmente
      onde o jogo começa de verdade

### Achado #33: RESOLVIDO o mistério da divergência entre ferramentas — E achado um problema mais profundo (sensibilidade ao "ruído" do STATUS_B) — CORREÇÃO: a "instabilidade" era erro de comparação nosso, não um bug real
**Correção importante** (feita na mesma sessão, logo depois): escrevemos
`debug_compare.c` (novo, roda DUAS instâncias de `neogeo_t` em paralelo —
uma via `neogeo_run_slice` por vblank, outra passo-a-passo cru — e
compara `pc`/`master_cycles` no MESMO número exato de ciclos a cada
fronteira de vblank) especificamente pra confirmar a suspeita de
"sensibilidade ao ruído" abaixo. Resultado: **NENHUMA divergência
detectada até vblank 3000**. As duas formas de rodar são, de fato,
100% equivalentes — o "mistério" original era um ERRO DE COMPARAÇÃO
nosso: comparamos o ponto de parada do `debug_bp` (contado em
INSTRUÇÕES, ~5.65M) contra o ponto de parada do `debug_screenshot`
(contado em VBLANKS, ~230) assumindo uma correspondência aproximada
entre os dois que não é exata — e como cada ponto de parada captura o
PC num instante ARBITRÁRIO da execução (não necessariamente um estado
"parado"/estável), comparar dois pontos de parada @ contagens de ciclo
LIGEIRAMENTE diferentes (~46.6M vs ~47.17M) naturalmente mostra PCs
diferentes, sem que isso signifique divergência real. Conferido:
`debug_compare` no vblank cujo `master_cycles` bate exatamente com o
que o `debug_bp` reportou mostra o MESMO PC nos dois lados.

A suspeita sobre o "ruído" do Achado #14 (`master_cycles >> 21`) sendo
uma fonte de instabilidade **fica sem evidência de sobra** por ora — não
está descartada como fonte de sensibilidade real a mudanças de INPUT
(pressionar um botão em frames diferentes ainda pode, em teoria, causar
trajetórias diferentes de propósito — isso é esperado), só não é a
causa da divergência entre ferramentas que motivou a investigação. Fica
como TODO de baixa prioridade revisitar se algo parecido aparecer nesta
investigação de novo, mas SEM tratar isso como um problema confirmado.

O texto original do Achado #33 abaixo é mantido pra registro histórico
do raciocínio (inclusive o bug real do RTC no `debug_bp`, que
CONTINUA corrigido e válido), mas a conclusão final é a acima.

Comparando `debug_bp` (loop manual) e `debug_screenshot` (via
`neogeo_run_slice`, mesmo caminho do frontend/SDL2) pro MESMO estado
(chegar em `$C190DE`), achamos que `debug_bp` chegava lá e
`debug_screenshot` não — reabrindo um mistério documentado bem no
início desta investigação de boot ("`debug_boot`/`neogeo_run_slice` e
`trace_boot`/loop manual produzem trajetórias DIFERENTES apesar de
lógica aparentemente idêntica").

**Causa raiz #1 (real bug, corrigido)**: `debug_bp.c` usava a lógica
VELHA do pulso de RTC (Achado #14: contagem de 30 vblanks) em vez da
real baseada em ciclos (Achado #24: `rtc_next_toggle_cycles`) que
`neogeo_run_slice` já usa. Corrigido (mesma lógica nos dois agora).

**Causa raiz #2 (mais profunda, ainda não resolvida)**: mesmo depois do
fix acima, as trajetórias AINDA divergem. Instrumentamos as duas
ferramentas pra imprimir `master_cycles` no mesmo ponto: `debug_bp`
chega em `$C190DE` com `master_cycles≈47.17M`; `debug_screenshot`, no
vblank mais próximo em ciclos (`230`, `master_cycles≈46.63M` — só
~1.2% de diferença), está num PC completamente diferente (`$C18394`,
não `$C190DE`). Ou seja: uma diferença pequena e -esperada- na contagem
exata de ciclos (por causa de como cada ferramenta fatia o trabalho —
`debug_screenshot` chama `neogeo_run_slice` uma vez por vblank, `debug_bp`
roda instrução a instrução sem fatiar) muda a TRAJETÓRIA INTEIRA do
boot depois de um certo ponto.

Suspeita forte (não confirmada ainda): o "ruído" artificial que
injetamos em `neogeo_status_b()` (Achado #14: bit0 pseudo-aleatório
baseado em `master_cycles >> 21`, criado pra destravar um polling que
esperava uma borda que nunca vinha) é EXATAMENTE esse tipo de fonte de
sensibilidade — pequenas diferenças de `master_cycles` na hora exata em
que o jogo faz polling de `STATUS_B` mudam o bit de ruído lido, e como
esse bit alimenta uma decisão de boot, a trajetória inteira diverge daí
pra frente (efeito borboleta). Isso explicaria por que várias
investigações desta sessão (incluindo a de hoje) pareceram
"instáveis"/difíceis de reproduzir exatamente.

- [ ] Confirmar a suspeita: logar toda leitura de `STATUS_B` (endereço,
      valor do bit de ruído, `master_cycles` no momento) nas duas
      ferramentas e comparar onde exatamente elas leem valores
      diferentes
- [ ] Se confirmado, considerar substituir o "ruído" do Achado #14 por
      algo determinístico e independente de como o trabalho é fatiado
      (ex.: baseado em `vblank_count` em vez de `master_cycles` bruto,
      ou um contador dedicado incrementado só uma vez por vblank) — mais
      fiel a qualquer coisa real (hardware não "nota" como o emulador
      organiza internamente os batches de instrução) e devia tornar os
      resultados 100% reprodutíveis entre ferramentas de novo
- [x] ~~Até resolver isso, tratar qualquer trajetória...~~ resolvido, ver
      correção acima — não havia instabilidade real.

**Confirmado com `debug_screenshot` de verdade** (não só `debug_bp`): no
vblank 232 a tela está preta (transição, normal), mas por volta do
vblank 280 **o sistema já voltou a mostrar o MESMO menu de operador**
(HARDWARE TEST/EXIT etc, idêntico pixel a pixel) — sem termos dado
nenhum input. Ou seja: `$C18F74`/tela de crédito (Achado #32) é
TRANSITÓRIA — sem pressionar START/moeda a tempo, o sistema volta
sozinho pro menu (padrão clássico de attract mode de arcade: mostra tela
de crédito, espera input, timeout, volta pro menu/self-test).

- [ ] Simular START (ou moeda) durante a janela em que `$C190DE`/
      `(a97,A5)` está esperando — hoje NÃO temos como fazer isso: nosso
      `p1_buttons` só cobre Up/Down/Left/Right/A/B/C/D (8 bits), e
      START/SELECT/COIN no Neo-Geo real ficam noutro lugar (bits baixos
      de `neogeo_status_a()`, hoje hardcoded em "nada pressionado" —
      `v=0x9Fu` sempre). Precisa adicionar um jeito de simular
      START/COIN nessas ferramentas de debug antes de tentar de novo
- [x] Simulamos START forçando `wram[(a97,A5)]` bit4 diretamente (novo
      `NEOGEO_START_FROM_VB` em `debug_screenshot.c`, e `NEOGEO_FORCE_START`
      em `debug_bp.c`) — **bug de conta encontrado e corrigido no processo**:
      `(a97,A5)` é deslocamento `$0A97` (3 dígitos), não `$97` — endereço
      absoluto certo é `$10FD97` (offset de wram `$FD97`), não `$F397`
      como escrevemos na primeira tentativa (nenhum efeito visível, o que
      nos alertou pro erro). Corrigido, confirmado com `debug_bp`: D0 lê
      `0x10` de verdade e o `bne $c1927e` É tomado.
- [x] `$C1927E` desmontado: `pea ($c19072,PC)` (empilha retorno manual pro
      início do mesmo loop) seguido de OUTRO dispatch por tabela de saltos,
      dessa vez indexado por `(9d8,A5)` (a mesma variável "número de
      cabinet/slot" reaproveitada, zerada por padrão nesse ponto). Ou
      seja: **pressionar START sozinho só reconfirma o loop e volta pro
      mesmo lugar** (bate com o observado: por vblank 250 o PC já tinha
      convergido de volta pro mesmo estado de quando não apertamos nada) —
      falta alguma outra condição, quase certamente uma CONTAGEM DE
      CRÉDITO (moeda inserida) pra `(9d8,A5)` sair de 0 e essa segunda
      tabela escolher a entrada de "realmente começar o jogo" em vez de
      "continuar esperando"
- [ ] Simular MOEDA/crédito: achar onde fica o contador de créditos real
      (suspeita: `(9d8,A5)` mesmo, ou um campo próximo) e o que incrementa
      ele — provavelmente os bits de coin-in de `neogeo_status_a()`
      (hoje hardcoded em "nada pressionado", `v=0x9Fu`, bits4-0 sempre 1)
      precisam de um jeito de simular moeda igual fizemos com START
- [ ] Depois de simular moeda+start juntos: conferir se a segunda tabela
      de `$C1927E` (indexada por `(9d8,A5)`) realmente leva pro primeiro
      frame de gameplay

### Nota: texto quebrado no rodapé da tela de menu ("dia da semana" ao lado da data) — NÃO é bug de decode, é VRAM nunca escrita
Usuário reparou (rodando o frontend SDL2 ao vivo) um texto ilegível colado
em "15/??/??", no rodapé de várias telas de self-test/menu de operador.
Dump do fixmap (`NEOGEO_DUMP_FIXMAP_ROWS`/`_COLS`, novo, faixa
configurável) nessa área mostra códigos de tile como `0x1A7`, `0x1C1`,
`0x1E5`, `0x100` (repetido várias vezes) — nenhum corresponde a um
caractere ASCII válido do "font grande" (Achado #27/#28), e o padrão não
muda nem um pouco entre vblank 300 e vblank 8000 (várias voltas inteiras
do ciclo de self-test). Ou seja: **não é bug de decode da fix layer**
(que já sabemos que está certo, Achado #28) — é uma região de VRAM que a
BIOS never chega a escrever nesse fluxo, com sobra de qualquer coisa que
estava lá antes (input de outro raciocínio: essa área ficaria a
`SUN/MON/TUE/...` do dia da semana no ajuste de calendário; só que na
tela do menu de operador ela não deveria nem aparecer — pode ser
vazamento visual de outra tela, sintoma indireto de estarmos pulando
parte da inicialização normal via Achados #26/#31).

- [ ] Baixa prioridade: achar o que deveria limpar/escrever essa região
      (provável candidato: alguma rotina de "day of week" chamada só
      quando entra de verdade na tela de calendário) e ver se roda-la
      manualmente (ou não pular a inicialização que a chama) resolve

### Achado #35: REVERTIDOS os Achados #31/#31b (levavam pra outro subsistema de self-test, não pro jogo) — tentativa de bypass pragmático em `$C1B848`
Desmontando mais fundo `$C1927E` (Achado #32 continuação: `pea
($c19072,PC)` + dispatch por `(9d8,A5)`, valores 0-6 ajustáveis por
LEFT/RIGHT antes de START), veio à tona que TODA a ramificação alcançada
via `$C11D88→$C11028` (nosso bypass do Achado #31) é um SELETOR DE TESTE
de hardware (0-6 = qual teste específico rodar), não uma tela de crédito
nem o jogo — confirmado testando `idx=6` e vendo as MESMAS telas de
self-test (RED/GREEN/BLUE/WHITE, SOUND TEST) que já conhecíamos.
**"Cabine linkada detectada" nesse contexto provavelmente significa "modo
de diagnóstico de link", não "pula config, vai pro jogo"** como
assumimos.

Revertido: `neogeo_status_a()` voltou pra bit2=0 (`0x9Bu`), e os hooks de
PC `$C11D66`/`$C123F0` (Achados #31/#31b) foram REMOVIDOS de
`neogeo_run_slice`. Isso nos leva de volta pro caminho original (Achado
#26/#29): self-test → CABINET/SLOT → confirmar com 4 direções → `$C1B848`
(dead-end do Achado #29).

**Tentativa pragmática**: em vez de adivinhar o endereço de destino
"real" depois do dead-end, simulamos um RTS de verdade assim que o PC
bate em `$C1B848` — lê o endereço de retorno do topo da pilha (`A7`) e
salta pra lá, incrementando `A7` em 4, exatamente como a instrução `rts`
faria (mais seguro que inventar um PC arbitrário, já que pelo menos
respeita a convenção de pilha real).

**Resultado**: NÃO trava mais, mas também não chega no jogo — o sistema
executa mais um pouco e acaba se auto-corrompendo (provavelmente um
endereço de retorno sem sentido nesse ponto da pilha, já que não sabemos
o contexto real de chamada) até o CORE DO 68000 disparar sua própria
recuperação de exceção (mecanismo já existente em `m68k_core.c` pra
evitar loops de handler inválido) — na prática isso se comporta como um
**reset completo**: o boot recomeça do zero e chega de novo na tela
CABINET/SLOT (~1830-1870 vblanks depois), pedindo confirmação de novo.
Testado confirmando as 4 direções NAS DUAS janelas (`debug_screenshot`
ganhou suporte a `NEOGEO_DIRECTION_SEQUENCE` com lista separada por
vírgula) — o padrão se repete: confirma, avança um pouco, reseta nele
mesmo, volta pro CABINET/SLOT.

- [ ] Isso é um LOOP DE RESET, não uma trava — melhor que travar pra
      sempre, mas ainda não é o jogo. Provavelmente vamos ter que mesmo
      assim descobrir o destino REAL depois do dead-end (o que exige
      entender o formato de ~9KB do memory card carregado logo antes,
      ou achar onde exatamente o "return" deveria ir revisando o que
      $C1871E realmente faz por dentro — ainda não desmontado)
- [ ] Considerar logar CADA vez que o hack do Achado #35 dispara (contador),
      pra confirmar quantos ciclos de reset acontecem e se o padrão
      realmente é idêntico toda vez ou varia
- [ ] Voltar a considerar a hipótese mais simples: talvez falte mesmo um
      campo "cabinet já configurado" no formato de memory card que faça
      o jogo pular a tela de configuração inteira (nunca chegando no
      dead-end) — investigar ISSO em vez de tentar adivinhar o destino
      pós-dead-end

**CONFIRMADO** (testando confirmar CABINET/SLOT em DUAS janelas
consecutivas, vblank 1800 e 3630): é mesmo um loop de reset genuíno, não
um acaso — a segunda passada mostra a MESMA tela "CABINET No. 0/SLOT 1"
de novo (não confirmada, esperando input de novo), ou seja o
auto-reset acontece ANTES de sequer redesenhar a tela seguinte. O
bypass do Achado #35, por enquanto, só troca "trava pra sempre" por
"reseta pra sempre" — nenhum dos dois chega no jogo.

**ACHADO #35 DESCARTADO, causa raiz do dead-end esclarecida**: instrumentamos
`neogeo_run_slice` (`NEOGEO_DEBUG_C1871E=1`, novo) pra imprimir o estado
completo (registradores + conteúdo real da pilha) bem no instante em que
o PC bate em `$C1B842`/`$C1B848`. Resultado: **`A7 == A5 == $10F300`, e
os 8 bytes lidos do topo da pilha são todos zero** — ou seja, a pilha
está genuinamente VAZIA nesse ponto (bate com `$C11148:
movea.l #$10f300,A7` visto bem no início desse mesmo fluxo, que reseta
A7 pro mesmo valor de A5). Isso prova que o `jsr $c1871e` em `$C1B842`
**já retornou normalmente** (o próprio `rts` de dentro de `c1871e` já
rodou) ANTES de cairmos no loop — `$C1B848` não é um "retorno pendente"
escondido, é o PC seguinte de verdade depois de uma chamada que já
terminou. Não existe "destino real" escondido pra descobrir: **é um HALT
proposital, com pilha vazia, sem chamador pendente**. Forçar mais um RTS
ali (Achado #35) só pulava pro endereço $000000 (lixo), causando o loop
de reset — removido do código.

**Conclusão**: segurar as 4 direções simultâneas pra confirmar
CABINET/SLOT (Achado #26) quase certamente NÃO é o gatilho real que um
jogador (ou mesmo um técnico) usaria — leva a um beco sem saída
genuinamente proposital no código (provavelmente um teste de fábrica ou
um estado de erro "hardware não devia chegar aqui por esse caminho na
prática"). A saída de verdade da tela CABINET/SLOT deve ser outra coisa
— possivelmente nem envolve confirmar manualmente (um memory card já
"configurado" pode simplesmente pular a tela inteira, nunca entrando
nesse ramo em primeiro lugar).

- [ ] Próximo passo mais promissor: procurar o que faz a tela
      CABINET/SLOT NUNCA APARECER (pular direto pro jogo) em vez de
      tentar confirmá-la — revisitar o scan de `backup_ram+$124`
      (Achado #31) com essa lente: talvez não seja sobre "cabine
      linkada" de verdade, mas sim sobre "já tem uma config de cabinet
      salva, não precisa perguntar de novo" — reexaminar `$C11DB0` com
      mais cuidado (o que exatamente ele compara em `cmpm.b` no
      `$C11E08`, contra que "assinatura")
- [ ] Alternativa: aceitar que essa investigação específica bateu no
      fundo do poço sem mais documentação externa, e considerar as
      opções já colocadas antes pro usuário (Monitor Test Tool, ou
      pausar aqui)

### Achado #36: bit0 do "ruído" do STATUS_B é P1 START de verdade — RESOLVIDO (e o "ruído" do Achado #14 foi removido)
A pedido do usuário ("me ajuda a destravar dessa parte"), reexaminamos a
tela **I/O CHECK** (já renderizada corretamente desde o Achado #28) e
reparamos que ela mostrava `START 1` pro P1 exatamente no momento em que
nosso "ruído" do Achado #14 tinha zerado o bit0 do STATUS_B. A própria
BIOS nos disse o que aquele bit é: **P1 START**, não ruído elétrico.

Reescrito `neogeo_status_b()`: removido o hash pseudo-aleatório baseado em
`master_cycles`, bit0 agora reflete `debug_force_start_frames` (campo novo
em `neogeo_t`) — 0 por padrão (START solto), força 0 (pressionado,
ativo-baixo) quando uma ferramenta pede. `debug_screenshot` ganhou
`NEOGEO_START_FROM_VB`/`NEOGEO_START_REPEAT` pra simular alguém apertando
START de verdade. **Confirmado**: com isso, `wram[$FCD8]` (a máquina de
estados que ficava travada pra sempre no "estado 1" desde o Achado #14)
avança de verdade quando simulamos o toque.

### Achado #37: REG_STATUS_A e REG_STATUS_B tinham valores errados — corrigidos com dados MEDIDOS no MAME real
Montamos um romset compatível (`third_party/mame/roms/`, nomes com
extensão de chip) e rodamos o **MAME de verdade** com o mesmo cart/BIOS,
usando o debugger dele (`dump arquivo,endereco,tamanho`) pra medir os
registradores direto, em vez de continuar adivinhando. Resultado:

| registrador | nosso valor (antes) | MAME real (medido) |
|---|---|---|
| `$380000` STATUS_B | `0x8F` | **`0xFF`** |
| `$320001` STATUS_A | `0xBF`/`0x9F` | **`0x5F`** |

Bit5 de STATUS_A ("sense: 4-slot") e bit7 (dado serial do RTC, µPD4990A —
não implementado, deixamos em 0 igual o MAME no momento medido) estavam
errados. STATUS_B bits5-4 (que Achado #26 forçava pra "cartão de memória
presente") também — com SERVICE solto (Achado #36) essa checagem nem
roda mais, não tem motivo pra fingir cartão.

### Achado #38: a BIOS decide "mesmo dia" vs "dia novo" comparando a DATA DO RTC contra a última guardada na NVRAM — não tínhamos RTC nenhum
Rastreando por que o scan de `backup_ram+$124` (Achado #31) sempre achava
tudo zerado, achamos em `$C11E0A: cmpm.b (A0)+,(A1)+` uma comparação de 3
bytes entre `(ad2,A5)` (a data ATUAL, preenchida por uma leitura de RTC em
`$C12C78`) e `$D00104` (a data do ÚLTIMO BOOT, salva na NVRAM). Iguais =
"mesmo dia", mantém a tabela de estatísticas (o normal); diferentes =
"dia novo", desloca/zera a tabela — exatamente o comportamento que
víamos.

Medido com `debug_bp` bem em cima da instrução: nosso `(ad2,A5)` = `00 00
00` sempre (não implementamos o µPD4990A ainda), enquanto a NVRAM real diz
`26 09 11`. Nunca bate. Como ainda não temos o RTC de verdade, zeramos a
data salva na NVRAM no reset (`neogeo_reset`) pra simular "um relógio que
nunca foi acertado" — bate com a realidade (não implementamos o chip) sem
mexer em nenhuma lógica de código.
- [ ] TODO de verdade: implementar o µPD4990A (protocolo serial em
      `$380051`/`io_control_w` case 0x28 do MAME: bit0=dado, bit1=clock,
      bit2=strobe; leitura do dado em STATUS_A bit7, pulso em bit6)

### Achado #39: A P ROM inteira estava sem byte-swap, sem troca de metades, E `$000080-$0FFFFF` respondia como BIOS em vez de cartucho
O maior dos bugs. Rastreando por que a tabela de estatísticas (que sabemos
que é indexada pelo ID do jogo) nunca batia, achamos que a BIOS lê o ID
do cartucho em `$C11F76: move.w $108.w,...` — endereço **`$000108`**.
Isso é dentro da faixa que SEMPRE achamos que fosse BIOS (`$000000-
$0FFFFF`) — mas não é: no MAME real (`set_slot_idx` em `neogeo.cpp`),
`space.install_rom(0x000080, 0x0fffff, cart_rom + 0x80/2)` — **só os 128
bytes da tabela de vetores (`$000000-$00007F`) alternam entre BIOS e
cartucho; de `$000080` em diante é SEMPRE a P ROM do cartucho.**

Medido no MAME real (`save`/`dump` do debugger): `$000100` = literalmente
os bytes `"NEO-GEO" 00 82 ...` — o cabeçalho do cartucho, com o NGH
(`0x0082` = Double Dragon) certinho. No arquivo `082-p1.rom`, achamos essa
mesma string **só depois de duas transformações**: byte-swap por word
(o MAME carrega com `ROM_LOAD16_WORD_SWAP`) E troca de metades pra P ROMs
> 1MB (`ROM_LOAD16_WORD_SWAP(.... 0x100000, 0x100000) / ROM_CONTINUE(
0x000000, 0x100000)` — o SEGUNDO mega do arquivo é o que fica no
COMEÇO do espaço de endereço do cartucho). Não fazíamos nem o swap nem a
troca de metades, e mapeávamos a faixa toda pra BIOS por cima disso —
três bugs empilhados, todos corrigidos:
1. `rom_loader.c`: byte-swap da P ROM inteira após carregar.
2. `rom_loader.c`: troca das metades se `p_size > 1MB`.
3. `neogeo.c`: `is_bios_addr` agora só cobre `$000000-$00007F` (+ o
   espelho de código em `$C00000-$C1FFFF`); nova `is_cart_low_addr`
   cobre `$000080-$0FFFFF` lendo de `rom.p`.

**Resultado imediato**: a tabela de estatísticas passou a bater com o
MAME byte a byte, e o `idx` (estado do boot) mudou de comportamento —
sinal de que a BIOS finalmente reconhece o cartucho.

### Achado #40: bit1 do latch de sistema troca os VETORES de interrupção pro cartucho
`$C11FAA: move.b D0,$3a0013` — mesmo latch HC259 do Achado #27, dessa vez
bit1 (`use_cart_vectors` no MAME real). A BIOS seta esse bit pouco antes
de entregar o controle pro jogo, pra que a ISR de vblank (e as outras
interrupções) passem a ser as do CARTUCHO, não as da BIOS. Implementado:
campo novo `use_cart_vectors` em `neogeo_t`, tratado no mesmo
`system_latch_write` do Achado #27; leitura de `$000000-$00007F` passa a
vir de `rom.p` quando o bit está setado.

### Achado #41: o latch de sistema SÓ era tratado em escritas `.W` — mas o código real usa `.B` (achado que fez os Achados #27/#40 nunca dispararem de verdade)
Ao investigar por que `use_cart_vectors` não parecia surtir efeito,
achamos que `m68k_write8_cb` (escritas de 1 byte) **nunca verificava a
faixa `$3A0000-$3A001F`** — só `m68k_write16_cb` tratava. Como TODO
código real que vimos escrever nesse latch usa `move.b` (nunca
`move.w`), isso significa que o mecanismo do Achado #27 (fix layer
BIOS/cartucho) e do Achado #40 (vetores) **nunca tinham disparado de
verdade em nenhum teste anterior** — só "funcionavam" porque os valores
default (0) coincidiam com o que as telas de BIOS precisavam. Extraída a
lógica pra uma função `system_latch_write()` compartilhada, chamada dos
dois caminhos de escrita (`.b` e `.w`).

**Efeito colateral do Achado #39 pego pelos testes**: `is_bios_addr`
mudar de `< 0x100000` pra `< 0x80` quebrou `test_neogeo.c` (que colocava
seu programa sintético em `$000400`, agora território do cartucho) —
corrigido movendo o programa de teste pro espelho de código real da BIOS
(`$C00400`), sem alterar a checagem em si (era o teste que estava errado
pro hardware real, não o código).

**Confirmado com `debug_bp`**: depois de todos os achados #36-#41, o boot
agora alcança de verdade o vetor de vblank do CARTUCHO (`$005F56`,
calculado a partir do header lido do próprio cart) — algo que nunca tinha
acontecido em nenhuma sessão anterior.

- [ ] **Ainda não resolvido**: mesmo com o vetor do cartucho rodando por
      frame, a tela continua mostrando o MENU DE OPERADOR (HARDWARE
      TEST/EXIT/etc), não o logo do jogo. `idx` (`wram[$FCD8]`) fica em
      `0000` de forma estável (não `0001` como antes) — ou seja,
      mudamos de qual trava especificamente, mas ainda tem uma trava.
      Suspeita mais forte: o menu de operador é um DISPATCH SEPARADO
      (indexado por `(a81,A5)`, achado no Achado #32) — precisa achar de
      novo o que decide entrar nesse dispatch specific e por que ainda
      entramos nele com todos os valores agora medidos/corretos
- [ ] Considerar logar toda vez que `(a81,A5)` muda de valor e de onde,
      pra achar a real condição de entrada no menu de operador
- [ ] Verificar se o vetor de vblank do cartucho (`$5F56`) está fazendo
      algo útil (redesenhando algo) ou se é só um handler mínimo/stub
      nesse ponto do jogo — comparar contra o comportamento esperado

### Achado #41: O JOGO BOOTA. A chave TEST estava com a lógica invertida.
**Marco: pela primeira vez o emulador entrega o controle ao cartucho e roda
o attract mode do Double Dragon.**

Sintoma que restava: mesmo depois dos Achados #36-#40 (SERVICE solto, DIP de
"setting mode" desligada, P ROM com byte-swap + troca de metades, espelho
baixo do cartucho em $000080-$0FFFFF, vetores do cartucho), o boot sempre
terminava no MENU DE OPERADOR (HARDWARE TEST / … / EXIT) em vez de ir pro
jogo.

Como achamos: aumentamos o histórico de PCs do `debug_bp` de 8 pra 64
entradas e paramos em `$C18F74` (a entrada desse menu). A cadeia completa
apareceu de uma vez:
```
005F56…        ISR de vblank DO CARTUCHO
C00438 C11646  salta pra ISR da BIOS
C1167A C11684  housekeeping por frame
C126B4 … C12762
     -> C18F74  (menu de operador)
```
E o ponto exato da decisão, em `$C12756`:
```
C12744: not.b  D0            ; inverte a porta TEST ($300081)
C12752: move.b D1,($bdf,A5)  ; D1 = bordas de SUBIDA do input
C12756: tst.b  ($a81,A5)
C1275A: beq    $c12768
C1275C: btst   #$7, D1       ; borda no bit7 = "TEST acabou de ser apertado"
C12760: beq    $c12768
C12762: jmp    $c18f74.l     ; -> MENU DE OPERADOR
```
De quebra isso explicou o que o menu É: os 7 itens da tela são os índices
0-6 da tabela de saltos em `$C19290` que já tínhamos decodificado
(entrada 0 = `$C17062` = "HARDWARE TEST"), e o "loop de espera de input" em
`$C190DE` (Achado #32) é o loop do cursor desse menu — não um travamento.

**Causa raiz**: `NEOGEO_SYSTYPE_DEFAULT` estava `0x3F`, com o comentário
afirmando "bit7=0 (nao e test)". O fonte do MAME é explícito:
```c
PORT_START("TEST")
PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_CUSTOM )   // sense: JAMMA/MVS
PORT_SERVICE_NO_TOGGLE( 0x0080, IP_ACTIVE_LOW )  // <- chave TEST
```
ATIVO-BAIXO: bit7=1 = solta, bit7=0 = PRESSIONADA. Estávamos simulando o
botão TEST eternamente apertado, e a BIOS fazia exatamente o correto.
Mesmíssimo erro de polaridade do SERVICE (Achado #36) — vale a regra:
**no Neo-Geo, entrada de botão/chave é quase sempre ativo-baixo; 0 = apertado.**
Corrigido pra `0xBF` (bit7=1 solta, bit6=0 = 2 slots, que bate com o
`$D00047 = 2` medido no MAME).

**Validação** (`debug_screenshot`, mesmo save real do MAME):
- índice de estado: vb300 `idx=0003`, vb500 `idx=0007`, vb900 `idx=0008` —
  **idêntico ao MAME real** (frames 300/500/800 = 3/7/8, medido no Achado #37)
- vb300: logo **SNK** na tela
- vb700: `pc=006022`, `a5=108000` — PC dentro da P ROM do cartucho e A5 no
  work RAM do jogo: **handoff pro cartucho aconteceu**
- vb1100: attract mode do Double Dragon, com "INSERT COIN" e o logo da
  Imperial Entertainment desenhados pelo código do próprio jogo

Também ficou explicada a pergunta do usuário sobre "o logo do Neo-Geo não
aparece": não era bug de camada — a gente nunca chegava nessa parte do boot.

- [x] Sprites: agora SIM dá pra validar o decode (Achados #16/#21) contra
      conteúdo real. No attract mode já dá pra ver problemas (uma faixa de
      sprite deslocada na esquerda, fundo faltando) — primeira vez que temos
      dado real pra comparar contra o MAME frame a frame — **RESOLVIDO,
      Achado #42, ver abaixo**
- [ ] Conferir se o bankswitch de P ROM >1MB ($2FFFF0-$2FFFFF) está certo
      agora que o jogo roda de verdade (Double Dragon tem 2MB de P)
- [ ] Implementar o µPD4990A (RTC) de verdade e remover a gambiarra do
      Achado #38 (zerar a data salva em backup_ram[0x104..0x106])
- [ ] Bloco de sprite corrompido no attract mode do Double Dragon (vb~1200),
      AGORA na posição certa (Achado #42 resolveu o posicionamento) mas com
      conteúdo de tile ainda errado/embaralhado — provavelmente auto-animação
      (bits 2-3 do attr da SCB1, substituição de código por
      `auto_animation_counter`, NÃO implementada) ou endereçamento de C ROM
      pro tamanho maior do cart do Double Dragon. Investigar em seguida.

### Achado #42: dois bugs de POSIÇÃO de sprite (X nos bits errados, Y com
constante errada) + bit de encadeamento ("sticky") nunca implementado —
causa raiz da "faixa esquerda corrompida" E do logo "NEO·GEO" nunca aparecer

Usuário colocou outra ROM pra testar (Neo Bomberman, `roms/neobombe`) e,
comparando com uma foto de referência real do boot do Neo-Geo, apontou que
o logo animado "NEO·GEO" (que fica ACIMA de "MAX 330 MEGA / PRO-GEAR SPEC /
SNK", tudo na mesma tela) nunca aparecia no nosso emulador — só o resto.
Isso é o MESMO problema relatado bem no início da sessão anterior
("na bios o logo do neo geo nao apareceu") que uma investigação anterior
tinha erroneamente dado como resolvido (concluindo que o logo "SNK" sozinho
já era o esperado — estava errado, faltava o wordmark "NEO·GEO" inteiro).

Comparamos `neogeo_render_sprites` (`core/src/video/lspc_sprites.c`) contra
a fonte real do MAME (`neosprite_base_device::draw_sprites`, já baixada em
`/tmp/mame_src/neogeo_spr.cpp` de uma sessão anterior) e achamos:

1. **X errado**: líamos `x_word & 0x1FF` (SCB4, bits BAIXOS). O hardware
   real usa `x_word >> 7` (bits ALTOS, 9 bits a partir do bit 7):
   ```cpp
   x = m_videoram_drawsource[0x8400 | sprite_number] >> 7;
   ```
2. **Y com constante errada**: usávamos `496 - raw_y`. O valor real é
   `0x200 (512) - raw_y`:
   ```cpp
   y = 0x200 - (y_control >> 7);
   ```
3. **Bit de encadeamento ("sticky", bit6 da SCB3/y_control) nunca
   implementado** — o comentário no topo do arquivo já avisava isso
   explicitamente ("NÃO implementados"). No hardware real, quando esse bit
   está setado, o sprite NÃO tem X/Y/altura próprios — ele continua
   exatamente de onde o sprite anterior parou:
   ```cpp
   if (BIT(y_control, 6)) {           // encadeado
       x = (x + zoom_x + 1) & 0x01ff; // continua do anterior
       zoom_x = (zoom_control >> 8) & 0x0f;
   } else {                            // bloco novo
       y = 0x200 - (y_control >> 7);
       x = m_videoram_drawsource[0x8400 | sprite_number] >> 7;
       zoom_y = zoom_control & 0xff;
       zoom_x = (zoom_control >> 8) & 0x0f;
       rows = y_control & 0x3f;
   }
   ```
   Logos largos (como "NEO·GEO", feito de ~18 sprites de 16px encadeados
   lado a lado) dependem inteiramente desse mecanismo pra não precisar
   escrever X pra cada sprite individualmente. Sem ele, TODO sprite
   encadeado colapsava pra o X cru (garbage, geralmente perto de 0) — essa é
   a causa EXATA da "faixa vertical esquerda corrompida" reportada pelo
   usuário tanto em Double Dragon quanto (recém confirmado) em Neo Bomberman.

Reescrevemos `neogeo_render_sprites` pra processar sprites em ORDEM
(0..380), carregando um estado (x/y/zoom_x/zoom_y/altura) que persiste
entre sprites encadeados, replicando a lógica acima (inclusive o "skip se
x cai na faixa 0x140-0x1F0, totalmente fora da tela" e o wraparound de X
pra sprites parcialmente visíveis saindo pela esquerda, `cur_x > 0x1F0`).

**Validação**: depois do fix, o logo "NEO·GEO" aparece — IDÊNTICO — tanto
no Neo Bomberman quanto no Double Dragon no mesmo vblank (~650) da tela de
boot da BIOS (confirmando que é um recurso compartilhado da BIOS, não algo
específico de jogo), inclusive o símbolo © que antes ficava cortado fora da
tela agora aparece inteiro (prova indireta de que o X agora é maior/mais
correto, não mais grudado nos primeiros pixels). Isolamos ainda os tiles
crus de uma coluna do logo (`debug_tile_grid`, ferramenta nova) e
confirmamos que formam uma letra "N" reconhecível (perna vertical /
diagonal / perna vertical) — o DECODE do tile já estava certo desde os
achados #16/#21, só a posição estava errada.

Ferramenta nova: `core/tools/debug_tile_grid.c` — renderiza uma grade (ou
lista explícita) de tiles de sprite crus (16x16, C ROM), sem nenhum
posicionamento/zoom envolvido, só pra inspecionar visualmente se o decode
de um tile faz sentido isoladamente. Aceita `tile_inicio,qtde` OU uma lista
explícita `"0,e,1d,2c"` (útil pra empilhar tiles específicos de uma coluna
de sprite e comparar contra o que o renderizador real produz).

TODO (achado colateral, não resolvido ainda): depois do fix de posição, o
Double Dragon revelou um BLOCO de sprite (área ~80x100px, por volta de
vb~1200, já em pleno attract mode do jogo) agora na posição correta, mas
com conteúdo de tile visivelmente errado/embaralhado (mistura de verde,
marrom, branco em blocos de 16x16 nitidamente desalinhados uns dos outros)
— aparenta ser um personagem/cenário do jogo, mas os CÓDIGOS de tile lidos
devem estar errados. Suspeita inicial: auto-animação (bits 2-3 do attr da
SCB1 substituindo os bits baixos do código por um contador incremental,
usada por muitos efeitos de "fogo"/"água" animados) ainda não implementada
em `neogeo_render_sprites`, ou algum problema de endereçamento de C ROM
específico do tamanho maior do cart do Double Dragon. Próximo passo.

### Achado #43: bug de verdade no DECODE do tile de sprite (metade
esquerda/direita trocada + ordem de bit invertida) — validado byte-a-byte
contra um screenshot REAL do MAME

Usuário testou vários jogos depois do Achado #42 e reportou: "os sprites
estão sendo colocados corretamente, porém parecem desalinhados ou
invertidos... parece que está errando a posição da memória". Isso bateu
com o que eu already tinha visto no "NEO·GEO" (letras sobrepostas/
espelhadas) mas que eu tinha (erroneamente) atribuído a "só é uma fonte
bold". O usuário estava certo, eu estava errado.

Pra resolver de vez em vez de continuar especulando, usamos o MAME real
como ground truth: copiamos o romset do Double Dragon pra
`third_party/mame/roms/doubledr` (formato MAME) e rodamos
```
mame.exe doubledr -bios euro -debug -debugscript scripts/snap650.txt \
  -window -nomaximize -str 40 -rompath roms -skip_gameinfo -nothrottle
```
com um script de 650 `gvblank` + `snap` (mesmo vblank que usamos pra
comparar) — gerando um PNG real (`snap/doubledr/0003.png`) do "NEO·GEO"
correto. Comparado lado a lado, nosso render (mesmo depois do Achado #42)
mostrava letras erradas/sobrepostas (uma "▶" solta no meio de onde devia
ser um "E", por exemplo — o MESMO tipo de artefato que motivou o Achado
#21 numa sessão anterior).

Em vez de ficar testando hipóteses recompilando a cada tentativa, escrevemos
um script Python que lê os bytes CRUS dos arquivos de C ROM diretamente
(sem passar pelo nosso C) e testa hipóteses de decode, comparando contra os
pixels reais extraídos do PNG do MAME (crop de 16x16 por tile, ASCII-art
`#`/`.` por brilho). Achamos DOIS bugs, confirmados também contra o código
fonte real do MAME (`neosprite_optimized_device::optimize_helper`,
`neogeo_spr.cpp`):

1. **Metade errada**: a metade ESQUERDA da tela (x<8) deve usar a SEGUNDA
   metade dos 64 bytes do tile (offset 32-63), não a primeira. Tínhamos ao
   contrário desde o Achado #21 (que corrigiu a estrutura de blocos mas não
   pegou essa troca).
2. **Ordem de bit invertida**: bit da posição do pixel dentro do byte é
   LSB-primeiro (`bit = x & 7`), não MSB-primeiro (`bit = 7 - (x & 7)`) como
   tínhamos.
```c
/* antes (errado): */
u32 half = (x < 8u) ? 0u : 1u;
u32 bit = 7u - (x & 7u);
/* depois (Achado #43): */
u32 half = (x < 8u) ? 1u : 0u;
u32 bit = x & 7u;
```

**Validação**: depois do fix, decodificamos em Python os MESMOS bytes crus
usando a fórmula corrigida e bateu pixel-a-pixel (a menos de diferença de
threshold/anti-aliasing na conversão do PNG pra ASCII) com os 4 tiles reais
usados pela coluna do "N" do logo. Recompilado o core, o "NEO·GEO"
renderizado agora é visualmente IDÊNTICO ao screenshot real do MAME no
mesmo vblank — comparação lado a lado confirma.

### Achado #44: faltava subtrair o offset de VBLANK (16 linhas) na posição Y

Durante a mesma investigação, percebemos que mesmo comparando o mesmo
vblank, nosso "NEO·GEO" aparecia ~16px mais abaixo na tela que o do MAME
real. O "y" calculado no Achado #42 (`0x200 - (y_control>>7)`) é a linha de
varredura CRUA do LSPC (0 a VTOTAL-1, incluindo as linhas de vblank), não
já recortada pra nossa tela de 224 linhas visíveis — falta subtrair
`NEOGEO_VBEND` (as linhas de vblank ANTES do início da área visível, 16 no
hardware real). Adicionamos essa subtração em `cur_y`. Confirmado
comparando a posição vertical exata contra o mesmo snapshot do MAME usado
no Achado #43 (a faixa de conteúdo do logo bate exatamente com y=52-91 do
MAME depois da correção, contra y=68-107 antes, um erro de 16px).

TODO: o bloco de sprite corrompido no Double Dragon (vb~1200, ver TODO do
Achado #42) CONTINUA corrompido mesmo depois dos Achados #43/#44 — como
esperado, já que aquele bug é de CÓDIGO de tile errado (provavelmente
auto-animação ou endereçamento de C ROM), não de decode/posição. Validado
que os dois bugs são realmente independentes.

### Achado #45: os bits do ATRIBUTO do sprite (word ímpar da SCB1) estavam
quase todos errados — tile alto, hflip e vflip nas posições erradas, e a
auto-animação nunca implementada

Depois dos Achados #43/#44 o usuário testou vários jogos e reportou: "tem
sprites bons e ruins... mas já deu uma boa melhorada". O bloco embaralhado
do attract do Double Dragon (TODO do Achado #42) continuava quebrado.

Voltando à fonte real do MAME (`neosprite_base_device::draw_sprites`):
```cpp
const u16 attr = m_videoram_drawsource[attr_and_code_offs + 1];
u32 code = ((attr << 12) & 0xf0000) | m_videoram_drawsource[attr_and_code_offs];
if (!m_auto_animation_disabled) {
    if (BIT(attr, 3))      code = (code & ~0x07) | (m_auto_animation_counter & 0x07);
    else if (BIT(attr, 2)) code = (code & ~0x03) | (m_auto_animation_counter & 0x03);
}
if (BIT(attr, 1)) sprite_y ^= 0x0f;   // vflip
...
const pen_t *line_pens = &m_pens[attr >> 8 << m_bppshift];   // paleta
if (BIT(attr, 0)) { ... x_inc = -1; }  // hflip
```
Ou seja, o layout REAL da word de atributo é:
| bits  | significado                          | o que tínhamos    |
|-------|--------------------------------------|-------------------|
| 15-8  | paleta                               | igual (certo)     |
| 7-4   | bits 16-19 do código do tile         | usávamos bits 3-0 |
| 3     | auto-animação de 3 bits              | não implementado  |
| 2     | auto-animação de 2 bits              | não implementado  |
| 1     | vflip                                | usávamos bit 5    |
| 0     | hflip                                | usávamos bit 4    |

O erro do "tile alto" era o mais destrutivo: o Double Dragon tem 14MB de C
ROM (códigos de tile até ~0x1C000), então ler o nibble alto do lugar errado
fazia o sprite buscar gráfico numa região completamente diferente da ROM —
exatamente o "bloco embaralhado que parece arte de jogo, mas picotada".

Também implementamos a auto-animação (`auto_anim_counter/speed/disabled` em
`neogeo_t`, config vinda de LSPCMODE, contador andando 1x por frame em
`neogeo_run_slice`). Bits confirmados na fonte real
(`neogeo_base_state::set_video_control`, `src/mame/snk/neogeo_v.cpp` —
arquivo que precisou ser baixado à parte, não está em `neogeo.cpp`):
```cpp
m_sprgen->set_auto_animation_speed(data >> 8);
m_sprgen->set_auto_animation_disabled(BIT(data, 3));
```

**Validação**: o bloco embaralhado do attract virou as FOTOS DIGITALIZADAS
do intro do Double Dragon, nítidas e reconhecíveis (frames vb~1250-1400),
e o logo "Imperial Entertainment" continua correto.

### Achado #46 (armadilha de build, não de emulação): objeto desatualizado
com layout de struct antigo = corrupção de memória silenciosa

Durante o Achado #45 o usuário começou a receber diálogos do runtime do
MSVC: "Run-Time Check Failure #2 - Stack around the variable 'ng' was
corrupted". O sintoma revelador: `master_cycles` (o ÚLTIMO campo do
`neogeo_t`) saía impresso como **0** quando devia estar na casa das
centenas de milhões.

Causa: adicionamos campos novos (`auto_anim_*`) no MEIO do `neogeo_t`, e
algum objeto ficou compilado com o layout ANTIGO (struct menor) — a
ferramenta alocava a struct pequena na pilha enquanto o core escrevia nos
offsets da struct grande, passando do fim da alocação. Os efeitos eram
enganosos: o logo "Imperial Entertainment" sumia, `master_cycles` zerava, e
dava pra perder muito tempo achando que era bug de renderização (eu perdi).

Correção: `ninja -t clean` + rebuild completo. Depois disso `master_cycles`
voltou a 293994510 e o logo Imperial voltou a aparecer.

**Lição**: ao mexer no `neogeo_t` (ou em qualquer header compartilhado),
fazer rebuild LIMPO antes de concluir qualquer coisa sobre o resultado
visual. Um build parcial pode falhar no meio (aconteceu aqui: o link do
`neogeo_sdl2.exe` falhou com LNK1168 porque o emulador estava aberto,
abortando o resto do build) e deixar objetos inconsistentes.

TODO (próximo bug conhecido): por volta do vblank ~1500-1600 o Double
Dragon RESETA — o estado volta a ser idêntico ao do boot inicial
(`pc=c11da2 idx=fcd8 wram[f395]=94`), com a tela toda verde. Não é
watchdog (não implementamos nenhum — escritas em $300001 são ignoradas),
então é o código emulado travando/pulando pro vetor de reset de verdade.
Investigar com `debug_bp` a partir de vb~1450.

### Achado #47: a fix layer usa a linha de VARREDURA CRUA, não a linha da
tela — tudo saía 16px mais baixo e as 2 últimas linhas nunca apareciam

Usuário: "a tela de jogo está mais baixa q o normal também.. cortando
sprites". Fonte real do MAME
(`neosprite_base_device::draw_fixed_layer`):
```cpp
const u16 *video_data = &m_videoram_drawsource[0x7000 | (scanline >> 3)];
```
O índice da linha no tilemap vem do `scanline` CRU (0..VTOTAL-1), e a área
visível começa em `NEOGEO_VBEND` (confirmado em `neogeo_spr.h`: `0x010` =
16). Logo o topo da tela usa a linha `16>>3 = 2` do tilemap, não a 0.

Duas consequências, ambas reportadas pelo usuário:
1. Toda a fix layer saía 16px mais baixa que o correto.
2. As linhas 28-29 do tilemap NUNCA eram desenhadas (nosso laço ia de 0 a
   27) — e é exatamente ali que o Double Dragon põe o "LEVEL-4  CREDIT 00".
   O texto simplesmente não existia na tela.

Corrigido somando `NEOGEO_VBEND_LINES / FIX_TILE_PX` (= 2) ao índice da
linha. Criada a constante `NEOGEO_VBEND_LINES` em `neogeo.h`, usada também
pelo Achado #44 (que já tinha corrigido o mesmo offset do lado dos SPRITES,
com um `- 16` mágico — agora os dois usam a mesma constante documentada).

**Validação** (medida por script, não a olho): contra o snapshot real do
MAME no mesmo vblank, "INSERT COIN" aparece nas linhas 168-174 no MAME e
168-174 no nosso; "LEVEL-4 CREDIT 00" nas 216-222 nos dois. Alinhamento
exato.

### Achado #48: o bankswitch de P ROM estava errado em três pontos — era a
causa do CRASH que impedia o jogo de chegar na tela de título

Sintoma: por volta do vblank ~1475 o Double Dragon "resetava" (tela verde,
estado idêntico ao boot inicial). Era isso que impedia de chegar em
QUALQUER tela com cenário — daí o "falta background" do usuário.

Diagnóstico: colocamos breakpoints nos vetores de exceção da BIOS (lidos do
arquivo da BIOS, lembrando que ele é word-swapped: reset = `$C00402`,
address error = `$C0040E`, etc.) e rodamos `debug_bp` por 39.5M instruções:
```
BREAKPOINT em C0040E na instrucao 39396321: ... a5=2EFFFF ... master_cycles=299214678
  PCs anteriores: ... 020B72 020B74 020B7A 020B80 020B86 020B88 020B8E
```
`$C0040E` é o vetor de ADDRESS ERROR, e `a5 = $2EFFFF` é ÍMPAR — o 68000
não consegue ler word/long em endereço ímpar. O endereço está dentro da
janela de banco da P ROM ($200000-$2FFFFF), ou seja: o jogo leu um ponteiro
corrompido DE DENTRO da janela de banco.

Comparando com `neogeo_base_state::write_banksel` (fonte real do MAME):
```cpp
int bank = data & 0x07;                        // vem do DADO escrito
if ((bank + 1) * 0x100000 >= len) bank = 0;    // limita ao tamanho da ROM
m_bank_base = (bank + 1) * 0x100000;           // (bank+1)!
```
e o handler é instalado com
`install_write_handler(0x2ffff0, 0x2fffff, ...)` — **só escrita**.

Nossos três erros:
1. O banco era derivado do ENDEREÇO escrito, não do DADO.
2. Faltava o `+1`. Esse é o mais grave: o primeiro megabyte já está fixo em
   $000000-$0FFFFF, então o banco 0 da janela precisa mostrar o SEGUNDO
   megabyte. Sem o `+1`, a janela $200000-$2FFFFF devolvia uma cópia do que
   já estava em $000000 — o jogo lia dados/ponteiros errados dali.
3. `check_p_bankswitch()` era chamado também nos caminhos de LEITURA, então
   qualquer leitura em $2FFFF0-$2FFFFF trocava o banco como efeito colateral.

**Validação**: depois do fix, em vb1700 o PC está em `008030` com
`a5=108000` (código do cartucho, sem crash) e a tela de título do Double
Dragon aparece COMPLETA — cenário (monitores, fliperamas, chão), os
personagens, o logo "DOUBLE DRAGON" e o HUD — batendo com o snapshot real
do MAME. O attract segue adiante (vb2100/2500) mostrando as telas de
personagens com sprites grandes e detalhados.

TODO: sobrou uma faixa fina de lixo gráfico na borda DIREITA da tela
(x≈310-320) nas telas de personagem. Suspeita: o tratamento de wraparound
de X (`cur_x > 0x1F0 ? cur_x - 512 : cur_x`) em `lspc_sprites.c`, ou a
tabela real de zoom horizontal (ainda usamos aproximação linear no lugar do
`zoom_x_tables` do hardware). Investigar.

### Achado #49: faltava o SEGUNDO BANCO DE PALETA (bit 7 do latch de sistema)

Procurando outros bugs, olhamos o que os 8 bits do latch HC259 ($3A0000)
fazem de verdade no MAME (`neogeo.cpp`):
```cpp
m_systemlatch->q_out_cb<0>().set(FUNC(...::set_screen_shadow));
m_systemlatch->q_out_cb<1>().set(FUNC(...::set_use_cart_vectors));
m_systemlatch->q_out_cb<2..4>().set_nop();  // memory card
m_systemlatch->q_out_cb<7>().set(FUNC(...::set_palette_bank));
```
Só tratávamos os bits 1 e 5. O bit 7 troca o BANCO DE PALETA — o hardware
tem dois (`m_paletteram.resize(0x1000 * 2)`, `neogeo_v.cpp`), e a janela
$400000-$401FFF sempre enxerga o banco ativo:
```cpp
void set_palette_bank(int state) { m_palette_bank = state ? 0x1000 : 0; ... }
uint16_t paletteram_r(offs_t offset) { return m_paletteram[m_palette_bank + offset]; }
```
Implementado: `palette[]` dobrou de tamanho, `palette_bank` novo em
`neogeo_t`, e todos os acessos (leitura/escrita da janela + os dois
renderizadores) passaram a somar o banco ativo.

Nota honesta: no Double Dragon isso não mudou NENHUM pixel (o jogo não usa
o banco 1 nas telas que testamos). É correção de corretude pra jogos que
usam — não foi a causa do artefato que eu estava investigando.

TODO relacionado: o bit 0 (`set_screen_shadow`, escurece a tela inteira)
continua não implementado.

### Achado #50: o ZOOM de sprite usava aproximação linear; o hardware usa
tabelas — e uma delas é um arquivo que já estava no romset

O aviso no topo do `lspc_sprites.c` dizia desde sempre que o shrink era uma
"aproximação LINEAR, não a tabela real do chip". Fomos medir o tamanho do
erro e não era pequeno:

| zoom_y | linhas amostrando a origem ERRADA |
|--------|-----------------------------------|
| 200    | 38%                               |
| 150    | 48%                               |
| 100    | 66%                               |
| 37     | 89%                               |

(em valores "redondos" como 255/127/63 a aproximação acerta, o que explica
por que nunca tinha aparecido nas telas estáticas que comparamos antes.)

**Vertical**: o MAME lê uma ROM de lookup — e ela é o `000-lo.lo`, que já
estava em todos os nossos romsets sem ser usado:
```cpp
ROM_REGION( 0x20000, "spritegen:zoomy", 0 )
ROM_LOAD( "000-lo.lo", 0x00000, 0x20000, ... )
...
const u8 sprite_y_and_tile = m_region_zoomy[(zoom_y << 8) | zoom_line];
int sprite_y = sprite_y_and_tile & 0x0f;   // linha dentro do tile
int tile     = sprite_y_and_tile >> 4;     // qual tile da coluna
```
Carregamos o arquivo (`rs->zoomy`) e passamos a usar a tabela. Também
implementamos o "invert" (`BIT(sprite_line, 8)` → `sprite_y ^= 0x0f;
tile ^= 0x1f`), que é como o chip cobre sprites de mais de 16 tiles de
altura: a tabela só vai até o tile 15 e as linhas 256-511 espelham pra
cobrir os tiles 16-31.

**Horizontal**: o chip não escala por regra de três — usa uma máscara de 16
bits dizendo QUAIS das 16 colunas de origem são desenhadas
(`zoom_x_tables[]`, marcada no MAME como "verified on real hardware"):
```
0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff
```
Copiamos a tabela e trocamos o laço horizontal por ela.

**Validação**: a tela do logo "NEO·GEO" do Double Dragon (vb650) agora bate
**pixel a pixel** com o snapshot real do MAME — 0 pixels diferentes de
71680. Na tela da foto digitalizada (vb1300) as únicas diferenças ficam
DENTRO da caixa da foto (linhas 57-143), que é conteúdo animado em fase
ligeiramente diferente; todo o resto (textos, logo Imperial, HUD) bate
exatamente.

### Achado #51: sprite com `rows >= 32` aparece em TODAS as linhas (fundo de
tela cheia), e o Y é CIRCULAR em 512 linhas

Duas regras que faltavam, ambas na mesma função do MAME:
```cpp
inline bool sprite_on_scanline(int scanline, int y, int rows) {
    return (rows == 0) || (rows >= 0x20) || ((scanline - y) & 0x1ff) < (rows * 0x10);
}
```
1. `rows >= 0x20` (32-63) = visível em toda linha de varredura. Descartávamos
   tudo acima de 32 truncando pra 32.
2. O `& 0x1ff`: a coordenada Y dá a volta em 512 linhas. Um sprite em y=392
   com 240px de altura ocupa 392..647, que "vira" pra 0..135 — aparecendo no
   TOPO da tela. Nós fazíamos `screen_y = y_pos + sy` sem volta, então esses
   sprites sumiam inteiros. (Foi assim que o painel "SNK" do estádio do Super
   Sidekicks 2 apareceu.)

Reescrevemos o laço pra varrer as linhas da TELA e calcular a distância até o
topo do sprite com `& 0x1ff`, exatamente como o hardware — em vez de varrer
linhas relativas ao sprite.

**Armadilha (errei e corrigi):** a mesma função tem `rows == 0` na lista de
"sempre visível", e eu implementei isso. Encheu a tela de título do Double
Dragon de chamas/lixo — conferido contra o MAME. O motivo: quem realmente
monta a lista de sprites por linha é `parse_sprites`, e lá o `rows == 0` é
DESCARTADO antes:
```cpp
/* skip sprites with 0 rows */
if (rows == 0) continue;
```
Ou seja, aquele `rows == 0` do `sprite_on_scanline` nunca dispara na prática.
Lição: ler a função que CHAMA, não só a que parece responder a pergunta.

### Achado #52: o auto-incremento do endereço de VRAM não é uma soma de 16 bits

```cpp
void set_videoram_offset(u16 data) {
    m_vram_offset = (data & 0x8000 ? data & 0x87ff : data);
}
void set_videoram_data(u16 data) {
    m_videoram[m_vram_offset] = data;
    set_videoram_offset((m_vram_offset & 0x8000) |
                        ((m_vram_offset + m_vram_modulo) & 0x7fff));
}
```
A parte alta da VRAM ($8000-$87FF: SCB2/3/4 e as listas de sprite por linha) é
uma janela separada: o incremento preserva o bit 15 e dá a volta só nos 15
bits baixos, e escrever um endereço com bit15 setado mascara pra $8000-$87FF.

Fazíamos `vram_addr = vram_addr + vram_mod` em 16 bits puros — uma escrita
sequencial longa na área de tiles que passasse de $7FFF entrava na área dos
blocos de controle, corrompendo posição/altura de sprites em vez de dar a
volta.

Nota honesta: corrigido por ser claramente errado, mas NÃO resolveu o fundo
faltando do Super Sidekicks 2 (ver TODO abaixo) — não medi nenhuma mudança
visível nos jogos testados.

TODO em aberto (fundo do Super Sidekicks 2): na demo de jogo o campo, a
torcida e o placar não aparecem (o MAME mostra tudo). Investigado até aqui:
- Não é paleta: com a paleta ignorada (`NEOGEO_DEBUG_GRAYPAL=1`) continua preto.
- Não é o decode do tile: o tile que os sprites de fundo apontam decodifica
  cheio (256/256 pixels opacos) direto do C ROM.
- Os sprites de fundo que EXISTEM na nossa VRAM (40 com rows=15, zoom_y=146,
  y=392) desenham corretamente — são o painel "SNK" do topo.
- Dos 381 sprites, 322 estão com `rows=0` na nossa VRAM, e o hardware
  descarta esses. Ou seja: o resto do estádio parece nunca ser ESCRITO na
  VRAM pelo jogo na nossa execução. Suspeita atual: divergência de lógica do
  jogo (algo que ele lê e nós devolvemos errado) e não de renderização.

### Achado #53: emulador rodando a ~30% da velocidade real — quatro causas
empilhadas, e a build inteira em modo Debug o tempo todo

Usuário mostrou a janela do SDL2 com um título ANTIGO ("Fase 4/6... sem
sprites ainda", texto morto de uma fase muito anterior) e pediu fps + % de
velocidade real no título. Adicionamos a medição (via
`SDL_GetPerformanceCounter`, atualizada a cada ~500ms) e o número veio baixo:
~30fps / ~50% em vez dos ~59fps esperados. Investigação em camadas:

1. **`getenv()` no laço mais interno**: `neogeo_render_sprites` chamava
   `getenv()` (pros hooks de depuração `NEOGEO_DUMP_*`/`NEOGEO_DEBUG_GRAYPAL`)
   dentro do laço POR PIXEL — até ~1.3 milhões de chamadas/frame (381 sprites
   x até 224 linhas x até 16 colunas). Mesmo erro já documentado antes com
   `getenv()` dentro de `neogeo_run_slice`, reintroduzido aqui sem perceber.
   Corrigido: lido uma vez só no topo da função.
2. **O(sprites x 224 linhas)**: a correção do wraparound de Y do Achado #51
   passou a varrer as 224 linhas da tela pra CADA sprite (pra achar onde ele
   aparece via `& 0x1ff`), em vez de só a altura visível dele — até 381x224
   iterações de `continue` por frame. Corrigido: sprites normais testam só 2
   deslocamentos candidatos (`y_pos` e `y_pos-512`) e iteram apenas o
   intervalo de linhas que cai dentro da tela; só sprites `all_scanlines`
   (fundo de tela cheia, raros) ainda varrem as 224 linhas de verdade.
3. **`SDL_Delay(16)` fixo**: media (`debug_perf`, nova ferramenta) que o core
   sozinho roda a ~390fps — não era o gargalo principal. O problema real:
   `SDL_Delay(16)` no frontend dormia 16ms **por cima** do tempo que
   CPU+render já tinham gastado (medido entre 4ms e 17ms dependendo da cena,
   via contadores de performance por etapa), em vez de completar um
   intervalo-alvo. Frame total = 16 + trabalho_real, nunca convergindo pros
   ~16.9ms de 59.1856Hz. Corrigido pra pacing de verdade: mede o tempo do
   frame inteiro e só dorme o que falta pra completar o intervalo-alvo; se o
   frame já estourou o orçamento, não dorme (deixa o próximo recuperar).
4. **A build inteira estava em modo Debug** (`CMAKE_BUILD_TYPE=Debug`, sem
   otimização do compilador) — inclusive o `.exe` que `jogar.bat` abre. Os
   três fixes acima ajudam, mas sozinhos não bastavam: só depois de trocar a
   build principal pra `Release` o número foi de ~30% pra **99% da
   velocidade real (58.5fps)**, medido rodando o `neogeo_sdl2.exe` exatamente
   como o `jogar.bat` faz.

Ferramenta nova: `core/tools/debug_perf.c` — roda N frames sem janela/SDL,
separando quanto tempo vai pra `neogeo_run_slice` (CPU) vs
`neogeo_render_frame` (vídeo), útil pra isolar "é o core ou é o frontend"
sem depender de medir a janela ao vivo.

Lição / armadilha: gastei um tempo achando que o gargalo era o método como
EU estava lançando o processo (via `nohup`/background do shell, suspeitando
de vsync/compositor) — os números batiam de forma inconsistente entre testes
justamente porque eu estava comparando builds Debug e Release sem perceber
qual estava rodando em cada teste. `debug_perf` (sem SDL, sem ambiguidade de
build) foi o que separou sinal de ruído.

### Achado #54 (RESOLVIDO): "fantasma" de 1 frame — renderizávamos num ponto
arbitrário do frame, pegando o jogo no meio da rotina de atualização

Usuário reportou "sprites com bugs tipo fantasmas, erro rápido que volta ao
normal". Escrita `core/tools/debug_flicker.c` (novo) pra varrer automatica-
mente muitos frames e achar saltos de cor que revertem sozinhos. Achou 1
ocorrência em ~3700 frames escaneados (vb 250-3699): em vb=1412 a foto
digitalizada fica magenta/roxa por EXATAMENTE 1 frame (vb1411 e vb1413 são
normais).

Descartado, um a um:
- Auto-animação (Achado #45): desligada via teste, o fantasma continua
  idêntico.
- Dados de sprite (SCB1-4, posição/tile/attr): byte-a-byte idênticos entre
  vb1411 e vb1412 (`NEOGEO_DUMP_RANGE`).
- Banco de paleta (Achado #49): fixo em banco 1 desde ~vb235, não troca
  perto do frame afetado.
- Confirmado REAL (não efeito intencional do jogo): o MAME real, no mesmo
  vb=1412, mostra a cor normal — não reproduz o flash.

O que muda de fato: 18 entradas na LINHA DE PALETA 192-193 (dump binário
completo do banco ativo, `NEOGEO_DUMP_PAL_FULL`). O padrão é uma ROTAÇÃO por
um: cada valor antigo aparece um slot adiante no frame seguinte
(`idx=3074: 0312->0112`, `idx=3075: 0132->0312`, ...) — bate com uma técnica
comum de efeito "estática de TV" (a foto mostra uma tela de TV com chuvisco
atrás do personagem): o jogo roda essa linha de paleta por um contador a
cada frame. Nesse UM frame específico, nosso valor computado diverge do
real.

**CAUSA RAIZ (achada depois):** o que destravou foi comparar a paleta ao
longo de vários frames em vez de só dois. A linha 192 muda em vb1412 e
FICA assim em 1413/1414 — ou seja, não era corrupção transitória, era uma
atualização normal do jogo. E vb1412/1413/1414 têm paleta IDÊNTICA, mas
1412 sai magenta e 1413 sai normal. Logo o problema não era a paleta: era
o que ela estava pintando.

Diff de sprites entre 1412 e 1413 fechou o caso: a foto digitalizada é
montada por DOIS grupos de sprites (s261-270 com paleta 193, s271-280 com
paleta 192) cujos tiles formam um bloco contínuo (1aed8..1af4f). Cada frame
do "slideshow" troca o bloco inteiro de tiles E as duas paletas. Em vb1412
nós amostrávamos com a **paleta 192 já atualizada mas os tiles do grupo que
a usa ainda antigos** — paleta nova pintando tiles velhos = magenta. Um
frame rasgado (tearing), clássico.

Por que: `neogeo_run_slice(&ng, 202752u)` roda "um frame de ciclos a partir
de onde parou", e o chamador renderiza logo depois. Esse ponto de parada
não tem relação nenhuma com a borda de vblank — caía no meio da rotina de
atualização do jogo. No hardware real o que aparece na tela é o conteúdo da
VRAM/paleta durante a varredura da área visível, e o jogo escreve as
atualizações DEPOIS, dentro do vblank.

Correção: novo `neogeo_run_until_vblank()` (em `neogeo.c`, declarado em
`neogeo.h`), que roda exatamente até a próxima borda de vblank:
```c
u32 neogeo_run_until_vblank(neogeo_t *ng) {
    u32 remaining = NEOGEO_CYCLES_PER_FRAME - ng->cycles_since_vblank;
    if (remaining == 0) remaining = NEOGEO_CYCLES_PER_FRAME;
    return neogeo_run_slice(ng, remaining);
}
```
Todos os que DESENHAM passaram a usar isso (`platform/sdl2/main.c`,
`debug_screenshot`, `debug_flicker`, `debug_perf`). As ferramentas que só
inspecionam estado (`debug_boot`, `debug_checkpoints`, `debug_wramdump`)
ficaram como estavam de propósito.

**Validação:** vb1412 deixou de sair magenta (1411/1412/1413 agora todos
normais); a varredura de 1450 frames não acha mais o fantasma; o logo
"NEO·GEO" (vb650) continua **pixel-exato** vs MAME (0 de 71680 diferentes);
e a foto (vb1300) até melhorou um pouco (6710 vs 7126 pixels de diferença,
o resto sendo fase de animação).

Nota: a varredura ainda lista 6 "fantasmas" em vb1671-1688, mas todos com
assinatura de alternância perfeita (`d_ac=0`, `d_ab==d_bc`) e, inspecionados
visualmente, são a tela de título surgindo/piscando de propósito — não são
defeito. O detector é sensível demais pra esse padrão; é limitação da
ferramenta, não bug do emulador.

Efeito colateral corrigido: `debug_flicker.c` tinha um off-by-one (a
contagem de `neogeo_run_slice()` na hora do render ficava 1 a mais que o
`target_vb` do `debug_screenshot.c` pro mesmo rótulo) — os números que ele
imprime já foram corrigidos, e o achado acima já usa os vblanks CERTOS
(confirmados direto no `debug_screenshot`).

### Achado #55: a janela $0000-$7FFF do Z80 não é fixa na BIOS de áudio —
ela alterna com a M1 do cartucho pelo bit 5 do latch

Tínhamos `$0000-$7FFF` do Z80 sempre apontando pra SM1 (a "BIOS de áudio"),
com a M1 do cartucho entrando só pelas janelas de bankswitch. Errado. Do
MAME (`neogeo.cpp`):
```cpp
m_systemlatch->q_out_cb<5>().set(FUNC(ngarcade_base_state::set_use_cart_audio));
void set_use_cart_audio(int state) { m_bank_audio_main->set_entry(state); }
// entry 0 = "audiobios" (SM1), entry 1 = ROM de audio do cartucho (M1)
```
É o MESMO bit 5 que já usávamos pra fonte da fix layer (Achado #27) — um
bit, dois efeitos. Sem isso o Z80 rodava pra sempre o driver da BIOS, que só
conhece os sons da BIOS, e nunca tocaria nada do jogo.

Nota (`neogeo.cpp:1319`): em placa SEM a SM1, a ROM do cartucho fica sempre
ligada — tratado no código.

### Achado #56: faltavam os TIMERS do YM2610 (e a IRQ deles pro Z80) — era
por isso que o driver inicializava e parava

Sintoma: com a M1 certa carregada (#55), o driver de som recebia os comandos
do 68k corretamente (handshake NMI + porta $00 rastreados e funcionando),
configurava instrumentos FM... e nunca dava key-on em nada. Nem FM (todas as
escritas em $28 com o nibble alto zerado = key OFF) nem ADPCM-A (todas as
escritas em banco B $00 com bit7 = key OFF).

Duas verificações que fecharam o diagnóstico:
1. Comparamos nossa WRAM inteira contra a do MAME no mesmo vblank
   (`debug_wram_at` novo + `save mamewram.bin,0x100000,0x10000` no debugger
   do MAME): **351 bytes diferentes de 65536 (0,5%)**, e a FILA DE COMANDOS
   DE SOM do jogo em `$1059C0` era IDÊNTICA (`e2 f5 c0 ed`). Ou seja, o lado
   68k estava certo e aqueles bytes de aparência aleatória são os comandos
   reais do jogo mesmo. (Descobrimos a fila decodificando a instrução em
   `$002c42`: `MOVE.B (A0,D0.W),$320000` com A0=$1059C0.)
2. O driver escrevia `$26 = $eb` e `$27 = $2a` — ou seja, **Timer B ligado
   COM IRQ habilitada**. Drivers de som avançam a sequência de música a cada
   IRQ de timer. Nós não implementávamos timer nenhum, então a IRQ nunca
   vinha e a música nunca andava.

Implementado em `core/src/audio/ym2610.c`: timers A e B, flags de estouro,
registrador de status (portas $04/$06 — antes devolvia 0 fixo) e a linha de
IRQ pro Z80 (`Z80_IRQ`). Períodos em amostras de FM: A = `1024 - TA`,
B = `16 * (256 - TB)`. O tick roda no laço de CPU (não no render de áudio),
a 1 amostra a cada 216 ciclos do 68000 (144 clocks do YM @8MHz vs 12MHz do
68k), pra que a música ande mesmo em ferramentas que não geram som.

**Resultado:** escritas no YM saltaram de **203 para 11920** num mesmo
trecho, e o pico do áudio de **0 para 25840** — saiu som de verdade. O
primeiro som aparece aos ~23.5s do attract, exatamente quando o jogo começa
a enviar seus comandos. Só ADPCM-A por enquanto (samples/vozes); a música FM
precisa da síntese FM, que ainda não existe.

### Achado #57: síntese FM do YM2610 (4 canais × 4 operadores)

A peça grande do áudio. Só ADPCM-A tocava — samples e vozes — mas a MÚSICA
da maioria dos jogos é FM. Implementado em `core/src/audio/ym2610.c`,
copiando tabela por tabela e fórmula por fórmula do `ymfm` de Aaron Giles
(`src/ymfm_fm.ipp` + `src/ymfm_opn.cpp`), a mesma metodologia dos achados de
vídeo: nada deduzido de resumo.

O que entrou:
- `s_sin_table[256]` (seno logarítmico 4.8 de 1/4 de período; o sinal vem do
  bit9 da fase) e `s_power_table[256]` (atenuação 5.8 → volume linear).
- Gerador de fase: `fnum`/`block`/`detune`/`multiple`, com o keycode de 5
  bits vindo de `bitfield(block_freq,10,4)<<1 | bitfield(0xfe80, ...)`.
- Envelope de 4 estágios com `s_increment_table[64]`, o `rate_shift = rate>>2`
  e o glitch real do chip (taxas de attack 62/63 não incrementam).
- SSG-EG (os 8 modos), que vários drivers usam pra sons metálicos.
- Os 8 algoritmos de ligação, via a tabela `s_algorithm_ops` e o array
  `opout[8]`, com realimentação do operador 1.
- LFO (AM e PM), com `s_lfo_pm_shifts[8][8]`.
- Saída L/R por canal ($B4 bits 7/6), `rshift=1` e clamp em 16 bits — os
  parâmetros que o `ym2610::clock_fm_and_adpcm` do ymfm usa.

Dois detalhes que só se descobre lendo o código do chip certo:
1. **O YM2610 tem 6 slots de canal mas só 4 ligados** — `channel_mask 0x36`,
   ou seja, offsets 1 e 2 de CADA banco de registradores. Um mapeamento
   ingênuo "canal 0,1,2,3" erraria os registradores todos.
2. **É o FM que dá o compasso do ADPCM-A.** O divisor de envelope do OPN é
   3: o contador pula o valor 3 no ciclo baixo, de modo que
   `env_counter & 3 == 0` acontece uma vez a cada 3 amostras de FM — e é
   esse evento que avança o ADPCM-A. Trocamos o nosso contador manual de
   1/3 por esse, que é a fonte real.

Mapa de registradores (do `ym2610::write_data`/`write_data_hi` do ymfm):
banco A `$00-$0D` SSG, `$10-$1C` ADPCM-B, `$1D-$FF` FM; banco B `$00-$2F`
ADPCM-A, `$30-$FF` FM. Também entrou o **latch de `$A0-$AF`**: escrever em
`$A4+c` só guarda os bits altos, e a escrita em `$A0+c` comita os dois de
uma vez — sem isso a frequência mudaria em dois passos e sairia um estalo
audível a cada nota.

**Validação** (`debug_audio` com as chaves novas `NEOGEO_MUTE_FM` /
`NEOGEO_MUTE_ADPCM`, que isolam as duas fontes em vez de olhar só o pico da
soma):

| jogo | FM+ADPCM | só ADPCM | leitura |
|---|---|---|---|
| ssideki2 | 11283 | 6960 | as duas fontes tocando |
| neobombe | 2153 | 0 | música só FM |
| samsho | 6696 | 6696 | trecho só de samples |
| doubledr | 4697 | 0 | música só FM |

Custo: ~11x tempo real no núcleo, e o frontend continua em **99% da
velocidade real** com o som ligado.

### Achado #58: a conversão de cor de 5 bits pra 8 NÃO é linear (e o bit
"dark" não é "metade do brilho")

Conferindo a tela do logo contra o MAME depois do FM, apareceram **6417
pixels diferentes de 71680** — mas a geometria batia perfeitamente: as
diferenças eram todas do tipo "nosso 246, MAME 247" ou "nosso 213, MAME
217". Diferença de COR, não de posição.

A causa: o DAC de vídeo do Neo-Geo é uma escada de resistores de **3900,
2200, 1000, 470 e 220 ohms** (bit0 → bit4), e o MAME modela isso
(`neogeo_base_state::create_rgb_lookups` em `src/mame/snk/neogeo_v.cpp`,
chamando `compute_resistor_weights` de `src/emu/video/resnet.cpp`). O nosso
`(v * 255) / 31` é uma rampa linear — errava até 5 níveis por canal.

E o bit "dark" (bit15) não é brilho pela metade: é um resistor de
**pull-down de 8200 ohms** em paralelo, que escurece pouquíssimo
(255 → 251 no topo). O que escurece de verdade é o **shadow**, um pull-down
de **150 ohms** (255 → 142), usado pelo bit0 do latch de sistema e pelo pen
de sombra dos sprites — coisa que a gente estava aplicando no lugar errado.

Substituímos por quatro tabelas de 32 entradas (normal / dark / shadow /
dark+shadow), calculadas com a mesma matemática do `resnet.cpp` (mesmo
autoscale a partir da rede sem pull-down), em `lspc_fix.c`.

**Resultado:** a tela do logo "NEO·GEO" do Double Dragon (vb650) voltou a
bater **0 pixels diferentes de 71680** com o snapshot do MAME.

### Achado #59: SSG (as 3 ondas quadradas do YM2149 dentro do YM2610)

Terceira fonte de som do chip, registradores `$00-$0D` do banco A: 3 canais
de onda quadrada, um gerador de ruído por LFSR de 17 bits e um envelope em
rampa compartilhado. Copiado de `ymfm_ssg.cpp`, inclusive a tabela
`s_amplitudes[32]` (que é logarítmica, não linear) e os dois detalhes
contra-intuitivos:
- O registrador `$07` ("mixer") tem lógica **invertida**: bit SETADO
  DESLIGA a fonte.
- Período de ruído 0 tem que soar igual a período 1 (daí o `!= 1` no
  comparador).
- Escrever em `$0D` (forma do envelope) **reinicia** a rampa — é assim que o
  driver dispara cada nota.

Taxa: o reamostrador do ymfm pro YM2610 é 9 passos de SSG a cada 2 amostras
de saída (250 kHz), com os extremos valendo meio passo; copiado igual
(`resample_2_9`). O SSG é mono e vai pros dois lados, com ganho relativo
0.84/0.98 ≈ 6/7 contra o FM (rotas do `neogeo.cpp` do MAME).

Resultado medido (pico, 1500 vblanks, com `NEOGEO_MUTE_FM=1
NEOGEO_MUTE_ADPCM=1`): doubledr 334, os outros testados 0 — o SSG no
Neo-Geo é mesmo usado com parcimônia, então esse é o resultado esperado, não
um bug.

### Achado #60: causa-raiz do Achado #46 — o ninja NUNCA rastreou
dependência de header neste projeto

O Achado #46 ("build velho com layout de struct antigo corrompendo a
memória") voltou a acontecer ao mexer no `neogeo.h` pro SSG: o áudio saía
com picos absurdos de +32767 que sumiam depois de um `ninja -t clean`.
Dessa vez fui atrás do PORQUÊ em vez de só limpar.

O ninja descobre dependências de header no MSVC lendo a saída do
`/showIncludes` e comparando com o `msvc_deps_prefix` do `rules.ninja`.
Aqui:
- O `cl.exe` instalado só fala **português** e emite
  `Observação: incluindo arquivo:` (o `VSLANG=1033` não resolve — o pacote
  de idioma inglês não está instalado).
- O CMake detectou esse prefixo e o gravou no `rules.ninja` em **UTF-8**.
- Mas o `cl.exe`, escrevendo num pipe, emitia os acentos na **codepage do
  console** (CP850). Os bytes não batiam, o ninja descartava a saída
  inteira e **nenhuma dependência de header era registrada**.

Ou seja: durante todo o projeto, mexer em `neogeo.h` não recompilava nada.
Todo "bug fantasma" depois de mudar o header tinha essa origem.

Correção: `chcp 65001` nos scripts de build (`build/_b.bat` e `_bc.bat`),
que faz o `cl.exe` emitir UTF-8 e bater com o `rules.ninja`.

Verificação: `ninja -t deps <objeto>` agora lista os headers, e tocar em
`neogeo.h` passa a marcar 7 arquivos pra recompilar (antes: 0).

### Achado #61: ADPCM-B ("delta-T") — o último bloco do YM2610

Canal único com taxa de reprodução variável (delta-N de 16 bits), passo
adaptativo diferente do ADPCM-A (escala 0.9/1.2/1.6/2.0/2.4 em vez da
tabela de 49 passos) e **interpolação linear** entre amostras. Copiado do
`adpcm_b_channel` do ymfm. Registradores `$10-$1C` do banco A.

Também entrou no carregador: o YM2610 tem **dois barramentos de amostra
separados**, e a convenção de nomes dos cartuchos reflete isso — quando o
jogo usa os dois, as ROMs de A viram `v11/v12/...` e as de B `v21/v22/...`;
quando só usa A, ficam `v1/v2/...`. Antes o carregador só conhecia
`v1..v4`, então um jogo com `v11` teria ficado sem NENHUMA amostra.

**Ressalva honesta:** implementado, mas **não validado com som real** —
nenhum dos jogos que temos aqui usa ADPCM-B (conferido no `hash/neogeo.xml`
do MAME: doubledr, ssideki2, samsho e pbobblen não têm região
`ymsnd:adpcmb`). Quem usa são títulos antigos como nam1975 e ridhero.

Com isso o **YM2610 está completo**: ADPCM-A, ADPCM-B, FM e SSG, mais os
timers. Frontend continua em 99% da velocidade real e a tela do logo
continua pixel-exata contra o MAME.

### Achado #62: "screen shadow" (bit0 do latch de sistema)

O último bit do latch HC259 que faltava. No MAME não é um efeito por
sprite: `set_screen_shadow` só desloca a base dos pens pra `+0x2000`, que é
a mesma paleta convertida com o pull-down de 150 ohms do Achado #58. Ou
seja, escurece o DAC inteiro. Ligado em `neogeo_color_to_rgb888_ex`.

### Achado #63: o fundo da tela NÃO é preto

Limpávamos o framebuffer com zeros. O hardware pinta o fundo com o ÚLTIMO
pen do banco de paleta ativo (o "backdrop"). No MAME:
```cpp
m_bg_pen = pen_base + 0xfff;      // set_pens()
bitmap.fill(*m_bg_pen, cliprect); // screen_update()
```
Jogos com fundo colorido (menus, transições, fades) ficavam com moldura
preta em volta do que os sprites cobriam.

### Achado #64: limite de 96 sprites por linha (o "flicker" do Neo-Geo)

O LSPC monta, a cada linha de varredura, uma lista de no máximo **96**
sprites — os 96 primeiros em ordem de índice que tocam aquela linha. Do 97º
em diante o hardware simplesmente não desenha. É por isso que jogos cheios
piscam: eles ALTERNAM a ordem dos sprites entre frames de propósito, pra
que o que sobra pisque em vez de sumir.

Sem esse limite a tela aparecia MAIS completa que no hardware real — e o
efeito de piscar que o jogo programou não acontecia. Fonte:
`neosprite_base_device::parse_sprites` (constante `MAX_SPRITES_PER_LINE = 96`,
e `MAX_SPRITES_PER_SCREEN = 381`, não 384).

Como o nosso render é de frame inteiro, fazemos a mesma varredura antes,
uma vez por linha visível, guardando o índice do último sprite aceito
(224 × 381 iterações baratas por frame).

### Achado #65: a fase do pulso do RTC é REINICIADA por comando — e isso
valia ~49 frames de diferença no boot

Comparando a WRAM inteira do Super Sidekicks 2 contra o MAME frame a frame:
até o vblank 220 idêntica, e do 230 em diante completamente diferente — nós
já tínhamos terminado o teste de RAM da BIOS que o MAME só terminaria ~50
frames depois. Um contador livre de frames em `$10FE8B` confirmava: estávamos
**49 frames adiantados**.

Causa: tratávamos o pulso TP do RTC (µPD4990A) como um oscilador LIVRE desde
o reset. Não é. No MAME (`neogeo.cpp`, `io_control_w` com
`offset & 0x78 == 0x28`) o 68000 fala com o RTC por três pinos:
```cpp
m_upd4990a->data_in_w(BIT(data, 0));
m_upd4990a->clk_w(BIT(data, 1));
m_upd4990a->stb_w(BIT(data, 2));
```
e em `upd1990a.cpp`, na BORDA DE SUBIDA do STB, qualquer comando de "time
pulse" faz:
```cpp
m_timer_tp->adjust(attotime::zero, 0, one_second * mul / 2.0);
```
`attotime::zero` = dispara JÁ: o TP inverte na hora e o meio-período
recomeça a contar dali. Como a BIOS conta VBLANKS entre bordas do TP pra
validar o timing, uma fase arbitrária desloca o boot inteiro.

Implementado o reset de fase na borda de subida do STB. **Resultado:** a
divergência caiu de **49 frames pra 2-3**. Sobra um resíduo pequeno
(diferença de contagem de ciclos do nosso 68000 no teste de RAM da BIOS) —
anotado, não perseguido ainda.

Chave de depuração: `NEOGEO_NO_RTC_STROBE=1` volta ao comportamento antigo
pra comparar.

### Achado #66 e #67: metade do programa do Samurai Shodown nunca era
carregada — e a outra metade era embaralhada

Dois bugs no carregador que juntos derrubavam os cartuchos de 2MB+.

**#66 — o token `sp2` faltava.** A segunda metade da P ROM nem sempre se
chama `p2`. O Samurai Shodown vem como `045-p1.p1` + `045-pg2.sp2`. Sem o
token `sp2`, só 1 dos 2 MB entrava. Sintoma: por volta do vblank 806 o jogo
lia uma tabela de ponteiros, recebia lixo, e o 68000 tomava **ERRO DE
ENDEREÇO** (vetor 3, `move.w (A0)+` com A0 = `$BE4EF9`, ímpar) — a máquina
REINICIAVA e ficava na tela verde com texto embaralhado, que é a BIOS
refazendo o teste de RAM. Achado com uma ferramenta nova
(`core/tools/debug_jump.c`) que roda instrução a instrução a partir de um
vblank e avisa na primeira exceção, com histórico de PCs e registradores.
*(Não adicionar `sp1` à lista: é o nome das ROMs de BIOS, `sp-s2.sp1` etc.)*

**#67 — a troca de metades não vale pra todo cartucho grande.** O Achado #39
trocava as duas metades sempre que a P ROM passava de 1MB. Mas isso é o
idioma de **UM ARQUIVO** carregado com `ROM_CONTINUE`. Conferindo as
definições reais (`hash/neogeo.xml` do MAME):

| jogo | arquivos | destino | troca? |
|---|---|---|---|
| doubledr | `082-p1.p1` (2MB) | `0x100000` + `ROM_CONTINUE 0x000000` | **sim** |
| samsho | `045-p1.p1`, `045-pg2.sp2` | `0x000000`, `0x100000` | não |
| mslugx | `250-p1.p1`, `250-p2.ep1` | `0x000000`, `0x100000` | não |

Com dois ou mais arquivos eles entram em ordem e ponto. Passamos a aplicar a
troca só quando o carregador consumiu **exatamente um** arquivo.

**Resultado:** Samurai Shodown saiu de "71680 pixels diferentes de 71680"
(tela reiniciada) para **0 no vblank 600** e **337 no vblank 900**. E o
Metal Slug X, que antes ficava preso na grade de teste da BIOS, agora
executa o código do jogo de verdade — chega na tela de aviso anti-pirataria
da SNK, que é exatamente o esperado enquanto o chip de proteção do cartucho
não estiver implementado.

Os 8 jogos do `jogar.bat` conferidos no vblank 1200: Double Dragon
(Imperial), Super Sidekicks 2 (100 MEGA SHOCK), Neo Bomberman, Samurai
Shodown (texto de abertura), Puzzle Bobble, Tecmo World Soccer 96 (jogo
rolando), V-Liner (SRAM ERROR — esse cartucho tem SRAM própria, ainda não
implementada) e Metal Slug X (aviso da SNK).

### Achado #68: render LINHA A LINHA (o que destrava os efeitos de raster)

Até aqui o frame inteiro de CPU rodava primeiro e só então a tela era
desenhada de uma vez (`neogeo_run_until_vblank` + `neogeo_render_frame`,
Achado #54). Isso é correto enquanto o jogo só mexe em VRAM/paleta durante o
vblank — mas o Neo-Geo tem um **timer de raster** (`$3C0008`/`$3C000A`) cuja
IRQ dispara numa LINHA escolhida, justamente pra trocar paleta ou posição no
meio da tela (barras de água, céu em degradê, divisão de tela). Com render de
frame inteiro essas trocas ou valiam pra tela toda ou não valiam pra nada.

Implementado `neogeo_run_frame_render()`: o frame roda em **264 fatias de
uma linha** e cada uma das 224 linhas visíveis é desenhada logo depois de
varrida, com o estado do chip naquele instante. As 16 linhas de vblank de
cima (`NEOGEO_VBEND_LINES`) e as de baixo rodam sem desenhar. Os
renderizadores ganharam versões por faixa (`neogeo_render_sprites_range`,
`neogeo_render_fix_layer_range`, `neogeo_render_frame_range`); o render de
frame inteiro virou só o caso `y0=0, y1=224`.

As fatias são calculadas como `ciclos_por_frame * (linha+1) / 264` em vez de
somar um tamanho fixo, pra não acumular erro de arredondamento ao longo do
frame.

**Validação:** as três telas de referência pixel-exatas continuam com **0
diferenças de 71680** no modo linha a linha (Double Dragon vb650,
Super Sidekicks 2 vb600 e vb1200, Samurai Shodown vb600) — ou seja, a
mudança não alterou nada do que já estava certo. O frontend continua em
**99-100% da velocidade real**.

O `debug_screenshot` aceita `NEOGEO_SCANLINE=1` pra usar o caminho novo
(útil pra comparar os dois lado a lado).

### Achado #69: chip de proteção do Metal Slug X

Com o Achado #66/#67 o Metal Slug X passou a executar o código do jogo, mas
ficava **parado pra sempre** na tela de aviso anti-pirataria da SNK ("THIS
ROM CARTRIDGE IS FOR USE WITH ORIGINAL SNK BOARDS ONLY").

O cartucho (placa NEO-MVS PROGEOP, 1999) tem um ALTERA EPM7128 amarrado à
250-P1, na janela **$2FFFE0-$2FFFEF**. Copiado de `mslugx_prot_device`
(`src/devices/bus/neogeo/prot_mslugx.cpp`) e do mapeamento em `neogeo.cpp`
(`case NEOGEO_MSLUGX`). O chip é, na prática, um **leitor de bit serial**
sobre a própria P ROM a partir de `$0DEDD2`:

```
escrita  offset 0     -> começa leitura nova (command = 0)
         offset 1,2   -> acumula o comando (command |= data)
         offset 3     -> terminou
         offset 5     -> init (counter = 0, command = 0)
leitura  command 0001 -> bit em (counter), counter++ a cada leitura
         command 0fff -> bit em (word de $10F00A) - 1
         em ambos: (byte[0xdedd2 + (sel>>3)] >> (~sel & 7)) & 1
```

**Identificação do cartucho:** em vez de olhar o nome da pasta, lemos o
**NGH** (número do jogo) do cabeçalho padrão da P ROM em `$000108`, em BCD —
o mesmo dado que a BIOS lê. NGH `0250` = Metal Slug X.

**Resultado:** o jogo passa do aviso e chega no **título completo** e na
tabela de recordes ("BEST TANK BUSTERS"). Chave de depuração:
`NEOGEO_TRACE_PROT=1` mostra o diálogo com o chip.

### Achado #70: SRAM de cartucho (V-Liner)

O V-Liner (NGH `03E7`) parava em **"SRAM ERROR"**: esse cartucho tem 8KB de
SRAM com bateria em `$200000-$201FFF`, no lugar onde os outros têm o banco
de P ROM (`neogeo.cpp`, `case NEOGEO_VLINER`). Implementada; o jogo agora
passa da checagem e chega na tela "GAME CONFIG.".

**Ainda falta:** o V-Liner também remapeia as entradas (DSW em `$300000`,
IN5 em `$280000`, IN6 em `$2C0000`), então os controles desse cartucho
específico ainda não respondem — dá pra ver a tela, não pra navegar.

### Achado #71: rodar direto do .zip (romset MAME) — sem extrair

Baixamos o romset MVS/AES completo do MAME (170 jogos, 2.4GB, em
`roms/zips/*.zip`). Extrair cada um numa pasta própria (como fazíamos até
aqui) bagunçaria o projeto rapidamente. Em vez disso, o carregador agora lê
ROM direto de dentro do `.zip`.

**Como**: `neogeo_romset_load_dir_ex()` continua com a mesma assinatura
(`cart_dir`, `bios_dir`) — só que agora cada um desses caminhos pode
apontar tanto pra uma PASTA (comportamento antigo, intacto) quanto pra um
ARQUIVO `.zip`, detectado automaticamente (é pasta → trata como pasta; é
arquivo → tenta abrir como zip). Toda a lógica de busca por token (P ROM,
C ROMs, V ROMs...) continua igual — só a camada de baixo nível foi trocada
por uma pequena abstração, `rom_source_t`, com duas implementações:
"listar arquivos de uma pasta" (o `list_dir` de sempre) e "listar entradas
de um zip" (via miniz). Os romsets MAME usam DEFLATE de verdade (conferido
com Python: `compress_type == 8`), não dava pra só copiar bytes crus do
arquivo — por isso vendorizamos o **miniz** (Rich Geldreich, MIT,
`third_party/miniz/`, versão amalgamada de arquivo único) em vez de escrever
um inflate do zero.

**Validação**: carreguei o Double Dragon direto de `roms/zips/doubledr.zip`
(uma revisão de dump DIFERENTE da que já tínhamos extraído — bytes
diferentes, confirmado por md5) e comparei contra o snapshot do MAME:
**0 pixels diferentes de 71680**, igual à versão em pasta. Testado também
Samurai Shodown, Metal Slug X (chip de proteção incluído) e Super Sidekicks
2 direto do zip — todos corretos.

**Launcher novo** (`jogar.bat`): antes tinha 8 entradas fixas apontando pra
pastas extraídas. Agora lista os 170 `.zip` de `roms/zips/` dinamicamente,
com filtro por nome (digite parte do nome, ex: "mslug") e paginação (20 por
página) — sem precisar editar o `.bat` toda vez que uma rom nova entra na
pasta. O save de cada jogo (`NEOGEO_BACKUP_RAM_FILE`) usa o nome do zip sem
extensão, então cada jogo tem sua própria pasta de save em
`third_party/mame/nvram/<jogo>/` automaticamente.

**De quebra**, rodando a suíte de testes (`ctest`) achei um teste (`test_video`)
com uma expectativa desatualizada desde o Achado #47 desta manhã (a linha 0
da tela cai no offset `$7002` do tilemap da fix layer, não `$7000` — o
teste escrevia no endereço errado e por acidente não notava). Corrigido;
os 5 testes automatizados voltaram a passar 100%.

### Achado #72: chip SMA+CMC42 (King of Fighters '99) — implementado, mas o
jogo ainda não passa da tela de aviso

Pedido: "roda algum [jogo] que tenha o SMA" — o esquema de proteção mais
comum nos cartuchos de 1999+ (KOF99, Garou, Metal Slug 3, KOF2000), bem
mais pesado que o chip do Metal Slug X (Achado #69): aqui o PROGRAMA e as
GRÁFICAS de sprite saem de fato criptografados da ROM, não é só um "leitor
de bit".

**O que foi implementado**, copiado de fonte primária do MAME
(`src/devices/bus/neogeo/prot_sma.cpp` e `prot_cmc.cpp`):
- Descriptografia do 68k em 3 passos (troca de bits dos dados na área
  bancada inteira; troca de bits de endereço por blocos de 0x800 bytes;
  reconstrução do banco fixo a partir da área bancada já descriptografada).
- Carregamento especial da P ROM: 3 arquivos (`ka.neo-sma`, `251-p1.p1`,
  `251-p2.p2`) em OFFSETS ABSOLUTOS fixos num buffer de 9MB — não é
  concatenação simples como o resto dos cartuchos.
- Tabela de bankswitch própria (`kof99_bank_base`, com bitswap<6> e uma
  tabela de 33 offsets) no lugar da fórmula padrão `(banco+1)*1MB`.
- Registrador de proteção fixo (retorna sempre `0x9A37`) e gerador de
  "número aleatório" (LFSR de 16 bits) que o jogo consulta.
- Descriptografia CMC42 das C ROMs: monta temporariamente o layout
  intercalado que o hardware físico usa (pares c1/c2, c3/c4... intercalados
  byte a byte), roda o algoritmo (9 tabelas de 256 bytes + permutação de
  endereço em 2 passadas), desfaz a intercalação de volta pra c_odd/c_even,
  e extrai os dados da fix layer (que nesses cartuchos vêm do FINAL da
  C ROM, não de um s1.s1 separado).
- Detecção do cartucho por nome de arquivo (prefixo "251" + arquivo
  "*.neo-sma") — não dá pra usar o NGH do cabeçalho como nos Achados #69/#70
  porque ele só existe DEPOIS da descriptografia.

**Bug feio no caminho**: um `malloc`/`free` usados em `prot_sma_kof99.c`
sem `#include <stdlib.h>` — sem o protótipo em escopo, o compilador (MSVC
em modo C) assumiu que essas funções devolvem `int` (32 bits) e TRUNCOU o
ponteiro de 64 bits retornado, com sign-extension incluído (`buf` chegou a
aparecer como `0xFFFFFFFFF4519040`). Só apareceu na descriptografia CMC42
porque foi o primeiro malloc "grande" depois do bug introduzido. Lição:
mesmo com quase 30 achados de hoje usando padrões C bem estabelecidos,
falta um include básico ainda quebra tudo silenciosamente — vale a pena
compilar com avisos de função implícita tratados como erro no futuro.

**Validação alcançada**: o P ROM decodificado bate BYTE A BYTE com o dump
real do MAME no banco fixo inteiro (as únicas diferenças são exatamente as
esperadas — os vetores de exceção que a BIOS sobrepõe por padrão, Achado
#40, não bug nosso). O jogo carrega, roda código real descriptografado,
mostra a tela de aviso da SNK com fonte e cores corretas (prova que a
extração da fix layer via CMC42 também está no caminho certo).

**Onde emperra**: o jogo fica preso num loop (`move.b D0,$300001` +
`bra.s`) na tela de aviso, sem nunca ler os registradores de proteção —
ou seja, trava ANTES até de tentar o desafio SMA, não por causa dele. No
MAME de referência esse mesmo ponto passa em menos de 600 vblanks direto
pro attract mode. Ainda não isolei a causa exata: pode ser um bug na
descriptografia do BANCO usado nesse trecho específico do boot (só validei
o banco FIXO byte a byte; o banco TROCADO ainda não tem uma verificação
byte a byte confiável — as tentativas via escrita manual no debugger do
MAME não confirmaram o registrador de troca de banco de forma consistente)
ou alguma checagem adicional (checksum?) que ainda não localizei.

**Resumindo**: a infraestrutura de descriptografia SMA+CMC42 está
implementada e o P ROM decodificado já bate com o MAME onde consegui
verificar — mas o KOF99 especificamente ainda não sai da tela de aviso.
Não é uma mentira de "100% funcionando"; é trabalho real, real assim como
os outros achados, só que incompleto nesse título específico.

> **RESOLVIDO no Achado #73 abaixo.** A descriptografia estava certa o tempo
> todo; o que faltava era o *watchdog*. Deixo o texto acima como está porque
> ele registra honestamente o que eu sabia naquele momento.

## Achado #73: o watchdog — e por que o KOF99 travava na tela WARNING

**Sintoma:** KOF99 dava boot, descriptografava certo, desenhava a tela
"WARNING — THIS ROM CARTRIDGE IS FOR USE WITH ORIGINAL SNK BOARDS ONLY" e
travava pra sempre em `$9D8E` (`move.b D0,$300001` + `bra.s -8`).

**Rastreamento (código real do cartucho, desmontado no MAME):**

```
0087CE: bclr #7, $10FD80
0087D6: tst.b  $10FD82
0087DC: beq    $87EC
0087DE: tst.w  $D00100        <- flag "proteção falhou", na backup RAM
0087E4: beq    $87EC
0087E6: jmp    $9D72          <- tela WARNING, laço infinito
```

Quem liga esse flag é `$9D44` (`move.w #1,$D00100`), chamado de `$9CE4`.
E `$9CE4` só é alcançado depois deste laço, em `$9CDA`:

```
009CDA: 203C 0003 0D40   move.l  #$30D40,D0
009CE0: 5380             subq.l  #1,D0
009CE2: 64FC             bcc     $9CE0        <- 200.000 voltas, ~3,6M ciclos
009CE4: 6100 005E        bsr     $9D44        <- marca "proteção falhou"
```

**A causa:** durante essas 200.000 voltas o programa **não escreve em
`$300001`** — e `$300001` é o watchdog. No hardware real a placa RESETA no
meio do laço, então `$9CE4` nunca executa e o flag nunca é ligado. Como nós
não tínhamos watchdog nenhum, o laço terminava, o flag era ligado, e a partir
daí todo boot caía no WARNING.

Isso é **proteção deliberada da SNK**, não um bug do jogo: um clone com
watchdog quebrado se auto-denuncia. O comentário do próprio MAME em
`src/mame/snk/neogeo.cpp` documenta o truque citando *este mesmo trecho do
KOF99* — e foi de lá que saiu o número exato:

> "The watchdog timer will reset the system after ~0.13 seconds.
>  By cgfm's research, exactly 3,244,030 cycles (based on 24MHz clock)."
>
> `WATCHDOG_TIMER(config, "watchdog").set_time(attotime::from_ticks(3244030, NEOGEO_MASTER_CLOCK));`
> `map(0x300001, 0x300001).mirror(0x01fffe).w("watchdog", FUNC(watchdog_timer_device::reset_w));`

**Confirmação empírica no MAME** (breakpoints + `save` num arquivo, porque o
`printf` do debugger não sai no stdout): `$9CDA` é atingido, `$9CE4` **não**
é, e logo depois o PC aparece em `$C00402` (o vetor de reset da BIOS) — ou
seja, o watchdog realmente resetou a placa no meio do laço.

**Implementação:**
- `NEOGEO_WATCHDOG_CYCLES = 1622015` (= 3.244.030 ciclos de master / 2,
  porque `master_cycles` aqui conta ciclos do 68000 a 12 MHz).
- Qualquer escrita em `$300001`, espelhado em `$300000-$31FFFF` (o
  `.mirror(0x01fffe)` do MAME), rearma o prazo — tanto no caminho `.B`
  (que é o que os jogos usam) quanto no `.W`.
- Ao estourar, chama `reset_common(ng, false)`: reseta 68000, Z80, LSPC,
  YM2610 e latches, **sem apagar** WRAM, VRAM, paleta, sound RAM nem backup
  RAM — um pulso de /RESET não apaga DRAM, e o MAME também não zera essas
  regiões. O relógio (µPD4990A, com bateria própria) também segue de onde
  estava: só o power-on zera `master_cycles`.
- `NEOGEO_TRACE_WATCHDOG=1` loga cada reset.

**Resultado:** KOF99 agora passa da tela de aviso e roda o attract inteiro —
eyecatchers do K' e do Iori (com o fade-in correto), tela "SNK PRESENTS /
INSERT COIN", CREDIT 00. O watchdog dispara exatamente 1 vez, em `pc=009CE0`,
que é o laço acima.

**Sem regressão:** Double Dragon vb650 continua **0 pixels diferentes de
71680** contra o snapshot do MAME; `ctest` 5/5; samsho, mslugx, ssideki2 e
pbobblen dão boot igual (nenhum deles dispara o watchdog).

**Lição de método:** eu tinha apostado que o bug era o passo 2 da
descriptografia SMA (o único transform nunca verificado). Era hipótese
plausível e estava **errada**. O que resolveu foi seguir o código do jogo
instrução por instrução, do sintoma pra trás, até achar quem ligava o flag —
e então perguntar "por que no MAME esse trecho não executa?".

## Achado #74: M1 de até 512KB e o bankswitch de áudio do Z80 (que estava errado)

**Como apareceu:** montei uma varredura automática (`tools/varredura.sh`) que
roda os 170 ZIPs da pasta por N vblanks e salva um BMP de cada. De cara, **21
jogos nem carregavam**, todos com o mesmo erro: `M1 (sound): excede o limite
de 131072 bytes`. Nosso teto era 128KB; o real é 512KB.

| Tamanho de M1 | Exemplos |
|---|---|
| 256KB | bstars, garou, kof98, rbff2, sengoku3, nitd, superspy, maglord... |
| 512KB | mslug3, mslug5, kof2000/2001/2003, samsho5, svc, pnyaa... |

**E aí veio o bug de verdade.** Fui conferir no MAME se a nossa lógica de
banco aguentava M1 grande e descobri que ela estava **errada desde sempre**.
`neogeo_base_state::audio_cpu_banking_init` (`src/mame/snk/neogeo.cpp`):

```cpp
uint32_t const address_mask = (len - 0x10000 - 1) & 0x3ffff;
for (int region = 0; region < 4; region++)
    for (int bank = 0xff; bank >= 0; bank--)
    {
        uint32_t const bank_address = 0x10000 + ((bank << (11 + region)) & address_mask);
        m_bank_audio_cart[region]->configure_entry(bank, &ROM[bank_address]);
    }
```

Dois erros nossos:

1. **Faltava a base `0x10000`.** As 4 janelas de banco ($8000/$C000/$E000/
   $F000) olham a M1 *a partir de 64KB* — os primeiros 64KB são a área fixa
   mapeada em $0000-$7FFF do Z80. Nós fazíamos `banco * tamanho_da_janela` a
   partir do zero, ou seja, as janelas viam o código fixo.
2. **O recorte é um AND com `address_mask`, não `% m_size`.** Pra M1 de 512KB
   a máscara dá `0x2FFFF` — que nem é potência de 2 menos 1. É assim mesmo no
   MAME; copiei literal em vez de "melhorar".

E tinha um terceiro, mais sutil: os **bancos iniciais**. Nós zerávamos tudo; o
MAME põe `f000=0x1E, e000=0x0E, c000=0x06, 8000=0x02`, com este comentário:

> "set initial audio banks - THIS IS A HACK. Z80 banking is handled by the
>  NEO-ZMC chip in the cartridge (...). On the real chip, initial banks are
>  all 0. However, early cartridges with less than 64KB of Z80 code and data
>  don't have ROM banking at all."

Esses valores fazem cada janela apontar pro endereço "identidade"
($8000→$8000 etc), que é o que um cartucho antigo sem NEO-ZMC espera. Com
banco 0 em tudo, jogo antigo lia lixo.

**Implementado** como `neogeo_z80_banking_init()` + `neogeo_z80_set_bank()`,
cópia fiel do MAME. O `% m_size` continua só na leitura final, fazendo o papel
do `ROM_RELOAD` que o MAME usa pra espelhar M1 pequena dentro da região de
áudio (e garantindo que nunca lemos fora do buffer).

**Sem regressão:** Double Dragon vb650 segue **0 pixels diferentes de 71680**;
`ctest` 5/5.

**Lição de método (de novo a mesma):** eu só achei esse bug porque fui ler o
código do MAME pra uma pergunta *lateral* ("o banco aguenta 512KB?"). Ele
estava escondido há várias fases atrás de "mas os jogos tocam som, então deve
estar certo". Som "soando plausível" não é validação — e essa continua sendo a
maior lacuna de rigor do projeto.

## Achado #75: a CMC42 não era só do KOF99 — mais 6 jogos ficaram pixel-exatos

A varredura automática mostrou vários jogos desenhando **lixo gráfico**
colorido: `ganryu`, `sengoku3`, `zupapa`, `bangbead`, `nitd`, `preisle2`.
Todos usam a **mesma CMC42** que eu já tinha implementado pro KOF99 — muda só
a *chave* (o `extra_xor`) e a quantidade de pares de C ROM.

Chaves, de `src/devices/bus/neogeo/prot_cmc.h` do MAME:

| conjunto | jogo | chave |
|---|---|---|
| 070 | zupapa | `0xBD` |
| 251 | kof99 | `0x00` |
| 252 | ganryu | `0x07` |
| 254 | s1945p | `0x05` |
| 255 | preisle2 | `0x9F` |
| 259 | bangbead | `0xF8` |
| 260 | nitd | `0xFF` |
| 261 | sengoku3 | `0xFE` |

**Identificação pelo número do conjunto no nome do arquivo** (`252-c1.c1` →
252), e não pelo NGH como nos Achados #69/#70: aqui o NGH simplesmente não
serve — ganryu diz `0002`, e bangbead e sengoku3 dizem **os dois** `0020`.
Esses cartuchos têm a P ROM em claro (sem SMA); só o gráfico é cifrado.

Também generalizei a contagem de pares de C ROM (era fixa em 4, do KOF99):
ganryu/zupapa/bangbead/nitd têm 1 par, sengoku3 tem 2, preisle2 tem 3,
s1945p e kof99 têm 4.

### O sub-achado: 48MB não é potência de 2

`preisle2` continuou saindo lixo depois de tudo certo. O motivo está no recorte
de endereço do `cmc_prot_device::gfx_decrypt`:

```cpp
if (rom_size == 0x3000000) /* special handling for preisle2 */
{
    if (rpos < 0x2000000/4) baser &= (0x2000000/4)-1;
    else                    baser = 0x2000000/4 + (baser & ((0x1000000/4)-1));
}
else if (rom_size == 0x6000000) /* special handling for kf2k3pcb */
{ ... }
else /* Clamp to the real rom size */
    baser &= (rom_size/4)-1;
```

O `baser &= rom_size/4 - 1` só funciona se `rom_size` for potência de 2.
preisle2 tem 3 pares × 8MB × 2 = **48MB (0x3000000)**, que não é — e o MAME
trata o caso à mão. Copiei literal, incluindo o caso do `kf2k3pcb` que ainda
não uso, porque inventar uma fórmula "geral" aqui seria chute.

**Resultado (contra snapshot do MAME, vb1200):**

| jogo | antes | depois |
|---|---|---|
| zupapa | lixo | **0 px de 71680** |
| ganryu | lixo | **0 px de 71680** |
| bangbead | lixo | **0 px de 71680** |
| preisle2 | lixo | correto (só fase de attract) |
| nitd | lixo | correto (só fase de attract) |
| sengoku3 | lixo | correto (só fase de attract) |
| s1945p | preto | **ainda preto — em aberto** |

Total de jogos pixel-exatos com o MAME subiu de 27 para **30**.

**Em aberto:** `s1945p` nunca sai da BIOS (testado até 3000 vblanks, sem
watchdog envolvido). Pela desmontagem ele cai no caminho de PRIMEIRA
INICIALIZAÇÃO da BIOS (`$C11D84 -> $C17062`, o mesmo do Achado #38) em vez do
boot normal (`$C11D88 -> $C11028`), porque o scan da tabela de estatísticas em
`$D00124` não acha entrada não-zero. É a mesma família de falha dos 8 jogos
presos na grade de self-test — investigar junto.

## Achado #76: medir no ponto certo — 155 de 156 jogos batem com o MAME no boot

Comparar no vblank 1200 dava um número feio (só 30 de 156 idênticos) e
**enganoso**: no vblank 1200 cada jogo está num ponto qualquer do laço de
attract, e os dois emuladores podem estar os dois certos em momentos
diferentes. Foi exatamente o que aconteceu com o KOF99 no Achado #73.

A medição que presta é num ponto **determinístico** do boot. Refiz tudo no
**vblank 200** (`tools/varredura.sh` + `tools/varredura_mame.sh`, ambos com
`OUTDIR=` pra não misturar):

| diferença vs MAME | vb200 | vb1200 |
|---|---|---|
| 0 pixel | 1 | 30 |
| ≤ 2% | **154** | 14 |
| 2–50% | 0 | 60 |
| > 50% | **1** | 52 |

**155 de 156 dentro de 2%** — e os resíduos são de 57 a ~1400 pixels, sempre
concentrados num cantinho (no `burningf`, por exemplo, nas linhas 184-223,
colunas 24-56), o que é cara de defasagem de sub-frame num contador da BIOS,
não de erro de renderização.

Isso muda a leitura do projeto: o núcleo (68000, Z80, LSPC, paleta, banco,
IRQ, RTC, watchdog) está certo em praticamente todo o acervo. O que ainda não
sei medir é o **áudio** e o **gameplay longo**.

### O único que diverge cedo: irrmaze

`irrmaze` ("The Irritating Maze") dava 62349 de 71680 no vb200. Investigando,
achei uma coisa real: **o cartucho traz a PRÓPRIA BIOS** (`236-bios.sp1`) e
ela substitui a do sistema — é uma placa diferente (trackball), não um extra.
Nós carregávamos a euro por cima. Implementado: se o zip do cartucho contém
um `.sp1`, ele vira a BIOS (`NEOGEO_TRACE_BIOS=1` mostra qual foi escolhida).
Verificado que funciona (`[bios] usando '236-bios.sp1' (do cartucho)`) e que
não muda nada nos outros (`doubledr` segue em `sp-s2.sp1`).

**Mas isso NÃO resolveu o irrmaze**: continua 62349, e o número é *idêntico*
de vb190 a vb240 — ou seja, não é defasagem de tempo, é um **deslocamento
estático** da imagem (a nossa começa no canto, a do MAME aparece deslocada
~64px com barras escuras em cima e embaixo). Causa ainda não diagnosticada.
Mantive a correção da BIOS mesmo assim porque ela está certa por si só.

**Sem regressão:** `doubledr` vb650 segue 0 de 71680; `ctest` 5/5.

## Achado #77: o fundo sumia — sprite de tela cheia usava a linha errada

Relato do usuário: "notei ausência de background". No Neo-Geo **não existe
camada de fundo**: o fundo É feito de sprites, os que têm `rows >= 0x20`
("aparece em todas as linhas de varredura", Achado #51). E era justamente
esses que a gente desenhava errado.

O MAME calcula, pra cada linha:

```cpp
const int sprite_line = (scanline - y) & 0x1ff;
```

ou seja, linha da TELA menos o Y do sprite, com volta em 512. No nosso
caminho normal isso já saía certo (o laço de `y_offsets` parte de `y_pos`).
Mas no caminho `all_scanlines` a gente varria com `off = 0`:

```c
if (all_scanlines) y_offsets[0] = 0; /* varre a tela inteira direto */
...
u32 sy = (u32)sy_s;   /* <- virava a linha da TELA, sem o Y do sprite */
```

Com isso o índice de tile saía completamente errado — e o fundo sumia (ou
virava listra). Corrigido pra `sy = (screen_yu - y_pos) & 0x1FF`.

Junto faltava a dobra "vai e volta" pra sprite com mais de 32 tiles:

```cpp
if (rows > 0x20) {
    zoom_line = zoom_line % ((zoom_y + 1) << 1);
    if (zoom_line > zoom_y) { zoom_line = ((zoom_y+1)<<1) - 1 - zoom_line; invert = !invert; }
}
```

### Como cheguei lá (e o que descartei pelo caminho)

Não saí chutando. Em ordem:

1. **Limite de 96 sprites/linha?** Criei `NEOGEO_SPRITE_LIMIT=0` pra desligar.
   Resultado idêntico → **descartado**.
2. **O jogo está gravando os tiles?** Criei `NEOGEO_TRACE_VRAM=<lo>-<hi>` pra
   logar escritas numa faixa de palavras com o PC de quem escreveu. Estava
   gravando, sim → o problema era no render, não na CPU.
3. **A tabela de zoom está certa?** Li a `000-lo.lo` direto: em `zoom_y=255`
   ela é identidade (`zoomy[0xFF00|n] == n`). Confirmado.
4. Sobrou o cálculo de `sprite_line` — e aí o bug estava à vista.

### Resultado (vb1200, contra snapshot do MAME, 156 jogos comparáveis)

| | antes | depois |
|---|---|---|
| 0 pixel | 30 | **38** |
| ≤ 2% | 14 | 18 |
| 2–50% | 60 | 60 |
| > 50% | 52 | **40** |

**19 jogos melhoraram muito, 0 pioraram.** Oito foram a zero:
`bjourney` (69208→0), `diggerma`, `legendos`, `marukodq`, `ncombat`,
`ssideki3`, `ssideki4`, `zedblade`. Outros grandes saltos: `samsho4`
(59580→2404), `mutnat` (60290→6913), `3countb` (58932→9636), `lresort`
(25379→168), `gururin` (39307→7253).

## Achado #78: tile além da C ROM — o MAME zera, não repete

Ao investigar o #77 achei outra divergência. Quando o código de tile passa do
tamanho da C ROM carregada, nós fazíamos `% tamanho_real`. O MAME não:

```cpp
/* get mask based on the length rounded up to the nearest power of 2 */
```

Ele arredonda a região PRA CIMA até a próxima potência de 2, mascara com ela,
e deixa a sobra **zerada** — então o pedaço que cai além do ROM de verdade sai
**transparente**, não repetindo outro tile. Só muda alguma coisa quando o
conjunto de C ROMs não é potência de 2 (`maglord` = 3 × 512KB = 1,5MB);
quando é (a maioria), `%` e `&` dão o mesmo resultado.

## Em aberto: ssideki e wh2

`ssideki` continua errado (fragmentos do estádio espalhados) e `wh2` continua
com fundo preto. **Não é o #77** — medi. No `ssideki` os códigos de tile que
estão na NOSSA VRAM (`0x9010`...) passam do espaço real de tiles da C ROM
(4MB = `0x8000` tiles) e precisam dar a volta. Testei forçar "transparente em
vez de dar a volta" (`NEOGEO_TILE_POW2`, já removido): a tela ficou vazia, o
que **confirma que esses códigos são mesmo altos demais**. Como o MAME
desenha a cena certa a partir do mesmo mecanismo de recorte, a conclusão é que
o conteúdo da nossa VRAM diverge — ou seja, o problema é de estado de
execução, não de renderização. Próximo passo: comparar a VRAM contra o MAME
num ponto sincronizado.

## Achado #79: SMA genérico — Garou e Metal Slug 3

O chip SMA do KOF99 (#72) é o mesmo em Garou, Metal Slug 3 e KOF2000; muda
por jogo: a permutação de bits dos dados, o tamanho de bloco da permutação de
endereço (0x800 / 0x8000 / 0x10000), a origem da reconstrução do banco fixo,
a **ordem** das etapas (garou/mslug3 reconstroem o fixo ANTES), a tabela de
bankswitch e os endereços dos registradores (bank kof99 `$2FFFF0`, garou
`$2FFFC0`, mslug3 `$2FFFE4`, kof2000 `$2FFFEC`; random em pares diferentes).
Tudo de `prot_sma.cpp` + `set_slot_idx` do MAME. Virou uma tabela
`sma_desc_t` e o caminho do KOF99 passou a usar o genérico —
**conferido byte a byte: 0 bytes de diferença** e `bank_base` igual nos 65536
valores de seletor (`NEOGEO_SMA_SELFTEST=1`).

Detalhe que só apareceu lendo o `ROM_START`: esses três jogos declaram
`ROM_REGION(0x80000, "fixed")` — **"larger char set"**, fix layer de 512KB em
4 bancos. O banco de cada tile sai de dados que o jogo escreve na VRAM
(`$7500`/`$7580`), com dois esquemas (`FIX_BANKTYPE_GAROU`/`_KOF2000`)
transcritos de `draw_fixed_layer`.

Resultado: garou e mslug3 **0 px no vb200**, attract com texto certo.

## Achado #80: CORREÇÃO do Achado #74 — eu tinha introduzido uma regressão de áudio

No #74 li `bank_address = 0x10000 + (...)` como "as janelas olham 64KB
adiante na M1". **Errado.** As macros de romset do MAME carregam a M1 duas
vezes na região de áudio:

```cpp
#define NEO_BIOS_AUDIO_128K(name, hash) ...
    ROM_REGION( 0x30000, "cslot1:audiocpu", 0 )
    ROM_LOAD( name, 0x00000, 0x20000, hash )
    ROM_RELOAD(     0x10000, 0x20000 )
```

Então `0x10000 + x` na região é `M1[x]`. Sobre o nosso arquivo cru, a fórmula
certa é `(bank << (11+region)) & ((m_size-1) & 0x3FFFF)`, sem somar nada.

**Prova** (RAM do Z80 contra o MAME no vb300, `tools/z80ram_vs_mame.sh`):

| jogo | fórmula errada (#74) | corrigida |
|---|---|---|
| maglord | **2048 de 2048** (driver de som morto) | 64 |
| mslug | 299 | 56 |
| pbobblen | 298 | 37 |

Eu tinha documentado como correção algo que quebrou o som de parte do
acervo. Fica aqui registrado. Lição: **mudança "confirmada no fonte" ainda
precisa de medição** — eu li o código certo e interpretei errado.

## Achado #81: CMC50 — KOF2000

Mesmo algoritmo do CMC42 com outro conjunto de 9 tabelas, mais a M1 cifrada:
a chave é a **soma de bytes (16 bits) dos primeiros 64KB** da M1 cifrada
(`util::sum16_creator::simple`, conferido em `hashing.cpp`), usada pra
embaralhar endereços. As tabelas foram **extraídas por script** direto do
`prot_cmc.cpp` (sem redigitar); as 9 do CMC42 conferidas pelo mesmo método:
idênticas. Resultado: KOF2000 no attract; RAM do Z80 a 16 bytes do MAME
(mesma ordem do Double Dragon, 28) — M1 errada daria RAM toda diferente.

## Achado #82: KOF98

Criptografia de 1998, anterior à SMA: pares de bytes reposicionados em blocos
de 0x200 nos primeiros 2MB, depois a sp2 empurrada de `$200000` pra
`$100000`. Mais um chip Altera que sobrepõe `$000100/$000102` conforme o que
o jogo escreve em `$20AAAA` (0x0090 → `00C2 00FD`, 0x00F0 → `NEO-`).
**P ROM: 0 bytes diferentes** do MAME (fixo e banco).

## Achado #83: família CMC50 / NEO-PCM2 / NEO-PVC — 11 jogos

rotd, pnyaa, mslug4, jockeygp, kof2001, kof2002, samsho5, samsh5sp, mslug5,
svc, kof2003. Uma tabela por cartucho (`cmc50_cart_t`) com o que vem do
`decrypt_all` e do `ROM_START` de cada um:

- **P ROM**: nada (kof2001...), blocos de 512KB (`prot_kof2k2.cpp`), ou a
  descriptografia NEO-PVC (XOR + troca de bits + troca de blocos,
  `prot_pvc.cpp`). mslug5/svc/kof2003 usam `ROM_LOAD32_WORD_SWAP`: P1 e P2
  **intercalados palavra a palavra**.
- **Endianness**: o MAME usa `BYTE_XOR_LE(i)` porque guarda a ROM na ordem
  do host. Como a nossa é sempre big-endian, a tradução é a de um host
  big-endian: `BYTE_XOR_LE(a) -> a ^ 1`. Conferido que a expressão dá o mesmo
  resultado nos dois tipos de host.
- **V ROMs** (NEO-PCM2): `decrypt(valor)` permuta palavras; `swap(valor)` é
  a versão nova com XOR e endereço embaralhado sobre 16MB.
- **Fix layer**: o tamanho declarado manda. rotd/samsho5/mslug5 têm tipo de
  banco 1 no device mas região de 0x20000 — **sem banco** na prática
  (`banked = addr_mask > 0x1ffff`).
- **NEO-PVC**: 0x1000 words de RAM sobre `$2FE000-$2FFFFF` (leitura e
  escrita). `$FF0` desempacota uma cor em `$FF1/$FF2`, `$FF4/$FF5` empacota
  em `$FF6`, e qualquer escrita `>= $FF8` faz o bankswitch e o chip reescreve
  os registradores. É a proteção: o jogo confere as contas de cor.
- **rotd**: um arquivo de 8MB carregado direto em `$000000`, sem
  `ROM_CONTINUE` — exceção à regra "arquivo único grande = troca de metades"
  do Achado #67.
- **jockeygp**: região de 2MB com o segundo MB em 0xFF, e 8KB de RAM de
  cartucho em `$200000` (mesmo mecanismo do V-Liner).

**Validação** (`tools/prom_vs_mame.sh`): as 13 P ROMs (estas 11 + garou +
kof98) têm **0 bytes de diferença** na área fixa. No banco, as únicas
diferenças são exatamente as janelas de proteção (os 8KB do PVC, os 6 bytes de
registrador SMA) — o MAME lê o registrador, o script comparou com a ROM crua.

## Achado #84: a backup RAM tem trava de escrita

O rotd continuava na tela WARNING. Rastreando: ele cai no mesmo laço de
espera longo do KOF99 (`$55E`)... e **o watchdog não dispara nem no MAME**
(confirmado com breakpoint: `$568` é atingido, reset não). Mesmo assim o MAME
não mostra o aviso. Dump em `$1AA58`: `$D00100 = 0000` — a escrita do flag
"proteção falhou" **não pegou**.

Motivo, em `neogeo.cpp`:

```cpp
m_systemlatch->q_out_cb<6>().set(FUNC(ngarcade_base_state::set_save_ram_unlock));
void ngarcade_base_state::save_ram_w(offs_t offset, uint16_t data, uint16_t mem_mask) {
    if (m_save_ram_unlocked) COMBINE_DATA(&m_save_ram[offset]); }
```

O bit 6 do latch de sistema destrava a backup RAM (`$3A001D` destrava,
`$3A000D` trava) e escrita com a trava fechada é **ignorada**. Nós gravávamos
sempre.

Efeito colateral inesperado e muito bom: no vb200, os jogos idênticos ao MAME
pularam de **1 para 152 de 156**. Os resíduos de 57-1400 pixels "num
cantinho" que eu tinha atribuído a defasagem de sub-frame (#76) eram na
verdade isto — a BIOS gravando onde não devia e mostrando um contador
diferente. Mais uma hipótese minha que estava errada.

## Achado #85: o relógio µPD4990A de verdade (e o fim do contorno do #38)

O s1945p reiniciava em loop. Comparando traces instrução a instrução contra o
MAME (`trace arq,maincpu,noloop` × `NEOGEO_TRACE_FILE` do `debug_jump`),
ancorando fora de laço (dentro de um laço periódico o alinhamento fica
ambíguo e dá divergência falsa — caí nisso uma vez), a primeira diferença era
em `$C11E0A`: a comparação da data do RTC com a data salva em `$D00104`. O MAME
lê a data **real do relógio do PC** e cai em "novo dia"; nós nunca tínhamos
implementado a leitura de data, e o Achado #38 forçava `00 00 00` dos dois
lados.

Implementado o chip completo em `core/src/system/upd4990a.c`, transcrito de
`upd1990a.cpp` (variante 4990A, modo serial, como o Neo-Geo liga): registrador
de deslocamento de 7 bytes, comandos no STB (REGISTER_HOLD, SHIFT, TIME_SET,
TIME_READ, TP 64Hz..60s, TEST), relógio que avança 1 s, `DATA_OUT` em
STATUS_A bit 7 (antes sempre 0) e o TP em bit 6. A "fase reiniciada no STB"
do Achado #65 deixou de ser regra genérica e virou consequência do comando.
`NEOGEO_RTC_DATE="AAAA-MM-DD HH:MM:SS"` fixa a data pra teste. O contorno do
#38 foi removido.

## Achado #86: leitura do YM2610 conforme o ymfm

Três coisas erradas na leitura das portas do chip pelo Z80:
- **flag "ocupado"** nunca era reportado. O ymfm marca ocupado por
  `32 × prescale(6) = 192` clocks de 8 MHz = **24 µs** após cada escrita de
  dado aceita;
- **porta $06** devolvia o status de timer. É `read_status_hi`: os flags de
  **fim de amostra** do ADPCM (bits 0-5 canais A, bit 7 ADPCM-B, com a cópia
  "ao vivo" escondida no bit 6), mascarados pelo registrador `$1C`;
- **porta $05** não existia (registradores do SSG, 0xFF em $0E/$0F, ID=1 em
  $FF); dado escrito na porta errada pro endereço latchado agora é ignorado,
  como no chip (latch único de 9 bits).

## Achado #87: o Z80 andava mais rápido que o real

`Z80_Run(done/3)` a cada instrução do 68000 jogava fora o resto da divisão E o
excesso do lote (o núcleo executa instruções inteiras até passar do alvo e zera
o contador na próxima chamada). Agora há um crédito de ciclos que carrega a
diferença.

## Achado #88: o YM2610 agora anda no tempo emulado

Antes, FM/ADPCM/SSG só avançavam quando o frontend pedia áudio — no ritmo da
placa de som do PC. O estado que o Z80 lê (fim de amostra) dependia disso, e
nas ferramentas sem som o ADPCM nunca tocava. Agora uma amostra é gerada a cada
216 ciclos do 68000, dentro do laço das CPUs, num buffer circular que o
frontend (e o `debug_audio`) só esvazia — como o `m_stream->update()` do MAME
antes de cada leitura.

## Achado #89: a NMI do Z80

No MAME a NMI é a saída de um `INPUT_MERGER_ALL_HIGH` com duas entradas —
"comando pendente" e "NMI habilitada" (portas $08/$18) — na borda de subida.
Três diferenças corrigidas: (a) a NMI sai quando as duas ficam verdadeiras em
**qualquer ordem** (religar com comando pendente dispara); (b) no reset o MAME
**desliga** a NMI (nós ligávamos); (c) escrita na porta $00 zera o latch
(`clear_w`). O `test_neogeo` codificava o comportamento antigo e foi
atualizado (o programa Z80 do teste agora liga a NMI com `OUT ($08)`, como um
driver real).

## Achado #90: o 68000 não seguia a tabela de tempos da Motorola

A raiz de verdade da diferença de sincronia. Medindo ciclos por instrução
(`NEOGEO_TRACE_CYC` no `debug_jump`), a volta da BIOS esperando o Z80

```
move.b D0,$300001.l  /  move.b $320000.l,D0  /  subq.b #1,D0  /  beq  /  dbra
```

custava **42 ciclos** aqui (8+8+8+8+10) contra **54** no hardware
(16+16+4+8+10). O núcleo tinha atalhos com `used = 8u` pra quase tudo — o
68000 andava ~25% rápido demais em relação ao Z80, aos timers e ao vídeo.

Correção sem redigitar tabela: compilei e rodei o **próprio `m68kmake` do
Musashi**, parseei a tabela de handlers do `m68kops.c` gerado replicando
`m68ki_build_opcode_table`, e gerei `m68k_cycles_000.inc` (65536 entradas,
conferidas com a tabela da Motorola: `move.b D0,(xxx).L`=16,
`move.b (xxx).L,D0`=16, `subq.b`=4, `mulu`=38+2n, `divu`=140). Por cima, os
ajustes dinâmicos que o Musashi aplica no 68000: Bcc não tomado (−2 byte /
+2 word), DBcc desviou (−2) / expirou (+2), Scc verdadeiro (+2),
deslocamento `2×contagem`, MOVEM `4×n`/`8×n`, MULU 2 por bit ligado, MULS 2
por transição, e o tempo de exceção quando a instrução gera uma.

**Resultado na espera da BIOS pelo Z80:**

| | voltas |
|---|---|
| MAME | 544 |
| nosso, antes de tudo | 181 |
| só com o crédito do Z80 (#87) | 697 |
| **com a tabela de tempos (#90)** | **542** |

Double Dragon vb650 continua **0 pixels**; `ctest` 5/5.

## Achado #91: o TP de 4096 Hz em ponto fixo

Depois do reset por watchdog do garou, o MAME chegava ao comando 7 do relógio
0,5 s antes de nós. Reconstruindo os comandos do MAME a partir das escritas em
`$380050` (watchpoint + `logerror` com `totalcycles`), o comando 8 (TP de 1 s)
saía no mesmo ciclo nos dois (18.590.852 × 18.590.818 após o reset). O que
mudava era o **nível do TP** nesse instante: antes dele o TP estava em 4096 Hz,
meio-período de **1464,84375** ciclos, que eu tinha arredondado pra 1464 — em
~18 mil inversões a paridade vira. Prazos agora em ponto fixo 16.16. Comando 7
após o reset: **+36.591.816** contra **+36.591.809** do MAME (7 ciclos).
kof99, samsho4 e samsho5 foram a 0 px no vb1200.

## Achado #92: DIP switch — um contorno antigo meu

`NEOGEO_DIPSW_DEFAULT` era `0xFB` (bit 2 zerado) "pra pular uma espera de
debounce que nunca terminava" — numa fase com relógio e timing errados. No
MAME o bit 2 é *Controller: Joystick / Mahjong Panel*, e a BIOS (`$C18484`)
com ele zerado grava **tipo de controle 3 (mahjong)** em `$10FD94`. Agora
`0xFF`, o padrão do MAME. O boot continua normal.

## Achado #93: interrupções e registradores de vídeo como no MAME

Portado de `neogeo.cpp`/`neogeo_v.cpp`:
- **IRQ de vblank** 29 ciclos depois do início do vblank
  (`NEOGEO_VBLANK_IRQ_HTIM` = 58 mclk), não na borda;
- **IRQ de timer / raster (nível 2)** — **não existia**. Contador de 32 bits
  (`$3C0008/$3C000A`), período `(contador+1) × 2` ciclos, e as 4 flags do
  `LSPCMODE` (habilita, recarga relativa na escrita, recarga no vblank em
  +573 ciclos, repetição);
- **IRQ3** pendente no power-on até a BIOS confirmar;
- **`LSPCMODE` na leitura**: `(v_counter << 7) | contador_de_animação`.
  Medido no MAME: no início do vblank o feixe está na **linha 240** e o
  registrador lê `$F801`. Nós tratávamos o vblank como linha 0 e devolvíamos
  nos bits baixos o que a CPU escreveu;
- **contador de auto-animação** avança na linha 256 (`NEOGEO_VSSTART`);
- leituras de `$3C0000` devolvem o **dado** da VRAM (buffer lido na hora em
  que o endereço é escrito), escrita só no byte baixo é ignorada;
- reset por watchdog **não** zera a fase do frame (a tela não para).

## Achado #94: quando o 68000 enxerga a IRQ

No código gerado do microcódigo do MAME (`m68000-sif.cpp`), toda instrução
faz `m_next_state = m_int_next_state` no começo do **último passo**, a
pré-busca de 4 ciclos. IRQ que chega nos últimos 4 ciclos só é atendida depois
da instrução seguinte. Implementado guardando o instante de cada fonte de IRQ
e só expondo ao núcleo as ligadas até `fim_da_instrução − 4`. Na espera de
vblank da BIOS do ironclad, os 6 primeiros frames passaram a interromper
exatamente na mesma instrução que o MAME.

## Achado #95: bug meu no #90 — desvio "pra instrução seguinte"

Eu decidia "Bcc não tomado" por `PC == op_pc + 4`. Só que um `bne` word com
deslocamento 2 **desvia** exatamente pra `op_pc + 4` (a BIOS tem um em
`$C11692`). Agora decide pela condição (Bcc não altera flags); DBcc idem.

## Achado #96: a tabela do Musashi tem furos — medidos contra o MAME

Trace do MAME com `totalcycles` **em cada instrução**
(`trace arq,maincpu,noloop,{tracelog "%d ",totalcycles}`) em 6 jogos, ~2
milhões de instruções, comparando o custo de cada opcode com a nossa tabela
(`tools/cycles_vs_mame.py`). **39 opcodes** discordavam, todos em poucas
famílias — justamente as notas de rodapé da tabela da Motorola que o Musashi
não aplica de forma consistente (dá pra ver pelas irmãs: `addq.w` em An = 4
mas `subq.w` em An = 8; `andi.l` = 14 mas `ori/eori/addi/subi.l` = 16):

| família | MAME | Musashi |
|---|---|---|
| `add.l/sub.l/and.l/or.l` Rn→Dn, `adda.l/suba.l` Rn→An | 8 | 6 |
| `addq.w` em An | 8 | 4 |
| `adda.w/suba.w #imm,An` | 12 | 14 |
| `andi.l #,Dn` | 16 | 14 |
| `bclr #n,Dn` | 12 | 14 |
| `bset/bchg #n,Dn` | 10 | 12 |

`tools/m68k_cycles_mame_fix.py` aplica as regras por família sobre a saída
pura do `m68kmake` (guardada em `m68k_cycles_musashi.inc`): 752 entradas
corrigidas, e o comparador passou a dar **0 opcodes discordantes**.

## Achado #97: C ROMs carregadas em duas metades (`ROM_CONTINUE`) — AOF

Relato do usuário: "aof está com problemas gráficos". Os personagens grandes
saíam embaralhados, com cenário e HUD corretos e no mesmo quadro do MAME. No
`ROM_START(aof)`:

```
ROM_LOAD16_BYTE( "044-c1.c1", 0x000000, 0x100000 )
ROM_CONTINUE(                 0x400000, 0x100000 )
ROM_LOAD16_BYTE( "044-c3.c3", 0x200000, 0x100000 )
ROM_CONTINUE(                 0x600000, 0x100000 )
```

A segunda metade de cada chip de 2MB vai **4MB adiante** na região; nós
concatenávamos os arquivos inteiros, trocando o meio da área de gráficos.
Oito jogos do acervo usam isso: aof, 3countb, fatfury2, kotm2, sengoku2,
viewpoin, ssideki e wh1 (inclusive o estádio embaralhado do Super Sidekicks
que ficou em aberto no #78 — o diagnóstico de "VRAM divergente" de lá estava
errado). Tabela gerada por script do `ROM_START` de cada um.

| jogo | antes | depois (vb1200) |
|---|---|---|
| aof | 17554 | **0** (vb1500) |
| 3countb | 1241 | **0** |
| ssideki | 69802 | 7339 |
| fatfury2 | 46189 | 7347 |
| kotm2 | 27952 | 5329 |

**Velocidade** (o usuário achou "muito rápido"): medido no frontend SDL,
1180 frames em 20,03 s = **58,9 fps = 99,5%** dos 59,19 Hz do Neo-Geo. O
áudio abre com conversão automática do SDL (sem mudança de tom). A sensação
provavelmente vinha dos gráficos quebrados; se persistir, o título da janela
mostra "% da velocidade real".

**(Resolvido no #100/#101.)** sengoku2 não é problema de C ROM — reiniciava com erro de
endereço por volta do vb1100. O contador de frames da BIOS (`$10FE8A`) bate
com o MAME até o vb1000; a divergência vem de uma IRQ de vblank atendida numa
instrução diferente (mesma família de corrida dos Achados #93/#94).

## Achado #98: o frontend rodava a 182% — o usuário estava certo

No #97 eu medi "58,9 fps = 99,5%" e concluí que a velocidade estava certa. **Erro
de medição**: eu medi frames MOSTRADOS por segundo, não frames EMULADOS por
frame mostrado. O usuário insistiu ("parece uns 25% mais rápido") e estava
certo — era pior.

`core/tools/check_frame_speed` mede ciclos do 68000 por chamada de
`neogeo_run_frame_render` (o caminho do frontend): **600 chamadas emulavam 1092
frames = 182%**. A causa: o laço usava `cycles_since_vblank`, que dá a volta no
fim do frame; na última linha ele virava com poucos ciclos de sobra e o "garante
que o frame fechou" rodava `202752 − sobra` — quase um frame inteiro a mais,
sem desenhar. As ferramentas de teste usam `neogeo_run_until_vblank` e nunca
passaram por esse código, então nenhuma validação contra o MAME pegava. O
áudio, gerado em dobro, estourava a fila e o frontend descartava blocos — a
música também "corria".

Correções:
1. alvos **absolutos** em `master_cycles`, ancorados no vblank: exatamente
   202.752 ciclos por chamada, sem deriva (600 → 600,00; 3000 → 3000,00);
2. a linha desenhada é a linha crua **(240 + fatia) % 264** — o vblank começa
   na 240, então a linha visível 16 só é varrida 40 linhas depois; antes o
   estado era amostrado 24 linhas cedo (errado pra efeitos de raster);
3. ritmo de parede com **prazo absoluto de alta resolução** (o `SDL_Delay`
   truncava ms e dava 60,1 fps). Medido dentro do frontend, sem o tempo de
   carga: **1200 frames em 20,275 s = 59,186 fps = 100,00%**.

Imagem do frontend (render linha a linha) no frame 650 do Double Dragon contra
o MAME: **0 pixels diferentes**.

**Lição:** validar o caminho que o usuário realmente usa. Todas as
comparações contra o MAME passavam por outra função.

## Achado #99: áudio validado contra o MAME — 4 bugs no YM2610

Gravando a saída do MAME (`-wavwrite -samplerate 55555`) e a nossa
(`debug_audio`), a música do Double Dragon **calava após ~1 s** e saía mono.
Método pra separar "Z80 escreve errado" de "chip sintetiza errado":
`NEOGEO_TRACE_YM=1` agora grava o instante (`t=` ciclos do 68000) de cada
escrita; o mesmo trace é reproduzido **no ymfm original** (compilado à parte,
fidelidade `OPN_FIDELITY_MED` igual ao MAME, V ROM via `NEOGEO_DUMP_V`) e no
nosso chip sozinho (`core/tools/debug_ym_replay`). O ymfm com as nossas
escritas já correlacionava 0,95 com o MAME → driver/Z80 certos, bug na síntese.

1. **Ataque do envelope FM invertido.** No ymfm `m_env_attenuation` é
   `uint16_t` e `(~att * inc) >> 4` roda em 32 bits: truncado, *subtrai* e
   leva a atenuação a 0. Nós usávamos `~att & 0x3FF`, que *somava* — depois
   que um operador chegava em 0x3FF, nenhuma nota voltava a soar.
2. **Reset do operador**: att 0x3FF e estado RELEASE (era 0/ATTACK, toda nota
   começava num estouro); `$B4-$B6` dos dois bancos = 0xC0 (L+R).
3. **`EG_QUIET` = 0x380** (era 0x200) + `prepare` preguiçoso como no ymfm (só
   após escrita FM que não seja `$27`, ou a cada 4096 amostras; canal inativo
   não soma saída).
4. **ADPCM-B mudo**: sem região `adpcmb` o MAME mapeia o ADPCM-B na V ROM do
   ADPCM-A. Leituras fora da ROM devolvem 0 (espaço de 24 bits, sem volta).

Mixagem como `neogeo.cpp`: `0,98·clamp16(FM+ADPCM) + 0,84·SSG`.

Resultado:
- nosso chip × ymfm com o mesmo trace: **0 amostras diferentes** em 16 s
  (doubledr, neobombe); samsho 3 e ssideki2 13 amostras no fim;
- saída do emulador × MAME: RMS igual a ±0,02 dB nos 4 jogos; forma de onda
  por janela de 0,5 s com correlação **0,99–1,000**, atraso de 1–16 amostras
  (<0,3 ms, variando por trecho = fase das escritas do Z80; deriva de ~6 ppm
  é o `-samplerate 55555` inteiro do MAME contra 55555,56 Hz reais).
- aberto: no início do jingle da BIOS (t≈5 s) há um trecho com atraso de ~475
  amostras (8 ms) contra o MAME — sincronia de partida do Z80.

## Achado #100: `s1945p` RESOLVIDO — registrador de endereço do 68000 não é de 24 bits

O jogo nunca saía da BIOS (aberto desde o Achado #75). A cadeia, achada
comparando o MAME instrução a instrução:

1. `$C11028` (boot frio da BIOS: zera as 64KB de WRAM e os dois bancos de
   paleta) rodava **1 vez em 900 vblanks no MAME e repetidamente no nosso**,
   a cada ~220 vblanks — o jogo se reinicializava sozinho;
2. a re-entrada vinha de uma **exceção de ERRO DE ENDEREÇO (vetor 3)** no
   código do cartucho, em `$023D98` (`move.l (A0)+,(A1)+`) com `A0 = 3`;
3. `A0` vinha de uma tabela de ponteiros que o cartucho monta na WRAM
   (`$10CCF2`), copiada da janela de banco da P ROM por `$00FA2E`:
   ```
   00FA2E: movea.l (A2), A0     ; le um ponteiro da tabela na P ROM
   00FA30: moveq   #-1, D5
   00FA32: cmp.l   A0, D5
   00FA34: beq     $fa4e        ; $FFFFFFFF = fim da tabela, PULA
   00FA42: move.l  A0, (A1,D0.l); senao guarda como ponteiro valido
   ```
4. a P ROM (5MB, `p1` em 0 + `sp2` em 0x100000), o banco (`0x100000`) e o
   conteúdo da janela `$200000-$2FFFFF` conferidos **byte a byte contra o
   MAME: 0 diferenças**. Mesmo assim `A0` saía `$00FFFFFF` em vez de
   `$FFFFFFFF` — o valor certo, **mascarado em 24 bits**.

**A causa:** o nosso núcleo do 68000 guardava os registradores de endereço
com `mask24()`. No 68000 real só o *barramento* de endereço tem 24 bits; os
registradores `A0-A7` são de **32 bits cheios** (`MOVEA.L` não trunca nada,
`MOVEA.W` estende o sinal). Com o topo zerado, o `cmp.l` contra
`$FFFFFFFF` nunca dava igual, o terminador da tabela virava "ponteiro
válido" (`$00FFFFFF`), o cartucho desreferenciava endereço ímpar e a BIOS
respondia com o boot frio — em loop eterno.

**Correção:** removido `mask24()` de todas as 34 escritas em registrador de
endereço (`MOVEA`, `MOVEM`, `(An)+`/`-(An)`, `ADDA/SUBA`, `ADDQ/SUBQ`,
`LEA`, `LINK/UNLK`, `EXG`, `CMPM`, pilha, `USP/SSP`). O mascaramento
continua onde de fato existe no hardware: na hora do acesso
(`mem_read8/16/32`, `mem_write8/16/32` e a busca de opcode já faziam
`mask24` no endereço).

**Resultado:** `s1945p` boota e roda igual ao MAME — logo da Psikyo no
vblank 300 com **0 pixels de diferença de 71680**, e a partir daí a abertura
e o modo de demonstração rodam normalmente.

Sem regressão, medido depois da correção:
- varredura dos 156 jogos no vb200 contra o MAME: **152 pixel-exatos**
  (mesmo número de antes), piores resíduos `kizuna` 480px (0,7%), `whp`
  413px, `kof95` 191px — nenhum jogo piorou;
- `ctest` 5/5; áudio do `doubledr` contra o MAME inalterado (correlação
  0,9512, RMS igual).

**Achado colateral (irrmaze):** o "deslocamento estático" do Achado #76 é
falso alarme de medição — o snapshot do MAME do `irrmaze` é **504x460**, não
320x224 (a placa do trackball tem outra configuração de vídeo). A comparação
nunca foi válida; refazer com a resolução certa antes de investigar.

**Lição de método:** o sintoma ("trava na BIOS") estava a 4 camadas de
distância da causa (um `&` a mais no núcleo da CPU). O que encurtou o
caminho foi sempre perguntar *"o MAME faz isso quantas vezes?"* — foi
contar `$C11028` (1 vez lá, 3 aqui) que transformou "trava" em "reinicia
sozinho", e daí em diante cada passo foi medido, não deduzido.

## Achado #101: sengoku2 — três diferenças de hardware medidas contra o MAME

**1. O reboot do sengoku2 era o mesmo bug do #100.** O "erro de endereço
~vb1100" sumiu com os registradores de endereço de 32 bits: o jogo roda o
attract inteiro, imagem praticamente idêntica ao MAME (271 px no vb1100).

**2. Porta TEST (`$300081`) valia `$BF`, o MAME dá `$80`.** Achada porque
`$10FEDE` divergia em TODO jogo desde o boot (s1945p e sengoku2):
`C1273E: move.b $300081,D0 / not.b D0 / move.b D0,($bde,A5)`. No MAME:
`PORT_BIT(0x003f, IP_ACTIVE_HIGH, IPT_UNUSED)` (= 0), bit6 "sense JAMMA" (= 0),
bit7 serviço ativo-baixo (= 1). Com `$80`, WRAM do sengoku2 exata até o vb750
(antes divergia no vb600).

**3. Tempo do 68000 que faltava no núcleo** — achado com uma ferramenta nova,
`tools/trace_diverge.py` (alinha trace do MAME com `totalcycles` e o nosso
`NEOGEO_TRACE_CYC`, casa entradas de IRQ e acusa a primeira instrução com PC
ou custo diferente):

- **IACK autovetorado sincroniza com o clock E.** No MAME
  (`m68000_device::vpa_sync`, `default_autovectors_map`) a leitura do vetor
  começa 10 ciclos após o início da exceção e espera a próxima borda do clock
  E (período 10), pulando uma se faltar menos de 3, +1 ciclo
  (`after_delay`). A entrada de IRQ custa 44 + 5..14, não 44 fixo. Conferido
  nos dois casos medidos (56 e 52 ciclos). Implementado em
  `m68k_core.c` (`vpa_sync`, `host_time`, `last_vpa_time`).
- **DIVU/DIVS: custo depende dos operandos.** Transcrevi o fluxo de controle
  do microcódigo (`divu_w_ds_dd_if`, `divs_w_ds_dd_if`, m68000-sif.cpp) com as
  funções de ALU exatas de m68000.h (`alu_and/sub/subc/sla0/sla1`); o custo do
  modo de endereçamento continua vindo da tabela. Antes era fixo (144 onde o
  MAME dava 140).

**Resultado:** trace do sengoku2 a partir do vb750 **idêntico ao MAME, ciclo a
ciclo, por 496.787 instruções e 24 interrupções** (antes divergia na 1ª IRQ).
Sem regressão: varredura vb200 com 152 jogos pixel-exatos (mesmos 3
resíduos: kizuna 480px, whp 413px, kof95 191px); `ctest` 5/5; doubledr com
WRAM e RAM do Z80 byte-exatas no vb600; s1945p vb300 0px.

**Em aberto (próxima divergência):** uma IRQ2 (timer de raster) que no MAME é
atendida 2 instruções depois da nossa. O MAME calcula o prazo do timer no
**tempo local da CPU no instante do acesso de escrita** (`scheduler::time()`
usa `local_time()` do dispositivo executando; o `icount` só é descontado
depois do acesso), e com `AUTOLOAD_REPEAT` o valor do contador que vale é o
que estava escrito naquele instante. Nós usamos o início da instrução. Fazer
isso direito exige o núcleo registrar o deslocamento de cada acesso dentro
da instrução — mudança grande, vale uma sessão própria.

## Achado #102: memória com bateria salva em disco

A backup RAM da placa MVS (64KB em `$D00000`: recordes, configuração da
BIOS, contadores/data de uso) e a SRAM de cartucho (V-Liner, Jockey GP)
existiam só durante a execução. Agora o frontend grava e carrega:

- arquivo `nvram/<jogo>.sav` (nome do romset sem diretório/extensão);
  formato `"NGNV"` + versão + backup RAM (ordem do espaço do 68000) + SRAM;
- carregado **antes** do reset (a BIOS lê a backup RAM no boot); gravado a
  cada ~10 s se o conteúdo mudou (FNV-1a) e ao sair; gravação atômica
  (temporário + rename);
- `NEOGEO_NO_NVRAM=1` desliga (boot limpo, pras comparações com o MAME);
  as ferramentas de depuração não carregam save.
- API no core: `neogeo_nvram_load/save/checksum`.

Não existe memory card: o MAME (e nós) reporta cartão ausente.

**Validado contra o `nvram/doubledr/saveram` do MAME:**
1. depois de 900 quadros, a backup RAM gravada bate com a do MAME exceto 4
   bytes (`$20-$22`, `$5C-$5D`) que são **data/hora** — duas execuções nossas
   em horários diferentes já divergem ali (os dois emuladores usam o relógio
   do PC no µPD4990A);
2. boot seguinte com o save do MAME carregado nos dois: vblank 250 com
   **0 pixels de diferença**; sem save a tela é outra (71680 px) — o save de
   fato muda o caminho de boot, e muda igual.

## Achado #103: controles completos — P2, START2/MOEDA2 e gamepad

Antes só existia P1 (`$300000`); START, MOEDA e SERVICE passavam pelos
campos de depuração `debug_force_*`, e `$340000` (P2) lia `FF`.

Mapa do MAME (`neogeo.cpp`, portas conferidas pela API Lua do próprio MAME:
`:edge:joy:JOY1/JOY2`, `:edge:joy:START` máscaras 1/4, `:AUDIO_COIN` 1/2/4),
tudo ativo-baixo:
- `$300000` P1 e `$340000` P2 (espelho `0x01fffe`): cima, baixo, esq, dir, A-D;
- `$380000` bit0 START1, bit1 SELECT1, bit2 START2, bit3 SELECT2;
- `$320001` bit0 MOEDA1, bit1 MOEDA2, bit2 SERVICE.

Core: `p2_buttons`, `sys_buttons`, `coin_buttons` em `neogeo_t` (0xFF no
reset). Frontend:

| | P1 | P2 |
|---|---|---|
| direções | setas / WASD | I J K L |
| A B C D | Z X C V | B N M , |
| START | 1 / ENTER | 2 |
| MOEDA | 5 / BACKSPACE | 6 |
| gamepad (SDL_GameController) | 1º controle | 2º controle |

Gamepad: direcional ou analógico esquerdo, A B X Y = A B C D, START, BACK =
moeda; conectar/desconectar a quente. F1 = SERVICE.

**Validado contra o MAME:** script Lua no MAME (`field:set_value`) e
`NEOGEO_INPUTS` no `debug_screenshot` com a mesma sequência no doubledr —
moeda 2, START 2, P2 segurando direita e apertando A. Tela de seleção com o
cursor do 2P no vblank 1000: **0 pixels de diferença**. (A entrada do Lua
vale a partir do quadro seguinte ao `set_value` — desloque 1 quadro.)
`ctest` 5/5.

## Achado #104: varredura de saúde do acervo inteiro + 2º bug de truncamento de 24 bits (LEA/PEA/JSR/JMP)

Pergunta do usuário: "temos ainda games que não rodam?" — em vez de supor,
escrevi `core/tools/debug_health.c`: roda cada jogo 3000 vblanks (~50s de
attract), conta exceções de CPU reais (vetores 2-11, não TRAP de software),
resets de watchdog, e amostra a tela a cada segundo na segunda metade pra
achar tela preta persistente. Rodei nos 170 jogos do acervo.

**Metodologia**: toda suspeita foi conferida contra o MAME antes de virar
achado — vários "sintomas" eram comportamento real do hardware:
- 6 jogos (`b2b`, `bakatono`, `maglord`, `roboarmy`, `sengoku`, `joyjoy`)
  ficavam parados na tela "NEO·GEO / MAX 330 MEGA" pelos 3000 vblanks
  inteiros — **o MAME faz exatamente a mesma coisa** (cartucho sem loop de
  attract além do logo). Não é bug.
- muitos jogos com `exc` na casa dos milhares mas `vet=00000000` — são
  chamadas de `TRAP #n` (vetores 32-47, fora da máscara de 32 bits que o
  `debug_health` usa pra detectar exceção de HARDWARE); uso normal de TRAP
  como chamada de sistema, não crash.

**4 jogos com tela preta persistente confirmados contra o MAME** (que roda
normal nos 4): `fatfury3`, `samsho3`, `viewpoin`, `mutnat`.

**`mutnat` resolvido** — mesma família do Achado #100 (truncamento de 32 pra
24 bits), mas escondida em outro lugar do núcleo: `ea_control_address()` e o
`indexed_address()` que ela compartilha com `ea_read`/`ea_write` mascaravam o
endereço calculado em 24 bits **antes de devolver**. Isso é usado por:
- **LEA** — guarda o endereço num registrador (`c->a[an] = addr`), que no
  68000 real NUNCA é truncado (só o barramento de 24 bits é limitado; o
  registrador e a ULA são de 32 bits inteiros);
- **PEA** — empurra o endereço na pilha, mesma regra;
- **JMP/JSR** — já mascaravam de novo no ponto de uso (`c->pc = mask24(addr)`),
  então não tinham esse bug, mas herdavam o mascaramento interno redundante.

`mutnat` fazia `LEA` de um endereço absoluto de 32 bits (provavelmente um
terminador de tabela `$FFFFFFFF`) que virava `$00FFFFFF`; o código
desreferenciava esse ponteiro errado e caía em erro de endereço (vetor 3) —
sintoma idêntico ao `A0=$00FFFFFF` do Achado #100. `ea_read`/`ea_write`
continuam corretos porque `mem_read`/`mem_write` já mascaram de novo na hora
do acesso de verdade — remover o mascaramento redundante daqui não muda
leitura/escrita de memória, só corrige LEA/PEA.

**Resultado**: `mutnat` bate pixel a pixel com o MAME no vblank 3000. Sem
regressão: varredura vb200 ainda 152/155 exatos (mesmos 3 resíduos de sempre
— kizuna, whp, kof95); `ctest` 5/5.

**Em aberto**: `fatfury3`, `samsho3`, `viewpoin` continuam com tela preta.
`fatfury3` já foi parcialmente rastreado: os primeiros **4.697.190
instruções batem ciclo a ciclo com o MAME** (via `tools/trace_diverge.py`,
ancorado em `$C11A84`) antes de divergir numa IRQ de timer atendida numa
instrução diferente (mesma família do timing "em aberto" do Achado #101) —
mas o travamento de verdade (erro de endereço com `A7` corrompido) acontece
~2,3M instruções DEPOIS dessa divergência, e ainda não achei a ligação entre
as duas coisas. `samsho3` mostra "WARNING BANK ERROR" da BIOS (suspeita:
proteção/bankswitch não coberta — a P ROM vem em 4 EPROMs `087-epr.ep1-4`
mais `087-p5`, layout incomum que meu carregador pode não estar montando
certo). `viewpoin` ainda não investigado a fundo.

## Achado #105: fatfury3 — o reset do watchdog não limpava os vetores do cartucho

Sintoma: tela preta, erros de endereço e a pilha (`A7`) em `$0000F2xx`.
Com `NEOGEO_WATCH_A7=1` (novo, no `debug_jump`) a pilha sai da WRAM
exatamente no **reset do watchdog** (vb 288,9): o 68000 relê SSP/PC nos
vetores e pegou `$0000F300` — o header da P ROM do cartucho.

**Causa:** no MAME o watchdog faz `schedule_soft_reset`, e o
`addressable_latch_device::device_reset` do HC259 zera as 8 saídas do latch
de sistema. Nosso `reset_common` zerava algumas (fix, trava da backup RAM,
banco de paleta) mas **não** `use_cart_vectors` nem `screen_shadow`. No
power-on o `memset` do `neogeo_init` escondia o bug; só aparecia em jogo que
leva reset de watchdog com os vetores do cartucho ligados.

Mais dois acertos no mesmo reset, medidos contra o MAME:
- **34 ciclos de microcódigo de reset** (`state_reset_if`: 4 passos de 2 +
  leituras de SSP/PC + pré-busca) antes da 1ª instrução — igual ao `tc=34`
  do MAME em `$C00402`. Sem isso a fase CPU×vblank ficava 6 ciclos
  deslocada e a IRQ de vblank caía numa instrução diferente no laço de
  espera da BIOS. Modelado como `m68k_reset_stall` (Z80/vídeo/timers andam
  normalmente nesses ciclos).
- `neogeo_run_until_vblank` devolvia o controle **no meio do quadro** no
  reset (o `run_slice` faz `break`); as ferramentas passavam a contar 1
  quadro a menos. Agora continua até o alvo absoluto.

**Armadilha de medição (importante):** depois de um soft reset o `gvblank`
do debugger do MAME fica 1 quadro à frente, e o `totalcycles` recomeça do
zero. Comparações por contagem de `gvblank` pareciam mostrar "1 quadro de
atraso", mas um trace único e contínuo alinhado por tempo (vb620→vb720)
deu **1.592.698 instruções e 100 IRQs idênticas ciclo a ciclo**. Para
comparar telas use o notificador de quadro do Lua: **quadro N do Lua = nosso
vblank N+1** (o notificador dispara no tempo N+1).

**Resultado:** `fatfury3` com **0 px** contra o MAME em 1150, 1500, 2000,
2500 e 3000. Outros jogos com reset de watchdog no quadro 1000 (Lua) /
vb1001: kof99, garou, samsho4, lastblad, blazstar, pulstar **0 px**; aof3
384 px só no brilho do logo SNK (8 linhas — cara do timer de raster em
aberto do #101). vb200: 152/155 exatos (mesmos 3 resíduos); `ctest` 5/5.

Ferramentas novas: `NEOGEO_WATCH_A7=1` (`debug_jump`), `tools/frame_offset.sh`
(trace curto MAME×nosso num vblank, com deslocamento de ciclo).

## Achado #106: samsho3 — P ROM em 5 chips, só 1MB era carregado

Sintoma: a BIOS parava em "WARNING / BANK ERROR". O dump da nossa P ROM
tinha **1MB**; o MAME (`ROM_START(samsho3)`) monta **3MB**:
`087-epr.ep1..ep4` (512KB cada, em $000000-$1FFFFF) + `087-p5.p5` (1MB em
$200000). O carregador só aceitava os tokens `p1 p2 sp2 ep1 ep2`, então
`ep3`, `ep4` e `p5` ficavam de fora e a janela de banco ($200000) vinha vazia.

Correção: tokens `ep3`, `ep4`, `p5` na lista (vários arquivos = concatena em
ordem, sem troca de metades). Varri os 170 zips: só `samsho3` tem esses
nomes no ramo comum (o `garou` tem `ep3/ep4` mas usa o ramo SMA; conferido
que as P ROMs de `garou` e `kof2003` ficaram byte a byte iguais).

Resultado: P ROM **idêntica ao layout do MAME** (3MB, byte a byte). Contra o
MAME (Lua quadro N = nosso vb N+1): **0 px** em 300, 1000 e 2000; no 3000,
332 px numa faixa de 8 linhas do chão rolando (x 142-311, y 210-217) — cara
do timer de raster em aberto (#101). `ctest` 5/5.

## Achado #107: viewpoin — com a CPU parada, o tempo emulado parava junto

Sintoma: tela preta. Depois do #105 (`run_until_vblank` passou a insistir
até o alvo), o `debug_health` **travava** no viewpoin — a pista: o
`neogeo_run_slice` tinha `if (done == 0) break;`, e `m68k_core_step` devolve
0 quando o 68000 está parado (instrução `STOP` esperando IRQ, ou HALT depois
de falha dupla). Ou seja, com a CPU parada **o relógio da placa parava**:
nada de vblank, IRQ, timer, Z80 ou watchdog — o jogo nunca saía do STOP.

No hardware (e no MAME) o clock continua: o núcleo parado só queima ciclos
(`icount`), as IRQs chegam e acordam o STOP, e o watchdog reseta uma placa em
HALT. Correção: CPU parada consome 2 ciclos por passo no `run_slice` (Z80,
vídeo, timers e watchdog andam normalmente).

Resultado contra o MAME (Lua quadro N = nosso vb N+1): **0 px** em 200, 600,
2000 e 3000. No quadro 1000 o logo da Sammy usa um efeito de raster (distorção
linha a linha): com render de quadro inteiro (`debug_screenshot` padrão) dá
15016 px, com render linha a linha (`NEOGEO_SCANLINE=1`, o caminho do
frontend) **0 px**. vb200: 152/155 (mesmos resíduos); `ctest` 5/5.

**Lição de ferramenta:** jogo com efeito de raster tem de ser comparado com
`NEOGEO_SCANLINE=1`; o render de quadro inteiro não mostra trocas no meio da
tela.

## Achado #108: sprite com zoom vertical — tile além de `rows` não é descartado

Os resíduos que eu atribuía ao "timer de raster" (#101) no `aof3` (brilho do
logo SNK) e no `samsho3` (faixa do chão) **não eram tempo**: em render linha
a linha continuavam iguais. Ampliando, a metade de baixo do "NK" (8 linhas)
não era desenhada. No nosso `lspc_sprites.c` havia
`if (tile_row >= height_tiles) continue;` — o `draw_sprites` do MAME não tem
isso: o único teste é `sprite_on_scanline` (linha < `rows*16`), e a entrada da
SCB1 que a tabela de zoom indicar é lida mesmo além de `rows`. Com zoom
vertical a tabela devolve tiles acima de `rows` nas últimas linhas.

Resultado: `aof3` quadro 1000 e `samsho3` quadro 3000 — **0 px**.

## Achado #109: C ROM com buraco na região — 155/155 no vb200

Os três resíduos que sobravam desde o Achado #76 (`kizuna` 480px, `whp`
413px, `kof95` 191px) eram o mesmo bug, visível no `kof95` como o logo "SNK
PRESENTS" que nunca aparecia. Ferramentas novas pra achar:
- **WRAM e VRAM do MAME por Lua** no instante exato do quadro
  (`space:read_range` e `emu.item(dev.items["0/m_videoram"])`); com isso:
  WRAM idêntica, **VRAM idêntica** → problema no nosso render, não no jogo;
- `debug_wram_at ... v` (VRAM do LSPC em words).

Os sprites do logo usam tiles `$30F87+`, que ficam em `$187C380` na região de
gráficos do MAME — dentro de `c7/c8`. O `ROM_START(kof95)` carrega `c5/c6`
(2MB) em `$1000000` e `c7/c8` em `$1800000`, deixando **um buraco** em
$1400000-$17FFFFF. Nosso carregador concatenava os arquivos, `c7/c8` caía em
$1400000 e o tile ficava além do fim da região (transparente pelo #78).
Varrendo o `neogeo.xml` contra o acervo: só `kof95`, `whp` e `kizuna` têm
buraco. Entradas geradas do XML na tabela de layout de C ROM (a mesma do
#97); o `kizuna` é identificado por `216-c3` porque `059-c1` é do `savagere`.

Resultado: **vb200: 155 de 155 jogos pixel-exatos com o MAME** (primeira vez;
o `irrmaze` não entra porque o snapshot de referência dele tem outra
resolução). `kof95` quadro 900 com o logo: 0 px. `ctest` 5/5.

## Achado #110: save states

**Frontend:** F5 salva, F7 carrega (`states/<jogo>.sta`, um slot por jogo);
mensagem no título da janela por 2 s. Ao carregar, a fila de áudio do SDL é
esvaziada.

**Core:** `neogeo_state_save/load` gravam o `neogeo_t` inteiro (~270 KB:
68000 e Z80 com o crédito de ciclos, YM2610 e buffer de áudio, LSPC/VRAM,
paleta, WRAM, backup RAM, µPD4990A, chips de proteção SMA/PVC/KOF98, bancos,
fase de vídeo, watchdog...). Não há estado em variáveis `static` do core
(conferido: só contadores de depuração). Ponteiros (ROMs, callbacks e
atalhos de memória das CPUs) vêm da instância viva. Cabeçalho `NGST` +
versão + `sizeof(neogeo_t)` + assinatura da P ROM: estado de outro jogo ou de
outra versão do emulador é **recusado sem mexer no estado atual**.

**Validação** (`core/tools/test_savestate`): instância A roda N quadros,
salva e roda mais M; instância B (outro endereço de memória, vinda de outro
ponto do jogo) carrega e roda os mesmos M pelo caminho do frontend
(`run_frame_render`). Hash de **cada quadro de imagem, do fluxo de áudio**,
WRAM, VRAM, paleta, RAM do Z80, ciclos e PCs: **idêntico** em doubledr,
fatfury3, mslug, kof99, garou, mslug5, kof2003, kof98, samsho3, vliner,
jockeygp, svc — e no fatfury3 salvando 4 quadros antes do reset do watchdog.
Estado do doubledr no kof99 e do samsho no samsho2: recusado, estado intacto.
`ctest` 5/5.

Limitação conhecida: o formato é a memória da estrutura, então muda quando
o `neogeo_t` muda — saves de uma versão anterior do emulador são recusados
(não corrompem).

## Fase 8 — Port pro SBC Linux ARM (Allwinner/Rockchip)
- [ ] Toolchain de cross-compilação (CMake toolchain file)
- [ ] Validar SDL2 disponível na placa (ou fallback framebuffer/DRM)
- [ ] Ajustes de performance se cycle-accurate não bater 60fps real (perfil,
      otimizações pontuais, nunca sacrificando corretude sem avisar)

---
Fases 0-2 fechadas (esqueleto + CPUs portadas e testadas). Próxima sessão:
começar Fase 3 (mapa de memória do sistema Neo-Geo) — provavelmente o melhor
ponto de partida é olhar o formato de ROM que você tiver disponível (MAME
splits, ou o `roms/` que já está sendo baixado) pra definir o layout de
arquivo aceito pelo carregador.

## Achado #111: pgoal 20x mais lento — `getenv` a cada leitura do banco P2

A varredura de saúde nunca listava o pgoal: o `timeout` matava o processo e o
`|| echo TIMEOUT` não pegava (o status do pipe era o do `tail`). Medido: 1000
quadros em ~20 s, 3000 em >150 s, sem travar (PC parado em $20172C, código
do jogo no banco P2). Causa: em `read16`, a linha de depuração do SMA era
`if (getenv("NEOGEO_TRACE_SMA") && addr >= 0x2FE000u ...)` — o `getenv` vinha
antes do teste de endereço, então toda busca de instrução em $200000–$2FFFFF
consultava o ambiente. Correção: `trace_sma()` lê a variável uma vez e o
teste de endereço vem primeiro. pgoal: 3000 quadros em 8 s, attract normal.
Afetava qualquer jogo que roda código do banco P2 (wh2 também), inclusive no
frontend em tempo real.
