/* "Cola" do sistema Neo-Geo (Fase 3): liga os dois cores de CPU portados
 * (M68kCore, Z80) à RAM/ROM via os mapas de memória abaixo, e faz as duas
 * CPUs avançarem juntas num scheduler simples de fatias de tempo.
 *
 * IMPORTANTE — honestidade sobre confiança nos endereços:
 *   - BIOS @ 0x000000, Work RAM @ 0x100000, P ROM @ 0x200000, Palette RAM
 *     @ 0x400000: confiança alta (documentados de forma consistente em
 *     várias fontes independentes sobre o hardware Neo-Geo).
 *   - REG_SOUND @ 0x320000 e os registradores do LSPC2 (VRAMADDR/VRAMRW/
 *     VRAMMOD/LSPCMODE/TIMERHIGH/TIMERLOW/IRQACK/TIMERSTOP @ 0x3C0000-
 *     0x3C000E): confirmados contra a NeoGeo Development Wiki
 *     (wiki.neogeodev.org, consulta em 2026-09-10 — páginas "Memory mapped
 *     registers", "68k interrupts", "68k/Z80 communication"). Substituem os
 *     palpites da Fase 3 original (o latch de som NÃO é em 0x3E0000 como eu
 *     tinha assumido antes).
 *   - Formato da paleta (dark bit + RGB 5-5-5) e layout do fix map/tile:
 *     reconstruídos a partir de resumos da mesma wiki — confiança média,
 *     ainda não validados visualmente contra uma imagem de referência real.
 *   - Bankswitch de P ROM > 1MB e bankswitch de Z80 (M1 > 64KB), além dos
 *     números exatos de porta de I/O do YM2610: NÃO implementados de forma
 *     confiável ainda — ver TODOs abaixo. Não adivinhei valores específicos
 *     porque errar aqui silenciosamente é pior do que deixar explícito que
 *     falta implementar.
 */

#include "neocore/neogeo.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Achado #23 (ver docs/ROADMAP.md): o valor de 200000 aqui era uma
 * aproximação (documentada como "não calibrada" desde a Fase 4) e estava
 * ERRADA — ~1.4% rápida demais. Achamos isso comparando WRAM completa
 * (dump via `save` no debugger do MAME) frame a frame contra a nossa: a
 * partir de ~180 vblanks os dumps começam a divergir, e o contador de
 * vblank em `wram[$10FEE4]` (incrementado 1x por interrupção, sempre)
 * mostrava um valor A MAIS no nosso emulador — clássico sintoma de
 * "nosso frame termina cedo demais, então já dispara a próxima IRQ antes
 * do hardware real". Valor real (NTSC): 384x264 pixels/frame, 4 mclk
 * (24MHz)/pixel, 68000 roda em metade do mclk (12MHz) → 384*264*4/2 =
 * 202752 ciclos/frame — confirmado contra a NeoGeo Development Wiki
 * ("Display timing"/"Clock"). */
#define NEOGEO_CYCLES_PER_FRAME 202752u

/* Achado #73: WATCHDOG (ver docs/ROADMAP.md). Um LS175 que reseta a placa
 * inteira se o programa não escrever em $300001 por tempo suficiente. O valor
 * exato vem do comentário do próprio MAME em `src/mame/snk/neogeo.cpp`:
 *   "The watchdog timer will reset the system after ~0.13 seconds.
 *    By cgfm's research, exactly 3,244,030 cycles (based on 24MHz clock)."
 * e da configuração `WATCHDOG_TIMER(config, "watchdog").set_time(
 * attotime::from_ticks(3244030, NEOGEO_MASTER_CLOCK))`. Como `master_cycles`
 * aqui conta ciclos do 68000 (12 MHz = metade do master de 24 MHz), o prazo em
 * ciclos do 68000 é 3244030/2. */
#define NEOGEO_WATCHDOG_CYCLES 1622015u

/* ---- 68000: mapa de memória --------------------------------------------- */

static inline u32 mirror(u32 addr, u32 size) {
    /* size deve ser potência de 2. Usado para RAM/ROM espelhadas dentro de
     * uma janela maior de endereço do que o chip físico. */
    return addr & (size - 1u);
}

/* A BIOS aparece em DUAS regiões do espaço de endereço do 68000: em
 * 0x000000 (onde a CPU busca o vetor inicial de SSP/PC logo após o reset) e
 * em 0xC00000-0xC1FFFF (onde o código e os vetores de exceção REALMENTE
 * apontam — confirmado contra a NeoGeo Development Wiki, "Boot process":
 * PC inicial real é $00C00402). Sem mapear 0xC00000 também, a CPU cai em
 * "open bus" ao tentar executar o código real e trava num loop de exceção
 * — foi exatamente isso que aconteceu na primeira tentativa de boot real
 * (ver docs/ROADMAP.md). */
/* Achado #39 (ver docs/ROADMAP.md): CORREÇÃO IMPORTANTE — só os 128 bytes da
 * TABELA DE VETORES ($000000-$00007F) é que alternam entre BIOS e cartucho.
 * De $000080 até $0FFFFF quem responde é SEMPRE a P ROM do CARTUCHO, não a
 * BIOS. Confirmado no mapa de memória real do MAME (`set_slot_idx` em
 * `src/mame/snk/neogeo.cpp`):
 *     space.install_rom(0x000080, 0x0fffff, cart_rom + 0x80/2);
 * e medido na prática: o MAME tem `"NEO-GEO"` + o número NGH do jogo
 * (`0x0082` = Double Dragon) em `$000100`/`$000108`, que é exatamente onde a
 * BIOS lê o ID do cartucho (`$C11F76: move.w $108.w, ...`). Como
 * mapeávamos essa faixa inteira pra BIOS, líamos o header errado, o ID saía
 * 0, e a BIOS concluía "cartucho desconhecido" — reconstruindo do zero a
 * tabela de estatísticas em `$D00124` e caindo na sequência de primeira
 * inicialização em vez de bootar o jogo. */
static inline bool is_bios_addr(u32 addr) {
    return addr < 0x80u || (addr >= 0xC00000u && addr < 0xC20000u);
}
/* P ROM do cartucho visível no espelho baixo ($000080-$0FFFFF). */
static inline bool is_cart_low_addr(u32 addr) {
    return addr >= 0x80u && addr < 0x100000u;
}
static inline u32 bios_offset(u32 addr) {
    if (addr >= 0xC00000u) addr -= 0xC00000u;
    return mirror(addr, NEOGEO_BIOS_SIZE);
}

/* Memory card (0x800000-0x800FFF): 2KB/8-bit — só o byte baixo de cada word
 * é conectado ao cartão real (por isso o teste de memória da BIOS só
 * confere via cmp.b), então cada endereço de WORD mapeia direto pra um
 * byte do cartão (endereço>>1). Byte alto lido como open-bus (0xFF). */
static inline bool is_memcard_addr(u32 addr) { return addr >= 0x800000u && addr < 0x801000u; }
static inline u32 memcard_index(u32 addr) { return (addr >> 1) & (NEOGEO_MEMCARD_SIZE - 1u); }

/* Lê o registrador VRAMRW. NÃO auto-incrementa: a wiki (e o teste de RAM
 * real da BIOS, achado depurando o boot — ver docs/ROADMAP.md) confirma que
 * o incremento de VRAMMOD só acontece depois de uma ESCRITA, não de uma
 * leitura. A rotina de teste de VRAM da BIOS lê e reescreve o mesmo valor
 * na mesma iteração (ler pra comparar, escrever de volta) esperando que só
 * a escrita avance o endereço — com leitura TAMBÉM incrementando, o
 * endereço avançava rápido demais e a verificação comparava a posição
 * errada, travando o boot pra sempre. */
static u16 vram_rw_read(neogeo_t *ng) {
    return ng->vram_read_buffer; /* Achado #93: `get_videoram_data` devolve o buffer */
}

/* Achado #52 (ver docs/ROADMAP.md): o endereço de VRAM do LSPC não é um
 * contador linear de 16 bits. A parte ALTA da VRAM ($8000-$87FF: SCB2/3/4 e
 * as listas de sprite por linha) é uma janela separada, e o auto-incremento
 * preserva o bit 15 e dá a volta só nos 15 bits BAIXOS. Do MAME
 * (`neosprite_base_device`, `neogeo_spr.cpp`):
 *     void set_videoram_offset(u16 data) {
 *         m_vram_offset = (data & 0x8000 ? data & 0x87ff : data);
 *     }
 *     void set_videoram_data(u16 data) {
 *         m_videoram[m_vram_offset] = data;
 *         set_videoram_offset((m_vram_offset & 0x8000) |
 *                             ((m_vram_offset + m_vram_modulo) & 0x7fff));
 *     }
 * Fazíamos uma soma simples de 16 bits: uma escrita sequencial longa na área
 * de tiles (SCB1, $0000-$7FFF) que passasse de $7FFF entrava na área dos
 * blocos de controle ($8000+) — corrompendo posição/altura de sprites em vez
 * de dar a volta. */
static void vram_set_offset(neogeo_t *ng, u16 data) {
    ng->vram_addr = (data & 0x8000u) ? (u16)(data & 0x87FFu) : data;
    /* Achado #93: "the read happens right away" (`set_videoram_offset`). */
    ng->vram_read_buffer = (ng->vram_addr < NEOGEO_VRAM_WORDS) ? ng->vram[ng->vram_addr] : 0xFFFFu;
}

static void vram_rw_write(neogeo_t *ng, u16 val) {
    /* Depuração: NEOGEO_TRACE_VRAM=<lo>-<hi> (em hex) loga escritas na faixa
     * de palavras dada, com o PC de quem escreveu. */
    {
        static int lo = -2, hi = -2;
        if (lo == -2) {
            const char *e = getenv("NEOGEO_TRACE_VRAM");
            unsigned a = 0, b = 0;
            if (e && sscanf(e, "%x-%x", &a, &b) == 2) { lo = (int)a; hi = (int)b; }
            else { lo = -1; hi = -1; }
        }
        if (lo >= 0 && (int)ng->vram_addr >= lo && (int)ng->vram_addr <= hi) {
            printf("[vram] w %04x = %04x (mod=%d pc=%06x)\n",
                   ng->vram_addr, val, (int)ng->vram_mod, ng->m68k.pc);
            fflush(stdout);
        }
    }
    if (ng->vram_addr < NEOGEO_VRAM_WORDS) ng->vram[ng->vram_addr] = val;
    vram_set_offset(ng, (u16)((ng->vram_addr & 0x8000u) |
                              ((ng->vram_addr + (u16)ng->vram_mod) & 0x7FFFu)));
}

/* REG_STATUS_A ($320001, byte baixo da word em $320000): bit7=dado do RTC
 * (fixo em 1, não implementamos o protocolo de leitura de hora/data ainda),
 * bit6=pulso do RTC (ver campo rtc_pulse — a peça que faltava pro boot não
 * travar, achado depurando o boot real), bit5=0 (4 slots), bits4-0=
 * coin-in/service ativos-baixo (1=nada). */
static inline u8 neogeo_status_a(const neogeo_t *ng) {
    /* Achado #36 (ver docs/ROADMAP.md): bit2 = botão SERVICE, ATIVO-BAIXO.
     * Voltou pra 1 (= SERVICE SOLTO, o estado normal de uma placa ligada
     * sem ninguém mexendo nela). Estávamos com bit2=0 desde o Achado #26,
     * ou seja **simulando o botão SERVICE eternamente pressionado** — e é
     * exatamente por isso que o boot ia parar na tela CABINET/SLOT (a tela
     * de configuração de operador, que SÓ deve aparecer com SERVICE
     * segurado). O beco sem saída do Achado #29/#35 era o fim natural
     * desse fluxo de configuração, não um bug: a gente é que estava
     * entrando nele sem querer. Com bit2=1, `$C11D70: bne c11d84` desvia
     * pro caminho de boot NORMAL (`$C17062`). */
    /* Achado #37 (ver docs/ROADMAP.md): valor MEDIDO no MAME real, não mais
     * chutado. Rodamos o MAME com o mesmo cart/BIOS e pedimos o dump do
     * registrador pelo debugger (`dump arq,0x320000,2`) no frame 280:
     * resultado `80 5F` — byte alto 0x80 (resposta do Z80), byte baixo
     * (STATUS_A) = **0x5F** = 0101 1111:
     *   bit0..bit4 = 1  (COIN1, COIN2, SERVICE etc, todos soltos)
     *   bit5       = 0  ("sense: 4-slot", conforme o fonte do MAME)
     *   bit6       = 1  (pulso do RTC — varia com o tempo)
     *   bit7       = 0  (DADO SERIAL do RTC — nós deixávamos preso em 1!)
     * O fonte do MAME confirma a origem dos dois últimos:
     *   PORT_BIT(0x0040, ... upd1990a_device::tp_r)        // bit6
     *   PORT_BIT(0x0080, ... upd1990a_device::data_out_r)  // bit7
     * Como ainda não implementamos o protocolo serial do µPD4990A, deixamos
     * bit7=0 (igual ao MAME no momento medido) em vez de 1. */
    u8 v = (u8)(0x1Fu & ng->coin_buttons);
    /* Achado #34 (ver docs/ROADMAP.md): simulação de moeda (COIN1, bit0,
     * ativo-baixo) — sem isso não tem crédito, e sem crédito START sozinho
     * não faz o jogo começar (comportamento real de arcade). Controlado
     * por `debug_force_coin_frames` (mesmo padrão do `debug_force_start_frames`
     * pro START, ver neogeo.h). */
    if (ng->debug_force_coin_frames > 0) v &= (u8)~0x01u;
    if (ng->debug_force_service_frames > 0) v &= (u8)~0x04u; /* bit2 = SERVICE */
    if (ng->rtc_pulse) v |= 0x40u;
    if (ng->rtc_data_out) v |= 0x80u; /* Achado #85: data_out_r() do µPD4990A */
    return v;
}

/* Registradores de 0x300000-0x3FFFFF que já tratamos "de verdade" (o resto
 * continua open-bus/ignorado, ver TODOs). Devolve true se tratou. */
/* Achado #93: interrupções do 68000 como no MAME (`update_interrupts`):
 * IRQ3 (bit0), raster/timer = nível 2 (bit1), vblank = nível 1 (bit2). O
 * 68000 atende o nível mais alto pendente. */
/* Achado #94: QUANDO o 68000 enxerga a IRQ. No núcleo do MAME
 * (`m68000-sif.cpp`, gerado do microcódigo) toda instrução faz
 *     if(m_next_state != S_TRACE) m_next_state = m_int_next_state;
 * no começo do ÚLTIMO passo, a pré-busca de 4 ciclos do próximo opcode. Ou
 * seja: IRQ que chega nos últimos 4 ciclos de uma instrução só é atendida
 * depois da instrução SEGUINTE. Medido no ironclad: na espera de vblank da
 * BIOS (`tst.b` 16 + `bne` 10) o MAME atendia uma instrução depois de nós.
 * Aqui, antes de cada instrução (= fim da anterior, em `master_cycles`), só
 * contam as fontes ligadas até `master_cycles - 4`. */
static void irq_apply_visible(neogeo_t *ng) {
    u8 p = ng->irq_pending;
    u64 lim = (ng->master_cycles >= 4u) ? ng->master_cycles - 4u : 0u;
    u8 level = 0;
    if ((p & NEOGEO_IRQACK_RESET) && ng->irq_time[0] <= lim) level = 3u;
    else if ((p & NEOGEO_IRQACK_TIMER) && ng->irq_time[1] <= lim) level = 2u;
    else if ((p & NEOGEO_IRQACK_VBLANK) && ng->irq_time[2] <= lim) level = 1u;
    m68k_core_set_irq(&ng->m68k, level);
}

static void irq_update(neogeo_t *ng) {
    irq_apply_visible(ng);
}

static void irq_raise(neogeo_t *ng, u8 bit, u64 when) {
    if (getenv("NEOGEO_TRACE_IRQ")) {
        printf("[irq] bit=%u when=%llu master=%llu pc=%06x\n", bit, (unsigned long long)when,
               (unsigned long long)ng->master_cycles, ng->m68k.pc);
        fflush(stdout);
    }
    ng->irq_pending |= bit;
    ng->irq_time[bit == NEOGEO_IRQACK_RESET ? 0 : bit == NEOGEO_IRQACK_TIMER ? 1 : 2] = when;
    irq_apply_visible(ng);
}

/* `adjust_display_position_interrupt_timer`: dispara depois de
 * (contador + 1) ticks do PIXEL_CLOCK (6 MHz) = 2 ciclos do 68000 cada. */
static void disp_timer_adjust(neogeo_t *ng, u64 from) {
    ng->disp_timer_time = from + ((u64)ng->display_counter + 1u) * 2u;
}

#define IRQ2CTRL_ENABLE          0x10u
#define IRQ2CTRL_LOAD_RELATIVE   0x20u
#define IRQ2CTRL_AUTOLOAD_VBLANK 0x40u
#define IRQ2CTRL_AUTOLOAD_REPEAT 0x80u

static bool reg_read16(neogeo_t *ng, u32 addr, u16 *out) {
    switch (addr) {
    case NEOGEO_REG_SOUND:
        /* Byte alto = resposta do Z80 (REG_SOUND); byte baixo = STATUS_A
         * (compartilham a mesma word, endereços 0x320000/0x320001). */
        *out = (u16)(((u16)ng->sound_latch_to_68k << 8) | neogeo_status_a(ng));
        return true;
    case NEOGEO_REG_VRAMADDR:
        /* Achado #93: no MAME os offsets 0 E 1 ($3C0000/$3C0002) leem o dado
         * da VRAM (`video_register_r`), não o endereço. */
        *out = vram_rw_read(ng);
        return true;
    case NEOGEO_REG_VRAMRW:
        *out = vram_rw_read(ng);
        return true;
    case NEOGEO_REG_VRAMMOD:
        *out = (u16)ng->vram_mod;
        return true;
    case NEOGEO_REG_LSPCMODE: {
        /* Achado #93: `get_video_control` do MAME — AAAA AAAA A??? BCCC,
         * A = contador vertical, que vai de 0xF8 a 0x1FF:
         *     v_counter = vpos + 0x100; if (v_counter >= 0x200) v_counter -= VTOTAL;
         *     ret = (v_counter << 7) | (auto_animation_counter & 7);
         * Medido no MAME: no início do vblank (onde o nosso contador de
         * frame zera) o feixe está na linha 240 (VBSTART) e o registrador lê
         * $F801. Antes a gente tratava o vblank como linha 0 e devolvia nos
         * bits baixos o que a CPU escreveu. */
        u32 vpos = (240u + ng->cycles_since_vblank / 768u) % 264u;
        u32 v_counter = vpos + 0x100u;
        if (v_counter >= 0x200u) v_counter -= 264u;
        *out = (u16)((v_counter << 7) | (ng->auto_anim_counter & 7u));
        return true;
    }
    default:
        return false;
    }
}

/* Achado #89: a NMI do Z80 no MAME é a saída de um `INPUT_MERGER_ALL_HIGH`
 * com duas entradas — "tem comando pendente no latch" e "NMI habilitada"
 * (portas $08/$18) — ligada em INPUT_LINE_NMI, que no Z80 dispara na BORDA
 * de subida. Então a NMI sai quando as DUAS condições ficam verdadeiras, na
 * ordem que for: se o driver religar a NMI com um comando já pendente, ela
 * dispara nessa hora. E ler a porta $00 confirma o comando
 * (`set_separate_acknowledge(false)`), derrubando a linha. Nós só
 * disparávamos no instante da escrita, e só se já estivesse habilitada. */
static void z80_nmi_update(neogeo_t *ng) {
    u8 line = (ng->sound_latch_pending && ng->z80_nmi_enabled) ? 1u : 0u;
    if (line && !ng->z80_nmi_line) Z80_NMI(&ng->z80);
    ng->z80_nmi_line = line;
}

static bool reg_write16(neogeo_t *ng, u32 addr, u16 val) {
    switch (addr) {
    case NEOGEO_REG_SOUND:
        ng->sound_latch_to_z80 = (u8)val;
        ng->sound_latch_to_z80_write_count++;
        ng->sound_latch_pending = true;
        if (getenv("NEOGEO_TRACE_SNDCMD")) {
            printf("[sndcmd] 68k -> Z80: $%02x (pc=%06x addr=%06x val16=%04x) ciclos68k=%llu\n",
                   (u8)val, ng->m68k.pc, addr, val, (unsigned long long)ng->master_cycles);
            fflush(stdout);
        }
        z80_nmi_update(ng); /* Achado #89 */
        return true;
    case NEOGEO_REG_VRAMADDR:
        vram_set_offset(ng, val); /* Achado #52: mascara $8xxx pra $8000-$87FF */
        return true;
    case NEOGEO_REG_VRAMRW:
        vram_rw_write(ng, val);
        return true;
    case NEOGEO_REG_VRAMMOD:
        ng->vram_mod = (s16)val;
        return true;
    case NEOGEO_REG_LSPCMODE:
        ng->lspcmode = val;
        ng->display_control = (u8)(val & 0xF0u); /* Achado #93 */
        /* Achado #45: LSPCMODE carrega a config da auto-animação de sprite —
         * bits 15-8 = velocidade (quantos frames por passo do contador),
         * bit 3 = desabilitada. Ver core/src/video/lspc_sprites.c. */
        ng->auto_anim_speed = (u8)(val >> 8);
        ng->auto_anim_disabled = (u8)((val >> 3) & 1u);
        return true;
    case NEOGEO_REG_TIMERHIGH:
        ng->timer_reload = (ng->timer_reload & 0x0000FFFFu) | ((u32)val << 16);
        ng->display_counter = ng->timer_reload; /* Achado #93 */
        return true;
    case NEOGEO_REG_TIMERLOW:
        ng->timer_reload = (ng->timer_reload & 0xFFFF0000u) | val;
        ng->display_counter = ng->timer_reload;
        /* Achado #93: `set_display_counter_lsb` — com LOAD_RELATIVE, recarrega
         * o timer a partir de AGORA. */
        if (ng->display_control & IRQ2CTRL_LOAD_RELATIVE) disp_timer_adjust(ng, ng->master_cycles);
        if (getenv("NEOGEO_TRACE_IRQ")) {
            printf("[timerlow] val=%04x reload=%08x from=%llu pc=%06x ctrl=%02x\n", val, ng->timer_reload,
                   (unsigned long long)ng->master_cycles, ng->m68k.last_opcode_pc, ng->display_control);
            fflush(stdout);
        }
        return true;
    case NEOGEO_REG_TIMERSTOP:
        ng->timerstop = val;
        return true;
    case NEOGEO_REG_IRQACK:
        /* bits 2/1/0 = vblank/timer/reset; escrever 1 limpa a pendência. */
        ng->irq_pending &= (u8)~(val & 7u);
        irq_update(ng); /* Achado #93 */
        return true;
    default:
        return false;
    }
}

/* REG_STATUS_B ($380000): bit7=1 (MVS, não AES), bit6=0 (memory card não
 * protegida), bits3-0=1111 (controles ativos-baixo, nada pressionado).
 * bits5-4: card presente/OK (00) — ver Achado #26, docs/ROADMAP.md.
 *
 * Histórico (Achado #14, sessão anterior): tínhamos mudado isso pra
 * bits5-4=01 ("sem cartão") comparando trace-a-trace contra o MAME num
 * ponto específico ($C1336A). NA ÉPOCA não sabíamos ainda que precisava
 * passar `-bios euro` explicitamente pro MAME pra comparação valer
 * (achado só documentado bem depois, mesma sessão) — ou seja, aquele
 * trace pode ter sido contra uma BIOS diferente da nossa, tornando a
 * comparação inválida. Achado #26 (bem mais recente e verificado via
 * desmontagem direta, não trace) mostra que outro ponto do boot
 * (`$C1B6FE`, chamado como parte da decisão de entrar ou não no
 * self-test — Achado #20) EXIGE bits5-4=00 (card presente) + um cartão
 * com assinatura "NEO-GEO" válida pra evitar cair no self-test sempre.
 * Como simular um cartão formatado válido (ver `neogeo_init`) é mais
 * realista que simular "sem cartão nunca", voltamos bits5-4 pra 00. Se
 * isso reabrir o comportamento visto no Achado #14, precisamos revisitar
 * os dois achados juntos. */
/* Achado #37 (ver docs/ROADMAP.md): valor MEDIDO no MAME real (dump do
 * debugger em `$380000` no frame 280, com os 3 valores de multiplexador que
 * a BIOS usa — 0x12, 0x1B e 0x00): **sempre `FF`**. Ou seja: START/SELECT
 * soltos (bits3-0=1111), memory card AUSENTE (bits6-4=111) e MVS (bit7=1).
 * Estávamos com `0x8F`, isto é, forçando "memory card presente" (bits5-4=00)
 * desde o Achado #26 — e é isso que empurrava o boot pro fluxo de
 * configuração. Com SERVICE solto (STATUS_A bit2=1, Achado #36), a checagem
 * de memory card do `$C1B6FE` nem chega a ser chamada, então não há mais
 * motivo pra fingir um cartão. */
#define NEOGEO_STATUS_B_DEFAULT 0xFFu

/* Achado #14 (ver docs/ROADMAP.md): comparando checkpoints de WRAM reais do
 * MAME (`-bios euro`, mesma BIOS `sp-s2.sp1`) contra os nossos, achamos o
 * ponto exato onde o boot trava pra sempre: uma máquina de estados em
 * $C17150 (index em wram[$FCD8/$FCD9], relativo a A5=$10F300) fica presa no
 * estado 1 esperando uma BORDA DE SUBIDA no bit 0 de REG_STATUS_B
 * ($380000) — código em $C1850E lê esse registrador duas vezes por frame,
 * inverte os bits e faz "(anterior XOR atual) AND atual" pra detectar
 * transições (mesma técnica de debounce de Start/Select P1/P2 nos bits
 * 1-4). O bit 0, porém, é documentado como "?" (não confirmado) na NeoGeo
 * Development Wiki — não corresponde a nenhum botão real. No MAME real,
 * esse bit MUDA algumas vezes a cada ~10-30 frames durante o boot (dados:
 * idx fica em 1 dos frames ~240 a ~270, depois avança pra 2, 3, 4... até
 * chegar em 7 no frame 500 — ver ROADMAP). Sem nenhuma variação nesse bit
 * (nosso REG_STATUS_B era 100% constante), a borda nunca acontece e o boot
 * trava PRA SEMPRE no estado 1 (confirmado rodando 20000 frames: tela fica
 * estática, um grid de auto-teste, nunca chega no logo/título — igual o
 * MAME real mostra por volta dos 300 frames/5s).
 *
 * O mais provável é que esse seja um pino de entrada flutuante/não
 * conectado no hardware real (daí o "?" na wiki), captando ruído elétrico;
 * o código do jogo usa a borda dele como uma espécie de "espera um tempo
 * pseudo-aleatório". Não temos como replicar o ruído elétrico exato, então
 * modelamos como ruído determinístico derivado do contador de ciclos mestre
 * — produz transições com frequência parecida (a cada ~10-20 frames, não
 * a cada poucos ciclos) sem precisar de estado extra no struct.
 *
 * CUIDADO (achado depurando o resultado inicial): o código em `$C1850E` lê
 * STATUS_B DUAS VEZES dentro do MESMO frame (com um pequeno delay entre as
 * leituras) e usa a DIFERENÇA entre essas duas leituras como parte do
 * cálculo — se o ruído mudasse rápido demais (a cada ~1024 ciclos, como na
 * primeira tentativa), as duas leituras do mesmo frame quase sempre viam
 * valores diferentes uma da outra, gerando uma borda espúria TODO frame em
 * vez de a cada 10-30 frames como no hardware real. Isso destravava o boot
 * mas com timing errado, e resultou em texto/sprites desenhados em
 * posições espalhadas/erradas na tela (visto em screenshot, ver ROADMAP).
 * Por isso o shift aqui é grande o bastante (>>21, ~2M ciclos = ~10 frames)
 * pra garantir que as DUAS leituras dentro do mesmo frame sempre vejam o
 * MESMO valor (só muda entre frames, nunca dentro de um frame). Isso NÃO é
 * bit-exato ao hardware real, é uma aproximação pragmática pra destravar o
 * boot; se descobrirmos a fonte real desse bit no futuro, substituir aqui. */
/* Achado #36 (ver docs/ROADMAP.md): o "bit 0 misterioso" do Achado #14 é
 * simplesmente o **botão START do P1** (ativo-baixo). Prova: a própria tela
 * "I/O CHECK" da BIOS, que renderizamos corretamente depois do Achado #28,
 * lista P1/P2 x UP/DOWN/LEFT/RIGHT/A/B/C/D/START/SELECT — e mostrava
 * exatamente `START 1` pro P1 (e 0 pra todo o resto) no mesmo momento em
 * que `p1_buttons=0xFF` (nada pressionado) e o RUÍDO do Achado #14 estava
 * com o bit0 em 0. Ou seja, a BIOS estava reportando "P1 START
 * pressionado" porque nosso ruído zerava esse bit. Layout completo
 * confirmado por essa mesma tela:
 *   bit0 = P1 START, bit1 = P1 SELECT, bit2 = P2 START, bit3 = P2 SELECT
 *   (todos ativo-baixo), bits5-4 = memory card, bit6 = proteção de escrita
 *   do card, bit7 = 1 MVS / 0 AES.
 * Consequência: o boot NUNCA esteve "travado esperando ruído elétrico" —
 * estava esperando o JOGADOR APERTAR START, que é o comportamento normal
 * de um arcade. O hack de ruído (Achado #14) fazia a BIOS ver START sendo
 * apertado sozinho, em momentos aleatórios, o que destravava o boot mas
 * com timing errado (e provavelmente é a origem de vários comportamentos
 * estranhos que perseguimos desde então). Removido. */
static inline u8 neogeo_status_b(const neogeo_t *ng) {
    u8 v = NEOGEO_STATUS_B_DEFAULT & (u8)(ng->sys_buttons | 0xF0u);
    if (ng->debug_force_start_frames > 0) v &= (u8)~0x01u; /* P1 START apertado */
    return v;
}

/* REG_DIPSW ($300001, byte baixo da word em $300000/P1CNT): dip switches do
 * sistema. Achado depurando o boot real (ver docs/ROADMAP.md): a BIOS lê
 * esse valor e testa o bit 2 pra decidir se pula um longo processo de
 * "debounce" de controles (bit2=0) ou espera ele terminar sozinho
 * (bit2=1) — sem esse registrador implementado (open-bus, todos os bits em
 * 1), a BIOS sempre cai no caminho "espera", que nunca é satisfeito na
 * nossa emulação. Setamos bit2=0 pra destravar; os outros bits mantidos em
 * 1 (config "default", sem significado confirmado ainda — não sabemos a
 * função exata de cada dip switch neste jogo). */
/* Achado #36 (ver docs/ROADMAP.md): a própria tela "SETTING UP THE HARD DIP"
 * da BIOS (que renderizamos corretamente depois do Achado #28) documenta o
 * layout dessas chaves pra gente:
 *     SETTING MODE   0.......   (DIP 1)
 *     COMMUNICATION  ...000..   (DIP 4,5,6)
 *     FREE PLAY      ......0.   (DIP 7)
 *     STOP MODE      .......0   (DIP 8)
 * E a tela "I/O CHECK" mostrava `DIP 00100000` quando tínhamos `0xFB`
 * (= 1111 1011, só bit2 zerado) — ou seja a BIOS exibe o INVERSO do bit
 * cru (exibido 1 = chave LIGADA = bit cru 0), confirmando ativo-baixo e
 * que DIP 1 (SETTING MODE) == bit0 cru.
 * Portanto bit0 PRECISA ser 1 (SETTING MODE desligado) pra bootar normal.
 * O experimento do Achado #32 (bit0=0) ligava o modo de configuração sem
 * querer — revertido. bit2 mantido em 0 (achado antigo: pula uma espera
 * longa de debounce). */
/* Achado #92: CORREÇÃO — o bit 2 zerado era um contorno de uma fase em que
 * relógio e timing estavam errados (a "espera de debounce" nunca terminava).
 * Pela tabela do MAME (`PORT_START("DSW")` em neogeo.cpp) o bit 2 é
 *     PORT_DIPNAME( 0x04, 0x04, DEF_STR( Controller ) )  Joystick / Mahjong Panel
 * e a BIOS, em `$C18484`, com ele zerado grava o tipo de controle 3 (painel
 * de mahjong) em `$10FD94` — o que muda como ela lê os controles e, com
 * isso, o attract de jogos como o ironclad. Padrão do MAME: 0xFF (tudo
 * desligado). */
#define NEOGEO_DIPSW_DEFAULT 0xFFu

/* REG_SYSTYPE / porta "TEST" ($300081): bit7 = chave TEST, bit6 = "sense"
 * do tipo de placa (0 = 2 slots, 1 = 4/6 slots).
 *
 * Achado #41 (ver docs/ROADMAP.md): o valor aqui estava `0x3F`, com o
 * comentário afirmando "bit7=0 (nao e test)" — LÓGICA INVERTIDA, o mesmo
 * erro que o SERVICE tinha no Achado #36. O fonte do MAME é explícito:
 *     PORT_START("TEST")
 *     PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_CUSTOM )   // sense: JAMMA/MVS
 *     PORT_SERVICE_NO_TOGGLE( 0x0080, IP_ACTIVE_LOW )  // <- chave TEST
 * ATIVO-BAIXO: bit7=1 significa chave SOLTA, bit7=0 significa PRESSIONADA.
 * Com `0x3F` estávamos simulando o botão TEST eternamente apertado, e a
 * BIOS faz exatamente o que deve nesse caso — entra no MENU DE OPERADOR:
 *     C12744: not.b  D0             ; inverte a porta TEST
 *     C12752: move.b D1,($bdf,A5)   ; D1 = bordas de subida
 *     C12756: tst.b  ($a81,A5)
 *     C1275A: beq    $c12768
 *     C1275C: btst   #$7, D1        ; borda no bit7 = TEST apertado agora
 *     C12760: beq    $c12768
 *     C12762: jmp    $c18f74.l      ; -> menu de operador (7 itens, o mesmo
 *                                   ;    que víamos na tela)
 * Corrigido pra bit7=1 (chave solta). bit6 fica em 0 (2 slots), que é o que
 * medimos no MAME real ($D00047 = 2). */
/* Achado #101: porta "TEST" do MAME (`neogeo.cpp`):
 *     PORT_BIT( 0x003f, IP_ACTIVE_HIGH, IPT_UNUSED )   -> bits 0-5 = 0
 *     PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_CUSTOM )   -> sense JAMMA = 0
 *     PORT_SERVICE_NO_TOGGLE( 0x0080, IP_ACTIVE_LOW )  -> solto = 1
 * = $80. Com $BF (bits 0-5 em 1) a BIOS guardava ~porta = $40 em $10FEDE
 * todo frame, contra $7F no MAME — a WRAM divergia desde o boot em todo jogo. */
#define NEOGEO_SYSTYPE_DEFAULT 0x80u

/* Bankswitch do P ROM (confirmado contra o driver neogeo.cpp do MAME): a
 * janela de 1MB em 0x200000-0x2FFFFF é remapeada pra um banco de 1MB
 * diferente do P ROM ao ACESSAR qualquer endereço em 0x2FFFF0-0x2FFFFF —
 * é um "latch" decodificado pelo endereço, não pelo valor escrito, e
 * dispara tanto em leitura quanto em escrita no hardware real. bank =
 * (offset dentro da janela de 16 bytes) / 2, dando bancos 0-7 (até 8MB de P
 * ROM). Sem isso, cartuchos com P ROM > 1MB (como o Double Dragon, 2MB)
 * ficam sempre no banco 0 — a BIOS faz um checksum que varre os bancos e
 * trava ao ver sempre os mesmos dados. Achado depurando o boot real (ver
 * docs/ROADMAP.md). */
/* Achado #48 (ver docs/ROADMAP.md): o bankswitch de P ROM estava errado em
 * TRÊS pontos, e era a causa do crash (address error) que impedia o jogo de
 * chegar na tela de título. Comparado com `neogeo_base_state::write_banksel`
 * (fonte real do MAME):
 *     int bank = data & 0x07;                        // vem do DADO escrito
 *     if ((bank + 1) * 0x100000 >= len) bank = 0;    // limita ao tamanho real
 *     m_bank_base = (bank + 1) * 0x100000;           // (bank+1)!
 * e o handler é instalado como `install_write_handler(0x2ffff0, 0x2fffff, ...)`
 * — ou seja, SÓ ESCRITA.
 *
 * O que tínhamos: banco derivado do ENDEREÇO (não do dado), sem o "+1", e
 * disparando também em LEITURA. O "+1" é o mais grave: o primeiro megabyte
 * já está fixo em $000000-$0FFFFF, então o banco 0 da janela precisa mostrar
 * o SEGUNDO megabyte. Sem isso a janela $200000-$2FFFFF devolvia uma cópia
 * do que já estava em $000000 — o jogo lia ponteiros/dados errados dali,
 * montava um endereço ÍMPAR ($2EFFFF) e o 68000 disparava address error,
 * caindo no vetor $C0040E e reiniciando a placa. */
/* ============ Achado #69: chip de proteção do Metal Slug X ============
 *
 * O cartucho do Metal Slug X (placa NEO-MVS PROGEOP, 1999) tem um ALTERA
 * EPM7128 amarrado à 250-P1. O jogo lê esse chip pra decidir se está numa
 * placa SNK original; sem ele, fica PARADO na tela de aviso anti-pirataria
 * ("THIS ROM CARTRIDGE IS FOR USE WITH ORIGINAL SNK BOARDS ONLY") e nunca
 * chega no attract.
 *
 * O chip vive na janela $2FFFE0-$2FFFEF (MAME, `neogeo.cpp`, case
 * NEOGEO_MSLUGX: `install_read_handler(0x2fffe0, 0x2fffef, protection_r)`).
 * Lógica copiada de `mslugx_prot_device` (`prot_mslugx.cpp`):
 *
 *   ESCRITA (offset = índice de WORD dentro da janela):
 *     0  -> começa uma leitura nova: command = 0
 *     1,2-> acumula o comando: command |= data
 *     3  -> "terminou", nada a fazer
 *     5  -> init: counter = 0, command = 0
 *
 *   LEITURA: devolve UM BIT, extraído do próprio programa do cartucho a
 *   partir de $0DEDD2 — o chip é, na prática, um "leitor de bit serial"
 *   sobre uma região da P ROM:
 *     command 0x0001: bit em (counter), com counter++ a cada leitura
 *     command 0x0fff: bit em (word de $10F00A) - 1   (endereço de WRAM)
 *   Em ambos: `res = (byte[0xdedd2 + (sel >> 3)] >> (~sel & 7)) & 1`.
 *
 * O `select` do comando 0x0fff sai de uma variável do próprio jogo em
 * $10F00A — o comentário no MAME ("How should this be calculated?") diz que
 * nem eles sabem de onde o valor vem no hardware; copiamos como está. */
#define MSLUGX_NGH 0x0250u

/* Achado #70: o V-Liner (NGH 03E7) tem 8KB de SRAM COM BATERIA no próprio
 * cartucho, mapeada em $200000-$201FFF — no lugar onde os outros cartuchos
 * têm o banco de P ROM. No MAME (`neogeo.cpp`, case NEOGEO_VLINER):
 *     space.install_read_handler (0x200000, 0x201fff, ram_r);
 *     space.install_write_handler(0x200000, 0x201fff, ram_w);
 * Sem ela, a checagem da SRAM na hora do boot falha e o jogo para na tela
 * "SRAM ERROR". (O V-Liner também remapeia as entradas — DSW em $300000,
 * IN5 em $280000, IN6 em $2C0000 — isso ainda NÃO está implementado, então
 * os controles desse cartucho continuam sem funcionar direito.) */
#define VLINER_NGH 0x03E7u

/* getenv() é caro; chamado a cada leitura de $2xxxxx deixava o pgoal (que
 * roda código do banco P2) ~20x mais lento. Lê a variável uma vez só. */
static bool trace_sma(void) {
    static int v = -1;
    if (v < 0) v = getenv("NEOGEO_TRACE_SMA") ? 1 : 0;
    return v != 0;
}

static inline bool is_cart_sram_addr(const neogeo_t *ng, u32 addr) {
    return ng->cart_sram_enabled && addr >= 0x200000u && addr < 0x202000u;
}

static u16 mslugx_prot_read(neogeo_t *ng) {
    s32 sel;
    if (getenv("NEOGEO_TRACE_PROT")) {
        static int n = 0;
        if (n < 40) { n++; printf("[prot] read cmd=%04x counter=%u pc=%06x\n",
                                  ng->prot_command, ng->prot_counter, ng->m68k.pc); fflush(stdout); }
    }
    if (ng->prot_command == 0x0001u) {
        sel = (s32)ng->prot_counter;
        ng->prot_counter++;
    } else if (ng->prot_command == 0x0FFFu) {
        u32 o = mirror(0x10F00Au, NEOGEO_WRAM_SIZE);
        sel = (s32)(((u32)ng->wram[o] << 8) | ng->wram[o + 1u]) - 1;
    } else {
        return 0;
    }
    if (ng->rom.p_size == 0) return 0;
    u32 byte_addr = 0xDEDD2u + (u32)((sel >> 3) & 0x0FFF);
    u8 b = ng->rom.p[byte_addr % ng->rom.p_size];
    return (u16)((b >> (~sel & 7)) & 1u);
}

static void mslugx_prot_write(neogeo_t *ng, u32 addr, u16 data) {
    u32 offset = (addr - 0x2FFFE0u) / 2u;
    if (getenv("NEOGEO_TRACE_PROT")) {
        static int n = 0;
        if (n < 40) { n++; printf("[prot] write off=%u data=%04x pc=%06x\n",
                                  offset, data, ng->m68k.pc); fflush(stdout); }
    }
    switch (offset) {
        case 0u: ng->prot_command = 0; break;             /* nova leitura */
        case 1u: case 2u: ng->prot_command |= data; break; /* comando */
        case 3u: break;                                    /* terminou */
        case 5u: ng->prot_counter = 0; ng->prot_command = 0; break; /* init */
        default: break;
    }
}

/* Achado #82: proteção do KOF98 (`kof98_prot_device`). Um chip Altera no
 * cartucho sobrepõe as 2 words de $000100/$000102 (onde fica o começo do
 * cabeçalho "NEO-GEO") conforme o que o jogo escreve em $20AAAA:
 *     0x0090 -> estado 1: devolve 0x00C2, 0x00FD
 *     0x00F0 -> estado 2: devolve 0x4E45, 0x4F2D ("NEO-")
 *     outro  -> nada muda (0x00AA é escrito, mas "not needed?")
 * No estado 0 devolve as words originais da ROM já decifrada. */
static u16 kof98_prot_read(const neogeo_t *ng, u32 addr) {
    int second = (addr >= 0x102u) ? 1 : 0;
    if (ng->kof98_state == 1) return second ? 0x00FDu : 0x00C2u;
    if (ng->kof98_state == 2) return second ? 0x4F2Du : 0x4E45u;
    return ng->kof98_default[second];
}
static inline bool is_kof98_prot_write(const neogeo_t *ng, u32 addr) {
    return ng->prot_kof98 && (addr & 0xFFFFFEu) == 0x20AAAAu;
}
static void kof98_prot_write(neogeo_t *ng, u16 data) {
    if (data == 0x0090u) ng->kof98_state = 1;
    else if (data == 0x00F0u) ng->kof98_state = 2;
}

/* Achado #83: chip NEO-PVC (mslug5, svc, kof2003) — `pvc_prot_device` +
 * `neogeo_base_state::write_bankprot_pvc`. 0x1000 words de RAM ocupam
 * $2FE000-$2FFFFF, POR CIMA da janela de banco (leitura E escrita). Três
 * registradores têm efeito colateral:
 *   $FF0      -> "desempacota" uma cor de 16 bits em $FF1/$FF2
 *   $FF4/$FF5 -> "empacota" de volta em $FF6
 *   >= $FF8   -> bankswitch: banco = ((ram[FF8]>>8) | (ram[FF9]<<8)) + 1MB,
 *                e o chip reescreve FF8/FF9 na hora.
 * É o mecanismo de proteção: o jogo confere que as contas de cor batem. */
static inline bool is_pvc_addr(const neogeo_t *ng, u32 addr) {
    return ng->prot_pvc && addr >= 0x2FE000u && addr < 0x300000u;
}
static void pvc_unpack_color(neogeo_t *ng) {
    u16 pen = ng->pvc_ram[0xff0];
    u8 b = (u8)(((pen & 0x000f) << 1) | ((pen & 0x1000) >> 12));
    u8 g = (u8)(((pen & 0x00f0) >> 3) | ((pen & 0x2000) >> 13));
    u8 r = (u8)(((pen & 0x0f00) >> 7) | ((pen & 0x4000) >> 14));
    u8 s = (u8)((pen & 0x8000) >> 15);
    ng->pvc_ram[0xff1] = (u16)((g << 8) | b);
    ng->pvc_ram[0xff2] = (u16)((s << 8) | r);
}
static void pvc_pack_color(neogeo_t *ng) {
    u16 gb = ng->pvc_ram[0xff4];
    u16 sr = ng->pvc_ram[0xff5];
    ng->pvc_ram[0xff6] = (u16)(((gb & 0x001e) >> 1) | ((gb & 0x1e00) >> 5) | ((sr & 0x001e) << 7) |
                               ((gb & 0x0001) << 12) | ((gb & 0x0100) << 5) |
                               ((sr & 0x0001) << 14) | ((sr & 0x0100) << 7));
}
/* `mask`: 0xFFFF word, 0xFF00 byte par, 0x00FF byte ímpar (COMBINE_DATA). */
static void pvc_write(neogeo_t *ng, u32 addr, u16 data, u16 mask) {
    u32 off = (addr - 0x2FE000u) >> 1;
    ng->pvc_ram[off] = (u16)((ng->pvc_ram[off] & ~mask) | (data & mask));
    if (off == 0xff0) pvc_unpack_color(ng);
    else if (off >= 0xff4 && off <= 0xff5) pvc_pack_color(ng);
    if (off >= 0xff8) {
        u32 bankaddress = ((u32)ng->pvc_ram[0xff8] >> 8) | ((u32)ng->pvc_ram[0xff9] << 8);
        ng->pvc_ram[0xff8] = (u16)((ng->pvc_ram[0xff8] & 0xfe00) | 0x00a0);
        ng->pvc_ram[0xff9] &= 0x7fff;
        ng->p_bank_offset = bankaddress + 0x100000u;
    }
}

static inline bool is_mslugx_prot_addr(const neogeo_t *ng, u32 addr) {
    return ng->prot_mslugx && addr >= 0x2FFFE0u && addr < 0x2FFFF0u;
}

static inline void p_bankswitch_write(neogeo_t *ng, u32 addr, u16 data) {
    /* Achado #79: no MAME o registrador do chip SMA é um handler de 2 bytes
     * instalado POR CIMA do bankswitch padrão ($2FFFF0-$2FFFFF), num endereço
     * que muda por jogo (kof99 $2FFFF0, garou $2FFFC0, mslug3 $2FFFE4, kof2000
     * $2FFFEC). O resto da faixa padrão continua valendo normalmente. */
    if (ng->sma_type != 0 && (addr & 0xFFFFFEu) == ng->sma_bank_addr) {
        ng->p_bank_offset = neogeo_sma_bank_base(ng->sma_type, data);
        return;
    }
    if (addr < 0x2FFFF0u || addr > 0x2FFFFFu) return;
    u32 bank = data & 0x07u;
    if ((bank + 1u) * 0x100000u >= ng->rom.p_size) bank = 0u;
    ng->p_bank_offset = (bank + 1u) * 0x100000u;
}

/* Achado #72/#79: janelas do chip SMA, dentro do espaço de bankswitch
 * ($200000-$2FFFFF). `protection_r` devolve uma constante fixa (0x9a37,
 * copiada do `sma_prot_device::prot_9a37_r` do MAME — o jogo só confere se
 * bate) no mesmo endereço pra todos; `random_r` avança um LFSR de 16 bits a
 * cada leitura, em dois endereços que mudam por jogo. */
static inline bool is_sma_protr_addr(const neogeo_t *ng, u32 addr) {
    return ng->sma_type != 0 && addr >= 0x2FE446u && addr < 0x2FE448u;
}
static inline bool is_sma_random_addr(const neogeo_t *ng, u32 addr) {
    if (ng->sma_type == 0) return false;
    u32 a = addr & 0xFFFFFEu;
    return (ng->sma_rand_addr1 != 0 && a == ng->sma_rand_addr1) ||
           (ng->sma_rand_addr2 != 0 && a == ng->sma_rand_addr2);
}

static u8 m68k_read8_cb(void *user, u32 addr) {
    neogeo_t *ng = (neogeo_t *)user;
    addr &= 0xFFFFFFu;

    if (is_bios_addr(addr)) {
        /* Achado #40: vetores ($000000-$00007F) podem vir do CARTUCHO. */
        if (addr < 0x80u && ng->use_cart_vectors && ng->rom.p_size > addr) {
            return ng->rom.p[addr];
        }
        return ng->rom.bios[bios_offset(addr)];
    }
    /* Achado #82: overlay do KOF98 em $000100-$000103. */
    if (ng->prot_kof98 && addr >= 0x100u && addr < 0x104u) {
        u16 v = kof98_prot_read(ng, addr);
        return (addr & 1u) ? (u8)v : (u8)(v >> 8);
    }
    /* Achado #39: espelho baixo da P ROM do cartucho ($000080-$0FFFFF). */
    if (is_cart_low_addr(addr)) {
        return (ng->rom.p_size > addr) ? ng->rom.p[addr] : 0xFFu;
    }
    if (addr == 0x380000u) return neogeo_status_b(ng);
    if (addr == 0x320001u) return neogeo_status_a(ng);
    if (addr == 0x300000u) return ng->p1_buttons; /* REG_P1CNT, ver Achado #19 */
    if ((addr & 0xFE0000u) == 0x340000u) return (addr & 1u) ? 0xFFu : ng->p2_buttons; /* P2, espelho 0x01fffe */
    if (addr == 0x300001u) return NEOGEO_DIPSW_DEFAULT;
    if (addr == 0x300081u) return NEOGEO_SYSTYPE_DEFAULT;
    if (addr >= 0xD00000u && addr < 0xD10000u) { /* Backup RAM */
        return ng->backup_ram[addr - 0xD00000u];
    }
    if (is_memcard_addr(addr)) {
        return (addr & 1u) ? ng->memcard[memcard_index(addr)] : 0xFFu;
    }
    if (addr < 0x200000u) { /* Work RAM, espelhada */
        return ng->wram[mirror(addr, NEOGEO_WRAM_SIZE)];
    }
    if (is_pvc_addr(ng, addr)) { u16 v = ng->pvc_ram[(addr - 0x2FE000u) >> 1]; return (addr & 1u) ? (u8)v : (u8)(v >> 8); } /* Achado #83 */
    if (is_mslugx_prot_addr(ng, addr)) { /* Achado #69 */
        u16 v = mslugx_prot_read(ng);
        return (addr & 1u) ? (u8)v : (u8)(v >> 8);
    }
    if (is_cart_sram_addr(ng, addr)) return ng->cart_sram[addr - 0x200000u]; /* Achado #70 */
    if (is_sma_protr_addr(ng, addr)) { u16 v = 0x9a37u; return (addr & 1u) ? (u8)v : (u8)(v >> 8); } /* Achado #72 */
    if (is_sma_random_addr(ng, addr)) { u16 v = neogeo_sma_random_step(&ng->sma_rng); return (addr & 1u) ? (u8)v : (u8)(v >> 8); }
    if (addr < 0x300000u) { /* P ROM do cartucho, banco selecionado por
                                p_bankswitch_write (só em ESCRITA — Achado
                                #48). TODO: cartuchos com chip de proteção
                                próprio (CMC42/50, SMA, PVC) usam um esquema
                                diferente, não implementado. */
        u32 off = ng->p_bank_offset + (addr - 0x200000u);
        if (ng->rom.p_size == 0) return 0xFFu;
        return ng->rom.p[off % ng->rom.p_size];
    }
    if (addr >= 0x400000u && addr < 0x402000u) { /* Palette RAM (word) */
        u32 word_idx = (addr - 0x400000u) / 2u;
        if (word_idx >= NEOGEO_PALETTE_WORDS) return 0xFFu;
        u16 w = ng->palette[ng->palette_bank + word_idx];
        return (addr & 1u) ? (u8)w : (u8)(w >> 8);
    }
    {
        u16 rv;
        if (reg_read16(ng, addr & 0xFFFFFEu, &rv)) {
            return (addr & 1u) ? (u8)rv : (u8)(rv >> 8);
        }
    }
    /* Resto de 0x300000-0x3FFFFF (dipswitches, watchdog, coin counters
     * etc.): TODO Fase 3b/4 — open bus por ora. */
    return 0xFFu;
}

static u16 m68k_read16_cb(void *user, u32 addr) {
    neogeo_t *ng = (neogeo_t *)user;
    addr &= 0xFFFFFEu; /* acesso word é sempre alinhado */

    if (is_bios_addr(addr)) {
        /* Achado #40: vetores ($000000-$00007F) podem vir do CARTUCHO. */
        if (addr < 0x80u && ng->use_cart_vectors && addr + 1u < ng->rom.p_size) {
            return (u16)((ng->rom.p[addr] << 8) | ng->rom.p[addr + 1u]);
        }
        u32 o = bios_offset(addr);
        return (u16)((ng->rom.bios[o] << 8) | ng->rom.bios[o + 1]);
    }
    if (ng->prot_kof98 && addr >= 0x100u && addr < 0x104u) return kof98_prot_read(ng, addr); /* Achado #82 */
    /* Achado #39: espelho baixo da P ROM do cartucho ($000080-$0FFFFF). */
    if (is_cart_low_addr(addr)) {
        if (addr + 1u < ng->rom.p_size) {
            return (u16)((ng->rom.p[addr] << 8) | ng->rom.p[addr + 1u]);
        }
        return 0xFFFFu;
    }
    if (addr == 0x380000u) return (u16)(((u16)neogeo_status_b(ng) << 8) | 0xFFu);
    if (addr == 0x300000u) return (u16)(((u16)ng->p1_buttons << 8) | NEOGEO_DIPSW_DEFAULT);
    if ((addr & 0xFE0000u) == 0x340000u) return (u16)(((u16)ng->p2_buttons << 8) | 0xFFu); /* in1_edge_r */
    if (addr == 0x300080u) return (u16)((0xFFu << 8) | NEOGEO_SYSTYPE_DEFAULT);
    if (addr >= 0xD00000u && addr < 0xD10000u) {
        u32 o = addr - 0xD00000u;
        return (u16)((ng->backup_ram[o] << 8) | ng->backup_ram[o + 1]);
    }
    if (is_memcard_addr(addr)) {
        return (u16)(0xFF00u | ng->memcard[memcard_index(addr)]);
    }
    if (addr < 0x200000u) {
        u32 o = mirror(addr, NEOGEO_WRAM_SIZE);
        return (u16)((ng->wram[o] << 8) | ng->wram[o + 1]);
    }
    if (is_pvc_addr(ng, addr)) return ng->pvc_ram[(addr - 0x2FE000u) >> 1]; /* Achado #83 */
    if (is_mslugx_prot_addr(ng, addr)) return mslugx_prot_read(ng); /* Achado #69 */
    if (is_cart_sram_addr(ng, addr)) { /* Achado #70 */
        u32 o = addr - 0x200000u;
        return (u16)((ng->cart_sram[o] << 8) | ng->cart_sram[o + 1u]);
    }
    if (is_sma_protr_addr(ng, addr)) { if (trace_sma()) { printf("[sma] protr read pc=%06x\n", ng->m68k.pc); fflush(stdout);} return 0x9a37u; } /* Achado #72 */
    if (is_sma_random_addr(ng, addr)) { u16 rv = neogeo_sma_random_step(&ng->sma_rng); if (trace_sma()) { printf("[sma] random read pc=%06x val=%04x rng_after=%04x\n", ng->m68k.pc, rv, ng->sma_rng); fflush(stdout);} return rv; }
    if (addr >= 0x2FE000u && addr < 0x300000u && trace_sma()) { printf("[sma] read16 addr=%06x prot_kof99=%d pc=%06x\n", addr, (int)ng->prot_kof99, ng->m68k.pc); fflush(stdout); }
    if (addr < 0x300000u) { /* Achado #48: leitura NÃO troca o banco. */
        u32 off = ng->p_bank_offset + (addr - 0x200000u);
        if (ng->rom.p_size == 0) return 0xFFFFu;
        u32 o0 = off % ng->rom.p_size;
        u32 o1 = (off + 1) % ng->rom.p_size;
        return (u16)((ng->rom.p[o0] << 8) | ng->rom.p[o1]);
    }
    if (addr >= 0x400000u && addr < 0x402000u) {
        u32 word_idx = (addr - 0x400000u) / 2u;
        if (word_idx >= NEOGEO_PALETTE_WORDS) return 0xFFFFu;
        return ng->palette[ng->palette_bank + word_idx];
    }
    {
        u16 rv;
        if (reg_read16(ng, addr, &rv)) return rv;
    }
    return 0xFFFFu;
}

static bool system_latch_write(neogeo_t *ng, u32 addr); /* declarado abaixo, ver Achado #41 */

/* Achado #65 (ver docs/ROADMAP.md): o pulso TP do RTC (µPD4990A) NÃO é
 * livre — sua FASE é REINICIADA toda vez que o 68000 manda um comando pro
 * chip. No MAME (`neogeo.cpp`, `io_control_w` com `offset & 0x78 == 0x28`):
 *     m_upd4990a->data_in_w(BIT(data,0));
 *     m_upd4990a->clk_w(BIT(data,1));
 *     m_upd4990a->stb_w(BIT(data,2));
 * e no `upd1990a.cpp`, na BORDA DE SUBIDA do STB, qualquer comando de
 * "time pulse" faz:
 *     m_timer_tp->adjust(attotime::zero, 0, one_second * mul / 2.0);
 * `attotime::zero` = dispara JÁ: o TP inverte na hora e o meio-período
 * recomeça a contar dali. Tínhamos um oscilador livre desde o reset, então
 * a fase ficava arbitrária. Como a BIOS conta VBLANKS entre bordas do TP
 * pra validar o timing, uma fase diferente deslocava o boot inteiro — o
 * nosso terminava ~49 frames ANTES do MAME, e por isso as telas animadas do
 * attract (o carrossel de bandeiras do Super Sidekicks 2) apareciam numa
 * posição diferente mesmo com o resto pixel-exato. */
static bool rtc_io_write(neogeo_t *ng, u32 addr, u8 val) {
    if (addr < 0x380000u || addr >= 0x380100u) return false;
    u32 offset = (addr - 0x380000u) / 2u;
    if ((offset & 0x78u) != 0x28u) return true; /* endereço nosso, mas outro pino */

    /* Achado #85: substituído pelo chip completo. A "fase reiniciada no STB"
     * do Achado #65 continua valendo — agora como consequência do comando de
     * TP que o STB executa, e não como regra genérica. */
    neogeo_rtc_write(ng, val);
    return true;
}

/* Achado #73: qualquer escrita em $300001 (espelhado em $300000-$31FFFF, ver
 * `map(0x300001, 0x300001).mirror(0x01fffe)` no neogeo.cpp do MAME) rearma o
 * watchdog. Retorna true se o endereço era do watchdog. */
static inline bool watchdog_write(neogeo_t *ng, u32 addr) {
    if (addr < 0x300000u || addr >= 0x320000u || !(addr & 1u)) return false;
    ng->watchdog_deadline = ng->master_cycles + NEOGEO_WATCHDOG_CYCLES;
    return true;
}

static void m68k_write8_cb(void *user, u32 addr, u8 val) {
    neogeo_t *ng = (neogeo_t *)user;
    addr &= 0xFFFFFFu;

    if (is_bios_addr(addr)) return; /* BIOS: somente leitura */
    if (is_cart_low_addr(addr)) return; /* Achado #39: espelho da P ROM, somente leitura */
    if (addr >= 0xD00000u && addr < 0xD10000u) { if (ng->save_ram_unlocked) ng->backup_ram[addr - 0xD00000u] = val; return; } /* Achado #84 */
    if (is_memcard_addr(addr)) { if (addr & 1u) ng->memcard[memcard_index(addr)] = val; return; }
    if (addr < 0x200000u) {
        /* Depuração: NEOGEO_WATCH_W=<endereço hex> loga escritas .B nesse byte. */
        static long watch = -2;
        if (watch == -2) { const char *e = getenv("NEOGEO_WATCH_W"); watch = e ? (long)strtoul(e, NULL, 16) : -1; }
        if (watch >= 0 && (addr & 0xFFFFu) == ((u32)watch & 0xFFFFu)) {
            printf("[watch] w8 %06x = %02x pc=%06x ciclos=%llu\n", addr, val, ng->m68k.pc, (unsigned long long)ng->master_cycles);
            fflush(stdout);
        }
        ng->wram[mirror(addr, NEOGEO_WRAM_SIZE)] = val;
        return;
    }
    if (is_pvc_addr(ng, addr)) { pvc_write(ng, addr, (addr & 1u) ? (u16)val : (u16)(val << 8), (addr & 1u) ? 0x00FFu : 0xFF00u); return; } /* Achado #83 */
    if (is_mslugx_prot_addr(ng, addr)) { mslugx_prot_write(ng, addr, (u16)val); return; } /* Achado #69 */
    if (is_kof98_prot_write(ng, addr)) { kof98_prot_write(ng, (u16)val); return; }       /* Achado #82 */
    if (is_cart_sram_addr(ng, addr)) { ng->cart_sram[addr - 0x200000u] = val; return; } /* Achado #70 */
    if (addr < 0x300000u) { p_bankswitch_write(ng, addr, val); return; } /* P ROM: somente leitura, exceto o latch de bankswitch */
    if (watchdog_write(ng, addr)) return;     /* Achado #73 */
    if (system_latch_write(ng, addr)) return; /* Achado #41 */
    if (rtc_io_write(ng, addr, val)) return;  /* Achado #65 */

    if (addr >= 0x400000u && addr < 0x402000u) {
        u32 word_idx = (addr - 0x400000u) / 2u;
        if (word_idx >= NEOGEO_PALETTE_WORDS) return;
        u32 pi = ng->palette_bank + word_idx;
        if (addr & 1u) ng->palette[pi] = (u16)((ng->palette[pi] & 0xFF00u) | val);
        else           ng->palette[pi] = (u16)((ng->palette[pi] & 0x00FFu) | ((u16)val << 8));
        return;
    }
    {
        /* Acesso .B a um registrador word-only é raro/incomum no código real
         * de jogos. Evitamos "ler pra mesclar" aqui de propósito: pra
         * VRAMRW isso disparava o auto-incremento duas vezes (leitura +
         * escrita) e corrompia vram_addr silenciosamente — pior que a
         * aproximação abaixo (byte alto e baixo ambos = val). TODO: revisar
         * se algum jogo real depender de escrita .B parcial nesses regs. */
        u32 aligned = addr & 0xFFFFFEu;
        /* Achado #93: `video_register_w` — "accessing the LSB only is not
         * mapped" e "accessing the MSB only stores same data in MSB and LSB". */
        if (aligned >= 0x3C0000u && aligned < 0x3C0010u && (addr & 1u)) return;
        if (aligned == NEOGEO_REG_SOUND || aligned == NEOGEO_REG_VRAMADDR ||
            aligned == NEOGEO_REG_VRAMMOD || aligned == NEOGEO_REG_LSPCMODE ||
            aligned == NEOGEO_REG_TIMERHIGH || aligned == NEOGEO_REG_TIMERLOW ||
            aligned == NEOGEO_REG_TIMERSTOP || aligned == NEOGEO_REG_IRQACK) {
            reg_write16(ng, aligned, (u16)((val << 8) | val));
            return;
        }
        if (aligned == NEOGEO_REG_VRAMRW) {
            vram_rw_write(ng, (u16)((val << 8) | val));
            return;
        }
    }
    /* Resto de 0x300000-0x3FFFFF (dipswitches, watchdog, coin counters
     * etc.): TODO Fase 3b/4 — ignorado por ora. */
}

/* Achado #27/#40/#41 (ver docs/ROADMAP.md): latch de controle do sistema
 * ($3A0000-$3A001F). Retorna true se tratou o endereço. Chamada tanto do
 * caminho de escrita .W quanto do .B — na prática só o .B é usado pelo
 * código real (ver Achado #41). */
static bool system_latch_write(neogeo_t *ng, u32 addr) {
    if (addr < 0x3A0000u || addr >= 0x3A0020u) return false;
    u32 word_off = (addr - 0x3A0000u) / 2u;
    u32 bit = word_off & 7u;
    u32 value = (word_off >> 3) & 1u;
    if (bit == 5u) ng->use_cart_fix = (u8)value;
    /* Achado #84: bit 6 = DESTRAVA a backup RAM ($D00000). No MAME
     *     m_systemlatch->q_out_cb<6>().set(FUNC(ngarcade_base_state::set_save_ram_unlock));
     *     void ngarcade_base_state::save_ram_w(...) {
     *         if (m_save_ram_unlocked) COMBINE_DATA(&m_save_ram[offset]); }
     * ou seja, escrita com a trava fechada é IGNORADA. Os jogos contam com
     * isso: o rotd tenta gravar "proteção falhou" em $D00100 com a RAM
     * travada, e no hardware real nada acontece. */
    if (bit == 6u) ng->save_ram_unlocked = (u8)value;
    if (bit == 1u) ng->use_cart_vectors = (u8)value;
    /* Achado #49 (ver docs/ROADMAP.md): bit 7 = seleção do BANCO DE PALETA.
     * Confirmado na fonte real do MAME (`neogeo.cpp`):
     *     m_systemlatch->q_out_cb<7>().set(FUNC(...::set_palette_bank));
     * e (`neogeo_v.cpp`):
     *     m_palette_bank = state ? 0x1000 : 0;
     * Faltava por completo aqui — a janela $400000-$401FFF sempre escrevia e
     * lia o banco 0. Jogos que usam o banco 1 (troca de paleta por cena, ou
     * escrever a próxima paleta enquanto a atual está na tela) ficavam com
     * cores erradas/lixo em partes da tela.
     * Achado #62: bit 0 = "screen shadow". No MAME (`neogeo_v.cpp`) ele só
     * troca a base dos pens pra `+0x2000`, que é a MESMA paleta convertida
     * com o resistor de pull-down de 150 ohms (ver Achado #58). Ou seja: é
     * um escurecimento do DAC inteiro, não um efeito por sprite. */
    if (bit == 0u) ng->screen_shadow = (u8)value;
    if (bit == 7u) {
        if (getenv("NEOGEO_TRACE_PALBANK")) {
            printf("[palbank] pc=%06x master_cycles=%llu valor=%u (era %u)\n",
                   ng->m68k.pc, (unsigned long long)ng->master_cycles, value,
                   ng->palette_bank ? 1u : 0u);
            fflush(stdout);
        }
        ng->palette_bank = value ? NEOGEO_PALETTE_WORDS : 0u;
    }
    return true;
}

static void m68k_write16_cb(void *user, u32 addr, u16 val) {
    neogeo_t *ng = (neogeo_t *)user;
    addr &= 0xFFFFFEu;

    if (is_bios_addr(addr)) return;
    if (is_cart_low_addr(addr)) return; /* Achado #39: espelho da P ROM, somente leitura */
    if (addr >= 0xD00000u && addr < 0xD10000u) {
        u32 o = addr - 0xD00000u;
        /* Diagnóstico temporário (Achado #38): quem mexe na tabela de
         * estatísticas diárias em $D00124? (NEOGEO_TRACE_SRAM=1) */
        if (o >= 0x120u && o < 0x134u) {
            static int hits = 0;
            if (hits < 30 && getenv("NEOGEO_TRACE_SRAM")) {
                hits++;
                printf("[sram] escrita.w em $D%05X = %04X (pc=%06X)\n", o, val, ng->m68k.pc);
                fflush(stdout);
            }
        }
        if (ng->save_ram_unlocked) { /* Achado #84 */
            ng->backup_ram[o] = (u8)(val >> 8);
            ng->backup_ram[o + 1] = (u8)val;
        }
        return;
    }
    if (is_memcard_addr(addr)) { ng->memcard[memcard_index(addr)] = (u8)val; return; }
    if (addr < 0x200000u) {
        u32 o = mirror(addr, NEOGEO_WRAM_SIZE);
        {   /* NEOGEO_WATCH_W também pega escrita .W que cubra o byte */
            static long watch = -2;
            if (watch == -2) { const char *e = getenv("NEOGEO_WATCH_W"); watch = e ? (long)strtoul(e, NULL, 16) : -1; }
            u32 w = (u32)watch & 0xFFFFu;
            if (watch >= 0 && (o == w || o + 1u == w)) {
                printf("[watch] w16 %06x = %04x pc=%06x ciclos=%llu\n", addr, val, ng->m68k.pc, (unsigned long long)ng->master_cycles);
                fflush(stdout);
            }
        }
        ng->wram[o] = (u8)(val >> 8);
        ng->wram[o + 1] = (u8)val;
        return;
    }
    if (is_pvc_addr(ng, addr)) { pvc_write(ng, addr, val, 0xFFFFu); return; } /* Achado #83 */
    if (is_mslugx_prot_addr(ng, addr)) { mslugx_prot_write(ng, addr, val); return; } /* Achado #69 */
    if (is_kof98_prot_write(ng, addr)) { kof98_prot_write(ng, val); return; }       /* Achado #82 */
    if (is_cart_sram_addr(ng, addr)) { /* Achado #70 */
        u32 o = addr - 0x200000u;
        ng->cart_sram[o] = (u8)(val >> 8);
        ng->cart_sram[o + 1u] = (u8)val;
        return;
    }
    if (addr < 0x300000u) { p_bankswitch_write(ng, addr, val); return; }
    /* Achado #73: escrita .W em $300000 põe o byte baixo em $300001 — no MAME
     * o handler de 8 bits do watchdog é chamado igual. `addr` já vem par aqui,
     * então testamos o endereço do byte ímpar explicitamente. */
    if (watchdog_write(ng, addr | 1u)) return;

    /* Achado #27 (ver docs/ROADMAP.md): registrador de controle do sistema
     * ($3A0000-$3A001F), um latch HC259 real — o valor ESCRITO (`val`) é
     * ignorado, o que importa é o ENDEREÇO: bits 1-3 do offset de palavra
     * escolhem qual dos 8 bits do latch, bit 3 escolhe o valor (0 ou 1).
     * Confirmado direto no driver `neogeo.cpp` do MAME (`hc259_device::
     * write_a3`, `addressable_latch_device::write_a3`): `write_bit(offset&7,
     * (offset&8)>>3)`. Só bit5 (`set_use_cart_audio` no MAME, que também
     * mexe no banco de áudio do Z80) importa pra fix layer por ora; os
     * outros bits (sombra de tela, lock de memory card, unlock de save RAM,
     * banco de paleta) ainda não são implementados — TODO.
     *
     * Achado #41 (ver docs/ROADMAP.md): esse bloco só existia no caminho de
     * escrita .W (`m68k_write16_cb`) — mas TODO o código real da BIOS/jogo
     * escreve nesse latch com `move.b` (byte), nunca `move.w`. Ou seja, essa
     * lógica NUNCA disparava de verdade; `use_cart_fix`/`use_cart_vectors`
     * ficavam presos no valor de reset pra sempre. Extraído pra função
     * própria, chamada dos dois caminhos (byte e word). */
    if (system_latch_write(ng, addr)) return;

    if (addr >= 0x400000u && addr < 0x402000u) {
        u32 word_idx = (addr - 0x400000u) / 2u;
        if (word_idx < NEOGEO_PALETTE_WORDS) ng->palette[ng->palette_bank + word_idx] = val;
        return;
    }
    reg_write16(ng, addr, val);
}

/* ---- Z80: mapa de memória e I/O -----------------------------------------
 * TODO(Fase 3b): bankswitch de M1 > 64KB e portas reais do YM2610 (FM/ADPCM)
 * não implementados — ver comentário no topo do arquivo. Por ora: RAM
 * espelhada em 0x0000-0x7FFF, M1 fixo (primeiros 32KB) em 0x8000-0xFFFF,
 * porta 0x00 usada como latch de comunicação simplificado com o 68000. */

/* Mapa de memória do Z80 (confirmado contra a NeoGeo Development Wiki,
 * página "Z80" — ANTES estava com RAM e ROM invertidas, um bug sério que
 * fazia o Z80 executar NOPs a partir de RAM em branco em vez do driver de
 * som real, já que os vetores de reset/NMI vivem nos primeiros bytes):
 *   $0000-$7FFF: Audio BIOS fixa (32KB, sm1.sm1 — compartilhada entre
 *                jogos, igual à BIOS do 68k; NÃO é a M1 do cartucho — achado
 *                comparando trace-a-trace contra o MAME byte a byte, ver
 *                docs/ROADMAP.md)
 *   $8000-$BFFF: janela de bankswitch 3 (16KB) -> M1 do cartucho
 *   $C000-$DFFF: janela de bankswitch 2 (8KB) -> M1 do cartucho
 *   $E000-$EFFF: janela de bankswitch 1 (4KB) -> M1 do cartucho
 *   $F000-$F7FF: janela de bankswitch 0 (2KB) -> M1 do cartucho
 *   $F800-$FFFF: Work RAM (2KB)
 * TODO: bankswitch real das 4 janelas (NEO-ZMC/ZMC2) não implementado —
 * por ora mapeiam direto pro mesmo offset dentro da M1 do cartucho (banco 0
 * sempre), mesma abordagem provisória usada pro P ROM antes do bankswitch
 * real. */
static u8 z80_read_cb(void *ctx, u16 addr) {
    neogeo_t *ng = (neogeo_t *)ctx;
    if (addr >= 0xF800u) {
        return ng->sound_ram[(addr - 0xF800u) & (NEOGEO_SOUND_RAM_SIZE - 1u)];
    }
    if (addr < 0x8000u) {
        /* Achado #55 (ver docs/ROADMAP.md): esta janela NÃO é fixa na BIOS de
         * áudio — ela ALTERNA entre a SM1 (BIOS) e a M1 do CARTUCHO, pelo bit
         * 5 do latch de sistema. Confirmado na fonte do MAME:
         *     m_systemlatch->q_out_cb<5>().set(... set_use_cart_audio);
         *     void set_use_cart_audio(int s) { m_bank_audio_main->set_entry(s); }
         *   (entry 0 = "audiobios"/SM1, entry 1 = ROM de áudio do cartucho)
         * Deixando fixo na SM1, o Z80 rodava pra sempre o driver da BIOS — que
         * só conhece os sons da BIOS — e por isso NUNCA tocava nada do jogo,
         * mesmo recebendo os comandos. O mesmo bit também escolhe a fonte da
         * fix layer (`use_cart_fix`, Achado #27): é um bit, dois efeitos. */
        if (ng->use_cart_fix && ng->rom.m_size != 0) {
            return ng->rom.m[addr % ng->rom.m_size];
        }
        if (ng->rom.audiobios_size == 0) {
            /* Placa sem SM1: a ROM do cartucho fica sempre ligada. */
            return (ng->rom.m_size != 0) ? ng->rom.m[addr % ng->rom.m_size] : 0xFFu;
        }
        return ng->rom.audiobios[addr % ng->rom.audiobios_size];
    }
    if (ng->rom.m_size == 0) return 0xFFu;
    {
        /* Janelas de bankswitch: 3=$8000(16K),2=$C000(8K),1=$E000(4K),
         * 0=$F000(2K) — ver z80_io_read_cb e z80_bank_offset. */
        u32 window_idx, window_base;
        if (addr < 0xC000u)      { window_idx = 3u; window_base = 0x8000u; }
        else if (addr < 0xE000u) { window_idx = 2u; window_base = 0xC000u; }
        else if (addr < 0xF000u) { window_idx = 1u; window_base = 0xE000u; }
        else                     { window_idx = 0u; window_base = 0xF000u; }
        u32 off = ng->z80_bank_offset[window_idx] + (addr - window_base);
        /* O `% m_size` final equivale ao `ROM_RELOAD` que o MAME usa pra
         * espelhar M1 pequeno dentro da região de áudio — e garante que nunca
         * lemos fora do buffer. */
        return ng->rom.m[off % ng->rom.m_size];
    }
}

/* Achado #74 (ver docs/ROADMAP.md): bankswitch de áudio EXATAMENTE como o
 * `neogeo_base_state::audio_cpu_banking_init` do MAME:
 *
 *   uint32_t const address_mask = (len - 0x10000 - 1) & 0x3ffff;
 *   for (region = 0..3) for (bank = 0xff..0)
 *       bank_address = 0x10000 + ((bank << (11 + region)) & address_mask);
 *
 * Dois detalhes que eu tinha errado antes:
 *  1. As janelas de banco olham a partir de **$10000** da M1, não do começo —
 *     os primeiros 64KB são a área fixa ($0000-$7FFF do Z80).
 *  2. O recorte é um **AND com address_mask**, não um `% m_size`. Pra M1 de
 *     512KB a máscara dá 0x2FFFF (não é potência de 2 menos 1 — é assim
 *     mesmo no MAME).
 * `m_size < 0x20000` usa 0x20000 como comprimento efetivo porque no MAME
 * esses cartuchos declaram região de 0x20000 e duplicam a ROM com
 * `ROM_RELOAD`; o `% m_size` na leitura reproduz esse espelhamento. */
/* CORREÇÃO do próprio Achado #74 (Achado #80): eu tinha lido o `0x10000 +`
 * como "as janelas olham 64KB adiante na M1". ERRADO. As macros de romset do
 * MAME carregam a M1 DUAS VEZES na região de áudio:
 *     #define NEO_BIOS_AUDIO_128K(name, hash) ...
 *         ROM_REGION( 0x30000, "cslot1:audiocpu", 0 )
 *         ROM_LOAD( name, 0x00000, 0x20000, hash )
 *         ROM_RELOAD(     0x10000, 0x20000 )
 * Ou seja: região = [M1][M1 de novo a partir de 0x10000], com tamanho
 * `m_size + 0x10000`. `0x10000 + x` nessa região é `M1[x]`. Como o nosso
 * `rom.m` é o arquivo CRU (sem a cópia), em termos dele:
 *     offset = (bank << (11 + region)) & ((m_size - 1) & 0x3FFFF)
 * sem somar nada. (Confere: `(region_len - 0x10000 - 1) & 0x3ffff` com
 * region_len = m_size + 0x10000 dá exatamente `(m_size - 1) & 0x3ffff`.)
 * A versão errada fazia a janela $8000 inicial ler M1[$18000] em vez de
 * M1[$8000]. */
void neogeo_z80_set_bank(neogeo_t *ng, u32 region, u8 bank) {
    if (region > 3u) return;
    ng->z80_bank_offset[region] = ((u32)bank << (11u + region)) & ng->z80_bank_mask;
}

void neogeo_z80_banking_init(neogeo_t *ng) {
    u32 len = (ng->rom.m_size != 0u) ? ng->rom.m_size : 0x10000u;
    ng->z80_bank_mask = (len - 1u) & 0x3FFFFu;
    /* Bancos iniciais — o MAME chama isso de HACK e explica por quê: o
     * bankswitch de verdade é do chip NEO-ZMC do cartucho, e cartuchos antigos
     * com menos de 64KB de código Z80 não têm ZMC nenhum. Esses valores fazem
     * cada janela apontar pro endereço "identidade" ($8000 -> $8000 etc), que
     * é o que jogos não-bancados esperam. */
    static const u8 init_entry[4] = { 0x1Eu, 0x0Eu, 0x06u, 0x02u }; /* F000,E000,C000,8000 */
    for (u32 r = 0; r < 4u; r++) neogeo_z80_set_bank(ng, r, init_entry[r]);
}

static void z80_write_cb(void *ctx, u16 addr, u8 val) {
    neogeo_t *ng = (neogeo_t *)ctx;
    if (addr >= 0xF800u) {
        ng->sound_ram[(addr - 0xF800u) & (NEOGEO_SOUND_RAM_SIZE - 1u)] = val;
    }
    /* addr < 0xF800: ROM (fixa ou bankswitch), somente leitura. */
}

/* Mapa de portas de I/O do Z80 (confirmado contra a NeoGeo Development
 * Wiki, página "Z80 port map" — corrige um erro real: eu tinha a resposta
 * pro 68k na porta errada):
 *   $00 (leitura): pega o comando do 68k e confirma (limpa) a NMI
 *   $04-$07: YM2610 (Fase 5, não implementado ainda)
 *   $08 (leitura): seleciona o banco da janela $F000-$F7FF
 *   $09 (leitura): seleciona o banco da janela $E000-$EFFF
 *   $0A (leitura): seleciona o banco da janela $C000-$DFFF
 *   $0B (leitura): seleciona o banco da janela $8000-$BFFF
 *   $08 (escrita): habilita NMI de som; $18 (escrita): desabilita
 *   $0C (escrita): manda a resposta de volta pro 68k (REG_SOUND) — ANTES eu
 *     tinha isso na porta $00 por engano.
 * A seleção de banco usa um truque conhecido do Z80: em "IN A,(n)", o valor
 * ATUAL do acumulador A fica no barramento de endereço durante o ciclo de
 * leitura — então o código faz "LD A,banco / IN A,(porta)" e o hardware
 * usa esse A como número do banco, não o dado retornado. Como nosso core
 * chama a callback de leitura ANTES de sobrescrever A com o resultado
 * (`SET_A(io_rd(z,port))`), `ng->z80.af>>8` ainda reflete esse "banco"
 * corretamente aqui. */
/* Achado #86: "agora" em ciclos do 68000, visto de DENTRO do Z80. O Z80 roda
 * em lotes depois de cada instrução do 68000 e `master_cycles` só é somado
 * no fim do lote, então o tempo corrente = início do lote + T-states do Z80
 * já executados * 3 (4 MHz vs 12 MHz). */
static inline u64 ym_now(const neogeo_t *ng) {
    return ng->master_cycles + (u64)((ng->z80.cycles > 0 ? ng->z80.cycles : 0) * 3);
}

static u8 z80_io_read_cb(void *ctx, u8 port) {
    neogeo_t *ng = (neogeo_t *)ctx;
    if (port == 0x00u) {
        if (getenv("NEOGEO_TRACE_SNDCMD")) {
            printf("[z80] leu comando $%02x (pc=%04x)\n", ng->sound_latch_to_z80, ng->z80.pc);
            fflush(stdout);
        }
        ng->sound_latch_pending = false;
        z80_nmi_update(ng); /* Achado #89: ler confirma e derruba a linha */
        return ng->sound_latch_to_z80;
    }
    if (port >= 0x08u && port <= 0x0Bu) {
        u8 bank = (u8)(ng->z80.af >> 8);
        u32 idx = (u32)(port - 0x08u); /* 0=$F000(2K),1=$E000(4K),2=$C000(8K),3=$8000(16K) */
        neogeo_z80_set_bank(ng, idx, bank);
        return 0xFFu;
    }
    /* Achado #25 (ver docs/ROADMAP.md): portas $04/$06 são o registrador
     * de STATUS do YM2610 (par A/B) — bits de "busy" (FM ocupado/SSG-
     * ADPCM ocupado). O chip inteiro ainda não está implementado (Fase
     * 5), mas retornar 0xFF (todos os bits de busy setados) faz a
     * própria rotina de boot/self-test do Z80 achar que o chip está
     * sempre ocupado, o que pode explicar uma divergência real vista
     * comparando a RAM do Z80 contra o MAME já no primeiro vblank.
     * Aproximação barata: responder "nunca ocupado" (0x00) sem
     * implementar o chip inteiro. TODO(Fase 5): registrador de dados
     * ($05/$07) e o chip de verdade. */
    /* Achado #86: `ym2610::read(offset)` do ymfm, offset = porta - 4:
     *   0 ($04) status: timers A/B + bit7 OCUPADO
     *   1 ($05) dado: registrador do SSG ($00-$0D), 0xFF em $0E/$0F, ID=1 em $FF
     *   2 ($06) status alto: flags de FIM DE AMOSTRA do ADPCM (não é timer!)
     *   3 ($07) sempre 0 */
    if (port == 0x04u) {
        u8 v = neogeo_ym2610_read_status(ng);
        if (ym_now(ng) < ng->ym_busy_end) v |= 0x80u;
        return v;
    }
    if (port == 0x05u) {
        u16 a = ng->ym_address9;
        if (a < 0x0Eu) return ng->ym.regs_a[a];
        if (a < 0x10u) return 0xFFu;
        if (a == 0xFFu) return 0x01u;
        return 0x00u;
    }
    if (port == 0x06u) return neogeo_ym2610_read_status_hi(ng);
    if (port == 0x07u) return 0x00u;
    return 0xFFu;
}

static void z80_io_write_cb(void *ctx, u8 port, u8 val) {
    neogeo_t *ng = (neogeo_t *)ctx;
    if (port == 0x0Cu) {
        ng->sound_latch_to_68k = val;
        if (getenv("NEOGEO_TRACE_SNDCMD")) {
            printf("[sndcmd] Z80 -> 68k: $%02x (z80pc=%04x) ciclos68k=%llu\n",
                   val, ng->z80.pc, (unsigned long long)ym_now(ng));
            fflush(stdout);
        }
        return;
    }
    /* Achado #89: porta $00 na escrita = `generic_latch_8_device::clear_w`,
     * zera o valor do latch (não mexe no "pendente"). */
    if (port == 0x00u) {
        ng->sound_latch_to_z80 = 0;
        return;
    }
    /* Fase 5: habilita/desabilita a NMI de som. O driver do cartucho
     * DESLIGA a NMI enquanto processa um comando e RELIGA quando está
     * pronto pro próximo — disparar NMI incondicionalmente (o que fazíamos)
     * re-entra no handler no meio do processamento e embaralha o estado do
     * driver, que era por que ele inicializava mas nunca chegava a tocar
     * nada. */
    if (port == 0x08u || port == 0x18u) {
        ng->z80_nmi_enabled = (port == 0x08u) ? 1u : 0u;
        z80_nmi_update(ng); /* Achado #89 */
        if (getenv("NEOGEO_TRACE_SNDCMD")) {
            printf("[z80] NMI %s (pc=%04x)\n", ng->z80_nmi_enabled ? "LIGADA" : "desligada", ng->z80.pc);
            fflush(stdout);
        }
        return;
    }

    /* Fase 5 / YM2610. Portas: $04=endereco A, $05=dado A, $06=endereco B,
     * $07=dado B. "A" e "B" sao os dois bancos de registrador do chip.
     * Ainda nao ha sintese; por ora guardamos o registrador selecionado e
     * o ultimo valor escrito, e (com NEOGEO_TRACE_YM=1) imprimimos o
     * trafego, pra implementar contra dados reais em vez de adivinhacao. */
    if (port >= 0x04u && port <= 0x07u) {
        bool is_data = (port == 0x05u || port == 0x07u);
        u32 bank = (port <= 0x05u) ? 0u : 1u;
        if (!is_data) {
            ng->ym_addr[bank] = val;
            /* Achado #86: o chip tem UM latch de endereço de 9 bits
             * (`m_address` no ymfm); a porta $06 liga o bit 8. */
            ng->ym_address9 = (u16)((bank ? 0x100u : 0u) | val);
        } else {
            /* Achado #86 (`ym2610::write_data`/`write_data_hi` do ymfm):
             * dado na porta errada pro endereço latchado é IGNORADO, e toda
             * escrita aceita deixa o chip OCUPADO por 32*prescale(6) = 192
             * clocks de 8 MHz = 24 µs = 288 ciclos do 68000. */
            if (((ng->ym_address9 >> 8) & 1u) != bank) return;
            ng->ym_busy_end = ym_now(ng) + 288u;
            neogeo_ym2610_write_reg(ng, bank, ng->ym_addr[bank], val);
            ng->ym_writes++;
            if (getenv("NEOGEO_TRACE_YM")) {
                printf("[ym] banco %c reg $%02x = $%02x t=%llu\n",
                       bank ? 'B' : 'A', ng->ym_addr[bank], val,
                       (unsigned long long)ym_now(ng));
                fflush(stdout);
            }
        }
        return;
    }
    (void)ng;
}

/* ---- API pública ---------------------------------------------------------- */

void neogeo_init(neogeo_t *ng, const neogeo_romset_t *rom) {
    memset(ng, 0, sizeof(*ng));
    ng->rom = *rom;

    /* Achado #26 (ver docs/ROADMAP.md): a checagem de boot em `$C1B6FE`
     * (chamada por `$C11D72`, parte da decisão "entra no self-test ou
     * não" do Achado #20) exige um memory card VÁLIDO e formatado — testa
     * a assinatura "NEO-GEO" nos bytes reais (ímpares) de $800040-$80005C
     * (passo 4, byte baixo de cada word), um byte de formato em $800010
     * (nibble alto = 2) e um valor não-zero em $800074. Sem isso, a
     * checagem sempre retorna "falhou" (D5=0) e o boot SEMPRE cai no
     * self-test, não importa o resto. Simulamos um cartão formatado e
     * "em branco" (mas válido) por padrão — mais fiel ao hardware real
     * (cartões de fábrica/formatados já vêm com esse cabeçalho) do que
     * simplesmente reportar "sem cartão" o tempo todo. memcard_index(addr)
     * = (addr>>1) & 0x7FF, então os bytes ímpares $800041/45/49/4D/51/55/
     * 59/5D caem nos índices 0x20,0x22,0x24,...,0x2E. */
    static const u8 memcard_sig[] = { 'N', 'E', 'O', '-', 'G', 'E', 'O', 0x80u };
    for (size_t i = 0; i < sizeof(memcard_sig); i++) {
        ng->memcard[0x20u + i * 2u] = memcard_sig[i];
    }
    ng->memcard[0x08u] = 0x20u; /* formato válido (nibble alto = 2) em $800010 */
    ng->memcard[0x3Au] = 0x01u; /* valor não-zero em $800074 */

    m68k_core_init(&ng->m68k, m68k_read8_cb, m68k_read16_cb,
                   m68k_write8_cb, m68k_write16_cb, ng);

    Z80_Init(&ng->z80);
    ng->z80.ctx = ng;
    ng->z80.read = z80_read_cb;
    ng->z80.write = z80_write_cb;
    ng->z80.io_read = z80_io_read_cb;
    ng->z80.io_write = z80_io_write_cb;
}

/* `clear_memory=false` é o caso do reset por watchdog (Achado #73): no
 * hardware real o pulso de reset não apaga DRAM/SRAM nenhuma — a RAM de
 * trabalho, a VRAM e a paleta continuam com o que estavam. É exatamente o que
 * o MAME faz também (o reset da máquina reseta os *dispositivos*, não zera as
 * regiões de memória). */
static void reset_common(neogeo_t *ng, bool clear_memory) {
    if (clear_memory) {
        memset(ng->wram, 0, sizeof(ng->wram));
        memset(ng->sound_ram, 0, sizeof(ng->sound_ram));
        memset(ng->palette, 0, sizeof(ng->palette));
        memset(ng->vram, 0, sizeof(ng->vram));
    }
    ng->palette_bank = 0; /* Achado #49 */
    /* Achado #48: o banco 0 (padrão) mostra o SEGUNDO megabyte — o primeiro
     * já está fixo em $000000-$0FFFFF. Cartucho de 1MB ou menos não tem banco
     * nenhum: deixa 0 pra não ler fora da ROM. */
    ng->p_bank_offset = (ng->rom.p_size > 0x100000u) ? 0x100000u : 0u;

    /* Achado #69: identifica o cartucho pelo NGH (número do jogo), que fica
     * no cabeçalho padrão da P ROM em $000108 como BCD. É o mesmo dado que a
     * BIOS lê — mais honesto que olhar o nome da pasta. NGH 0250 = Metal
     * Slug X, o único cartucho com chip de proteção que temos aqui. */
    ng->prot_mslugx = 0;
    ng->prot_counter = 0;
    ng->prot_command = 0;
    ng->cart_sram_enabled = 0;
    if (ng->rom.p_size > 0x10Au) {
        u16 ngh = (u16)(((u16)ng->rom.p[0x108] << 8) | ng->rom.p[0x109]);
        if (ngh == MSLUGX_NGH) ng->prot_mslugx = 1;
        if (ngh == VLINER_NGH) ng->cart_sram_enabled = 1;
    }

    /* Achado #72: KOF99 (chip SMA+CMC42). Diferente do mslugx/vliner acima,
     * a identificação vem do CARREGADOR (`rs->is_kof99`), não do NGH — o
     * cabeçalho da P ROM só existe DEPOIS da descriptografia que o próprio
     * carregador já fez, então usar o NGH aqui seria só reconferir a mesma
     * decisão de um jeito mais indireto. */
    ng->prot_kof99 = ng->rom.is_kof99 ? 1u : 0u;
    ng->sma_rng = 0x2345u; /* semente do LFSR, igual ao MAME (device_reset) */
    ng->sma_type = ng->rom.sma_type; /* Achado #79 */
    ng->prot_kof98 = ng->rom.is_kof98 ? 1u : 0u; /* Achado #82 */
    ng->prot_pvc = ng->rom.is_pvc ? 1u : 0u;     /* Achado #83 */
    if (clear_memory) memset(ng->pvc_ram, 0, sizeof(ng->pvc_ram));
    if (ng->rom.has_cart_ram) ng->cart_sram_enabled = 1; /* Achado #83: jockeygp */
    if (ng->prot_kof98 && ng->rom.p_size >= 0x104u) {
        ng->kof98_default[0] = (u16)((ng->rom.p[0x100] << 8) | ng->rom.p[0x101]);
        ng->kof98_default[1] = (u16)((ng->rom.p[0x102] << 8) | ng->rom.p[0x103]);
    }
    /* `m_prot_state` NÃO é zerado no device_reset do MAME (só no construtor),
     * então só zeramos no reset a frio. */
    if (clear_memory) ng->kof98_state = 0;
    neogeo_sma_addrs(ng->sma_type, &ng->sma_bank_addr, &ng->sma_rand_addr1, &ng->sma_rand_addr2);

    neogeo_z80_banking_init(ng); /* Achado #74 */
    ng->sound_latch_to_z80 = 0;
    /* Achado #89: `neogeo_base_state::machine_reset` faz
     *     m_audionmi->in_w<1>(0);          // NMI DESLIGADA
     *     m_soundlatch->acknowledge_w();   // nada pendente
     * Nós começávamos com a NMI ligada. */
    ng->z80_nmi_enabled = 0;
    ng->z80_nmi_line = 0;
    neogeo_ym2610_reset(ng);
    ng->audio_rd = ng->audio_wr = 0;
    ng->audio_last_l = ng->audio_last_r = 0;
    ng->ym_mute_fm = getenv("NEOGEO_MUTE_FM") != NULL;
    ng->ym_mute_adpcm = getenv("NEOGEO_MUTE_ADPCM") != NULL;
    ng->ym_mute_ssg = getenv("NEOGEO_MUTE_SSG") != NULL;
    ng->ym_address9 = 0;
    ng->ym_busy_end = 0;
    ng->z80_credit = 0;
    ng->sound_latch_to_z80_write_count = 0;
    ng->sound_latch_to_68k = 0;
    ng->sound_latch_pending = false;
    ng->vram_addr = 0;
    ng->vram_mod = 0;
    ng->lspcmode = 0;
    ng->auto_anim_counter = 0;
    ng->auto_anim_frame_counter = 0;
    ng->auto_anim_speed = 0;
    ng->auto_anim_disabled = 0;
    ng->timer_reload = 0;
    ng->timerstop = 0;
    /* Achado #93: no power-on o MAME deixa a IRQ3 pendente
     * (`machine_start`: m_irq3_pending = 1) até a BIOS confirmar; e a tela
     * NÃO para num reset por watchdog — só o power-on zera a fase do frame. */
    if (clear_memory) {
        ng->irq_pending = NEOGEO_IRQACK_RESET;
        ng->irq_time[0] = 0; ng->irq_time[1] = 0; ng->irq_time[2] = 0;
        ng->cycles_since_vblank = 0;
        ng->display_counter = 0;
        ng->display_control = 0;
        ng->disp_timer_time = 0;
        ng->anim_time = 12288u; /* 1a linha 256: o frame começa na 240 */
        ng->vram_read_buffer = 0;
    }
    ng->vbl_irq_time = 0;
    ng->vbl_reload_time = 0;
    /* O µPD4990A tem bateria própria e NÃO para no reset da placa: num reset
     * por watchdog (Achado #73) o relógio segue de onde estava. Só o
     * power-on/cold reset zera a contagem de ciclos. */
    if (clear_memory) {
        ng->master_cycles = 0;
        ng->vblank_count_for_rtc = 0;
        /* Achado #85: o upd1990a do MAME só tem device_start (nada de
         * device_reset) — o chip só "liga" no power-on. */
        neogeo_rtc_init(ng);
    }
    /* Watchdog rearmado pelo reset, como no `device_reset` do
     * watchdog_timer_device do MAME. */
    ng->watchdog_deadline = ng->master_cycles + NEOGEO_WATCHDOG_CYCLES;
    ng->p1_buttons = 0xFFu; /* nada pressionado, ativo-baixo */
    ng->p2_buttons = 0xFFu;
    ng->sys_buttons = 0xFFu;
    ng->coin_buttons = 0xFFu;

    /* Achado #38 REMOVIDO no Achado #85: a gente zerava a data salva em
     * `$D00104` pra "bater" com um relógio que sempre lia 00-00-00. Com o
     * µPD4990A de verdade, a BIOS lê a data real (como o MAME) e decide o
     * caminho do boot sozinha. */
    ng->save_ram_unlocked = 0; /* Achado #84: o HC259 zera no reset */
    ng->use_cart_fix = 0; /* Achado #27: BIOS SFIX ativa no reset (confirmado no MAME: m_use_cart_audio=0) */
    /* Achado #105: `addressable_latch_device::device_reset` zera as 8 saídas
     * do HC259 — inclusive os VETORES DO CARTUCHO (bit 1) e a sombra (bit 0).
     * No power-on o memset do neogeo_init já fazia isso, mas no reset do
     * watchdog os vetores continuavam do cartucho: o 68000 lia SSP/PC do
     * header da P ROM (fatfury3: SSP = $0000F300), a pilha caía na ROM e o
     * primeiro RTS voltava pra lixo. */
    ng->use_cart_vectors = 0;
    ng->screen_shadow = 0;

    m68k_core_reset(&ng->m68k);
    Z80_Reset(&ng->z80);
    irq_update(ng); /* Achado #93: `machine_reset` -> update_interrupts() */
}

void neogeo_reset(neogeo_t *ng) {
    ng->watchdog_resets = 0;
    reset_common(ng, true);
}

/* Ciclos de 68000 por linha de varredura (264 linhas/frame, aproximado). Ver
 * comentário sobre REG_LSPCMODE em reg_read16 — dividir em fatias deste
 * tamanho (em vez de rodar o frame inteiro de uma vez) é o que faz
 * REG_LSPCMODE (e qualquer polling de hardware) realmente progredir: se
 * chamássemos m68k_core_run_cycles com o frame inteiro numa tacada só, o
 * próprio mecanismo de "detecção de loop estável" do core (que existe pra
 * performance) veria o registrador "parado" por várias iterações seguidas
 * e passaria a pular ciclos sem re-executar nada — e como só atualizávamos
 * cycles_since_vblank DEPOIS da chamada inteira, o valor realmente ficava
 * congelado durante todo o frame. Isso travou o boot real da BIOS por um
 * bom tempo até sermos descobrir (ver docs/ROADMAP.md). */
/* Usamos m68k_core_step() instrução a instrução em vez de
 * m68k_core_run_cycles(), mesmo em fatias pequenas: descobrimos comparando
 * contra um trace real do MAME que run_cycles() tem uma otimização interna
 * de "detecção de loop estável" que pode pular a RE-EXECUÇÃO de instruções
 * quando registrador/PC parecem não mudar por algumas iterações — o que é
 * inseguro pra qualquer registrador com efeito colateral em cada acesso
 * (como VRAMRW, que auto-incrementa vram_addr a cada leitura/escrita).
 * Rodar com run_cycles() e com step() direto levava nossa emulação a
 * travar em pontos DIFERENTES do boot pra um mesmo total de trabalho — ou
 * seja, essa otimização estava silenciosamente alterando o comportamento
 * observável. step() não tem esse atalho. Custa um pouco de performance;
 * corretude primeiro. Ver docs/ROADMAP.md. */
/* Achado #54 (ver docs/ROADMAP.md): roda EXATAMENTE até a próxima borda de
 * vblank, em vez de "um frame de ciclos a partir de onde estávamos". A
 * diferença importa pra quem RENDERIZA: o conteúdo que o hardware real
 * mostra num frame é o que estava na VRAM/paleta durante a varredura da
 * área visível, e o jogo escreve as atualizações DEPOIS disso, dentro do
 * vblank. Amostrando num ponto arbitrário do frame, a gente pegava o jogo no
 * meio da rotina de atualização — paleta nova já escrita, tiles ainda
 * velhos — e o resultado era um frame "rasgado" (a foto do Double Dragon
 * saía magenta por 1 frame, Achado #54).
 *
 * Overshoot de no máximo uma instrução (o laço de `neogeo_run_slice` só
 * checa o orçamento entre instruções), o que é irrelevante aqui. */
u32 neogeo_run_until_vblank(neogeo_t *ng) {
    u32 remaining = NEOGEO_CYCLES_PER_FRAME - ng->cycles_since_vblank;
    if (remaining == 0) remaining = NEOGEO_CYCLES_PER_FRAME;
    /* Achado #105: o `neogeo_run_slice` devolve o controle no meio da fatia
     * quando o watchdog reseta a placa. Sem continuar até o alvo absoluto,
     * a chamada terminava antes do vblank e as ferramentas passavam a contar
     * um quadro a menos depois de cada reset. */
    u64 target = ng->master_cycles + remaining;
    u64 start = ng->master_cycles;
    while (ng->master_cycles < target)
        neogeo_run_slice(ng, (u32)(target - ng->master_cycles));
    return (u32)(ng->master_cycles - start);
}

/* Achado #68: render POR LINHA DE VARREDURA.
 *
 * Até aqui rodávamos um frame inteiro de CPU e só então desenhávamos a tela
 * uma vez (`neogeo_run_until_vblank` + `neogeo_render_frame`). Isso
 * funciona enquanto o jogo só mexe em VRAM/paleta durante o vblank — mas o
 * Neo-Geo tem um TIMER de raster (registradores $3C0008/$3C000A) cuja IRQ
 * dispara numa LINHA escolhida, justamente pra trocar paleta ou posição no
 * meio da tela (barras de água, céu em degradê, divisão de tela). Com
 * render de frame inteiro, essas trocas ou apareciam na tela toda ou não
 * apareciam.
 *
 * Agora o frame roda em 264 fatias de uma linha cada e as 224 linhas
 * visíveis são desenhadas uma a uma, com o estado do chip NAQUELE momento —
 * que é como o hardware funciona. As 16 linhas de vblank antes da área
 * visível (NEOGEO_VBEND_LINES) e as de baixo rodam sem desenhar.
 *
 * Custo: a varredura de sprites passa a rodar por linha (381 sprites x 224
 * linhas), o mesmo que o MAME faz em `parse_sprites`. */
#define NEOGEO_LINES_TOTAL 264u

u32 neogeo_run_frame_render(neogeo_t *ng, u8 *out_rgb888) {
    /* Achado #98: EXATAMENTE um frame por chamada.
     *
     * A versão anterior media o progresso por `cycles_since_vblank`, que dá a
     * volta no fim do frame. Na última linha ele virava e ficava com poucos
     * ciclos de sobra, e o "garante que o frame fechou" calculava
     * `202752 - sobra` e rodava QUASE UM FRAME INTEIRO A MAIS, sem desenhar.
     * Medido com core/tools/check_frame_speed: 600 chamadas emulavam 1092
     * frames = 182% da velocidade real. As ferramentas de teste usam
     * `neogeo_run_until_vblank` e não pegavam isso — só o frontend.
     *
     * Agora os alvos são ABSOLUTOS em `master_cycles`, ancorados no início do
     * vblank corrente: `ancora + (linha+1) * 202752 / 264`. O último alvo é o
     * próximo vblank; o excesso da última instrução fica pro frame seguinte
     * (a próxima âncora já desconta), então não há deriva. */
    u64 anchor = ng->master_cycles - ng->cycles_since_vblank;
    u64 start = ng->master_cycles;
    for (u32 line = 0; line < NEOGEO_LINES_TOTAL; line++) {
        u64 target = anchor + ((u64)NEOGEO_CYCLES_PER_FRAME * (line + 1u)) / NEOGEO_LINES_TOTAL;
        if (target > ng->master_cycles) {
            u64 budget = target - ng->master_cycles;
            neogeo_run_slice(ng, (u32)budget);
        }
        /* Desenha a linha que ACABOU de ser varrida. A âncora é o início do
         * vblank, que no MAME é a linha crua 240 (VBSTART) — então a linha
         * crua desta fatia é (240 + line) % 264, e a área visível vai de 16
         * (VBEND) a 239. */
        u32 vpos = (240u + line) % NEOGEO_LINES_TOTAL;
        if (out_rgb888 && vpos >= NEOGEO_VBEND_LINES &&
            vpos < NEOGEO_VBEND_LINES + NEOGEO_SCREEN_H) {
            u32 y = vpos - NEOGEO_VBEND_LINES;
            neogeo_render_frame_range(ng, out_rgb888, y, y + 1u);
        }
    }
    return (u32)(ng->master_cycles - start);
}

u32 neogeo_run_slice(neogeo_t *ng, u32 m68k_cycles_budget) {
    u32 total_done = 0;
    /* getenv() cacheado UMA vez por chamada (não por instrução) — chamar a
     * cada passo do loop de step deixava isso ordens de magnitude mais
     * lento (getenv não é barato), quase parecendo um hang de verdade. */
    static int debug_c1871e_cached = -1;
    static int disable_achado35_cached = -1;
    if (debug_c1871e_cached < 0) debug_c1871e_cached = getenv("NEOGEO_DEBUG_C1871E") ? 1 : 0;
    if (disable_achado35_cached < 0) disable_achado35_cached = getenv("NEOGEO_DISABLE_ACHADO35") ? 1 : 0;

    while (total_done < m68k_cycles_budget) {
        /* Achados #31/#31b: REVERTIDOS (ver docs/ROADMAP.md, Achado #35).
         * Forçar $C11D66->$C11D88 (pular pro "cabine linkada detectada",
         * $C11028) parecia destravar o boot, mas desmontando mais fundo
         * ($C1927E em diante) descobrimos que essa ramificação inteira é
         * OUTRO SUBSISTEMA DE SELF-TEST/DIAGNÓSTICO (um seletor de teste
         * 0-6, não uma tela de crédito nem o jogo) — achado de que
         * "cabine linkada" nesse contexto provavelmente significa "modo de
         * diagnóstico de link", não "pula config, vai pro jogo" como
         * assumimos. O caminho de verdade pro jogo é o original
         * ($C1B4F0→CABINET/SLOT→`$C1B848`, Achado #29) — ver o bypass
         * pragmático do Achado #35 logo abaixo, que ataca isso em vez
         * disso. Mantido aqui comentado só de referência histórica; os
         * dois `if` de fato foram removidos. */
        /* Achado #35: REVERTIDO/DESCARTADO (ver docs/ROADMAP.md). A ideia
         * original era simular um RTS de verdade em `$C1B848` pra "voltar"
         * de `jsr $c1871e` — mas inspecionando o estado real nesse ponto
         * (`NEOGEO_DEBUG_C1871E=1`) achamos que **A7 == A5 == $10F300 e a
         * pilha está genuinamente VAZIA** (tudo zero) nesse momento: o jsr
         * já tinha retornado normalmente ANTES de cairmos no loop (o rts de
         * dentro de `c1871e` já rodou), então "forçar mais um RTS" só
         * pulava pro endereço $000000 (lixo), explicando o loop de reset
         * que víamos. Ou seja: `$C1B848` é um HALT genuíno e proposital,
         * de propósito, sem "destino real" escondido pra achar — não dá
         * pra sair dele simulando retorno de chamada, porque não existe
         * chamador pendente. Isso indica que segurar as 4 direções pra
         * confirmar CABINET/SLOT (Achado #26) provavelmente NÃO é o
         * gatilho real usado por um jogador de verdade — precisa achar
         * outro jeito de sair dessa tela que não passe por esse caminho. */
        if (debug_c1871e_cached) {
            static int dbg_hits = 0;
            if (dbg_hits < 20 &&
                (ng->m68k.pc == 0xC1B842u || ng->m68k.pc == 0xC1B848u)) {
                dbg_hits++;
                u16 s0 = ng->m68k.read16(ng->m68k.user, ng->m68k.a[7]);
                u16 s1 = ng->m68k.read16(ng->m68k.user, ng->m68k.a[7] + 2u);
                u16 s2 = ng->m68k.read16(ng->m68k.user, ng->m68k.a[7] + 4u);
                u16 s3 = ng->m68k.read16(ng->m68k.user, ng->m68k.a[7] + 6u);
                printf("[dbg_c1871e] pc=%06x wram[FDC2]=%02x a0=%06x a7=%06x sr=%04x stack(a7..+6)=%04x %04x %04x %04x d5=%08x\n",
                       ng->m68k.pc, ng->wram[0xFDC2],
                       ng->m68k.a[0], ng->m68k.a[7], ng->m68k.sr,
                       s0, s1, s2, s3, ng->m68k.d[5]);
                fflush(stdout);
            }
        }
        (void)disable_achado35_cached; /* mantido só pra não quebrar NEOGEO_DISABLE_ACHADO35 já documentado; o hack em si foi removido. */
        /* (Achado #32 removido aqui: a simulação de START era um poke direto
         * em `wram[$10FD97]`, o byte JÁ DEBOUNCED pelo próprio jogo. Agora
         * START é simulado no lugar certo — o registrador de hardware
         * REG_STATUS_B bit0, ver `neogeo_status_b()` e Achado #36 — então o
         * código do jogo faz o debounce dele sozinho, como no hardware
         * real.) */

        if (ng->irq_pending) irq_apply_visible(ng); /* Achado #94 */
        ng->m68k.host_time = ng->master_cycles; /* Achado #101 */
        ng->m68k.vpa_sync = 1u;
        u32 done;
        if (ng->m68k_reset_stall) { /* Achado #105 */
            done = ng->m68k_reset_stall;
            ng->m68k_reset_stall = 0;
        } else {
            done = m68k_core_step(&ng->m68k);
            /* Achado #107: CPU parada (STOP esperando IRQ, ou HALT depois de
             * falha dupla) não congela a placa — o clock segue, as IRQs chegam
             * e o watchdog reseta. No MAME o núcleo parado só queima icount.
             * Antes devolvíamos 0 e o tempo emulado simplesmente parava. */
            if (done == 0u && ng->m68k.halted) done = 2u;
        }

        /* Proporção de clock real: 68000 @ 12 MHz, Z80 @ 4 MHz -> 3:1.
         * Achado #87: antes era `Z80_Run(done/3)` a cada instrução do 68000,
         * jogando fora (a) o resto da divisão e (b) o EXCESSO do lote — o
         * `Z80_Run` executa instruções inteiras até passar do alvo e zera o
         * contador na próxima chamada. Com instruções curtas no 68000 (orçamento
         * de 3-5 T-states) e instruções de ~7-11 no Z80, o Z80 andava bem mais
         * rápido que o real: na espera da BIOS pela resposta do comando 1 do
         * som, o MAME dá 544 voltas e nós dávamos 181. Agora o que sobra ou
         * falta fica num crédito (em ciclos do 68000) e é compensado depois. */
        ng->z80_credit += (s64)done;
        {
            s32 z80_budget = (s32)(ng->z80_credit / 3);
            if (z80_budget > 0) {
                int used = Z80_Run(&ng->z80, z80_budget);
                ng->z80_credit -= (s64)used * 3;
            }
        }

        ng->master_cycles += done;
        ng->cycles_since_vblank += done;

        /* Achado #73: watchdog estourou -> reset da placa (sem apagar RAM). É
         * um caminho de execução NORMAL em vários jogos da SNK: o KOF99, por
         * exemplo, cai de propósito num loop de espera longo em $9CDA quando
         * ainda não achou dados válidos na backup RAM, contando que o watchdog
         * resete a placa antes de o código seguir e marcar "proteção falhou"
         * em $D00100. Sem watchdog o jogo trava na tela WARNING pra sempre. */
        if (ng->master_cycles >= ng->watchdog_deadline) {
            ng->watchdog_resets++;
            if (getenv("NEOGEO_TRACE_WATCHDOG")) {
                printf("[watchdog] reset #%u em pc=%06x (master_cycles=%llu)\n",
                       ng->watchdog_resets, ng->m68k.pc,
                       (unsigned long long)ng->master_cycles);
                fflush(stdout);
            }
            reset_common(ng, false);
            {   /* Achado #105: o reset do 68000 leva 34 ciclos no MAME (4 passos
                 * de 2 + leitura de SSP e PC + pré-busca) antes da 1ª instrução. */
                const char *e = getenv("NEOGEO_RESET_CYCLES");
                ng->m68k_reset_stall = e ? (u32)atoi(e) : 34u;
            }
            break; /* devolve o controle: o estado da máquina mudou por completo */
        }

        /* Fase 5: 1 amostra de FM = 144 clocks do YM2610 @8MHz = 216 ciclos
         * do 68000 @12MHz. É daqui que sai a IRQ de timer que faz a musica
         * andar (Achado #56). */
        ng->ym_cycle_accum += done;
        while (ng->ym_cycle_accum >= 216u) {
            ng->ym_cycle_accum -= 216u;
            neogeo_ym2610_generate_sample(ng); /* Achado #88 */
            neogeo_ym2610_timer_tick(ng, 1u);
        }
        if (ng->cycles_since_vblank >= NEOGEO_CYCLES_PER_FRAME) {
            ng->cycles_since_vblank -= NEOGEO_CYCLES_PER_FRAME;
            /* Achado #93: o frame vira no INÍCIO do vblank (linha 240, medido no
             * MAME). As interrupções NÃO saem aqui — cada uma tem seu instante,
             * como os timers do MAME (`start_interrupt_timers`). */
            u64 boundary = ng->master_cycles - ng->cycles_since_vblank;
            if (getenv("NEOGEO_TRACE_VBBOUND")) {
                printf("[vbbound] master_cycles=%llu boundary=%llu vbl_irq_time=%llu\n",
                       (unsigned long long)ng->master_cycles, (unsigned long long)boundary,
                       (unsigned long long)(boundary + 29u));
                fflush(stdout);
            }
            ng->vbl_irq_time = boundary + 29u;     /* NEOGEO_VBLANK_IRQ_HTIM = 58 mclk */
            ng->vbl_reload_time = boundary + 573u; /* NEOGEO_VBLANK_RELOAD_HTIM = 1146 mclk */
            ng->anim_time = boundary + 12288u;     /* NEOGEO_VSSTART = linha 256 */
            if (ng->debug_force_start_frames > 0) ng->debug_force_start_frames--;
            if (ng->debug_force_coin_frames > 0) ng->debug_force_coin_frames--;
            if (ng->debug_force_service_frames > 0) ng->debug_force_service_frames--;
        }
        if (ng->vbl_irq_time && ng->master_cycles >= ng->vbl_irq_time) {
            u64 t = ng->vbl_irq_time;
            ng->vbl_irq_time = 0;
            irq_raise(ng, NEOGEO_IRQACK_VBLANK, t);
        }
        if (ng->vbl_reload_time && ng->master_cycles >= ng->vbl_reload_time) {
            u64 t = ng->vbl_reload_time;
            ng->vbl_reload_time = 0;
            if (ng->display_control & IRQ2CTRL_AUTOLOAD_VBLANK) disp_timer_adjust(ng, t);
        }
        if (ng->anim_time && ng->master_cycles >= ng->anim_time) {
            ng->anim_time = 0;
            /* Achado #45: `auto_animation_timer_callback`, em NEOGEO_VSSTART. */
            if (ng->auto_anim_frame_counter == 0) {
                ng->auto_anim_frame_counter = ng->auto_anim_speed;
                ng->auto_anim_counter++;
            } else {
                ng->auto_anim_frame_counter--;
            }
        }
        while (ng->disp_timer_time && ng->master_cycles >= ng->disp_timer_time) {
            /* Achado #93: `display_position_interrupt_callback` — a IRQ de
             * timer (raster), que simplesmente não existia aqui. */
            u64 t = ng->disp_timer_time;
            ng->disp_timer_time = 0;
            if (ng->display_control & IRQ2CTRL_ENABLE) irq_raise(ng, NEOGEO_IRQACK_TIMER, t);
            if (ng->display_control & IRQ2CTRL_AUTOLOAD_REPEAT) disp_timer_adjust(ng, t);
        }

        /* Pulso do RTC: ver Achado #23, docs/ROADMAP.md — baseado em
         * CICLOS reais (6.000.000 = 0.5s a 12MHz), não em contagem de
         * frames, já que 30 frames != 0.5s exatos (202752*30 !=
         * 6000000). Checado fora do "if" de vblank acima porque não tem
         * por que estar sincronizado com o frame. */
        neogeo_rtc_tick(ng); /* Achado #85 */

        total_done += done;
        if (done == 0) break; /* CPU parada (halt) — evita loop infinito aqui */
    }
    return total_done;
}


/* ---- Persistência da memória com bateria --------------------------------- */

#define NEOGEO_NVRAM_MAGIC   "NGNV"
#define NEOGEO_NVRAM_VERSION 1u

bool neogeo_nvram_load(neogeo_t *ng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    u8 ver[4];
    static u8 bram[NEOGEO_BACKUP_RAM_SIZE];
    u8 sram[sizeof(ng->cart_sram)];
    bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, NEOGEO_NVRAM_MAGIC, 4) == 0 &&
              fread(ver, 1, 4, f) == 4 &&
              (u32)(ver[0] | (ver[1] << 8) | (ver[2] << 16) | ((u32)ver[3] << 24)) == NEOGEO_NVRAM_VERSION &&
              fread(bram, 1, sizeof(bram), f) == sizeof(bram) &&
              fread(sram, 1, sizeof(sram), f) == sizeof(sram);
    fclose(f);
    if (!ok) return false;
    memcpy(ng->backup_ram, bram, sizeof(bram));
    memcpy(ng->cart_sram, sram, sizeof(sram));
    return true;
}

bool neogeo_nvram_save(const neogeo_t *ng, const char *path) {
    /* grava num temporário e renomeia: um crash no meio não corrompe o save */
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    const u8 ver[4] = { NEOGEO_NVRAM_VERSION & 0xFFu, 0, 0, 0 };
    bool ok = fwrite(NEOGEO_NVRAM_MAGIC, 1, 4, f) == 4 && fwrite(ver, 1, 4, f) == 4 &&
              fwrite(ng->backup_ram, 1, NEOGEO_BACKUP_RAM_SIZE, f) == NEOGEO_BACKUP_RAM_SIZE &&
              fwrite(ng->cart_sram, 1, sizeof(ng->cart_sram), f) == sizeof(ng->cart_sram);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}

u32 neogeo_nvram_checksum(const neogeo_t *ng) {
    u32 h = 2166136261u; /* FNV-1a */
    for (u32 i = 0; i < NEOGEO_BACKUP_RAM_SIZE; i++) h = (h ^ ng->backup_ram[i]) * 16777619u;
    for (u32 i = 0; i < sizeof(ng->cart_sram); i++) h = (h ^ ng->cart_sram[i]) * 16777619u;
    return h;
}


/* ---- Save state (Achado #110) --------------------------------------------- */

#define NEOGEO_STATE_MAGIC   "NGST"
#define NEOGEO_STATE_VERSION 1u

/* Assinatura do jogo carregado: FNV-1a do 1º MB da P ROM + tamanhos. */
static u32 state_rom_signature(const neogeo_t *ng) {
    u32 h = 2166136261u;
    u32 n = ng->rom.p_size < 0x100000u ? ng->rom.p_size : 0x100000u;
    for (u32 i = 0; i < n; i++) h = (h ^ ng->rom.p[i]) * 16777619u;
    h = (h ^ ng->rom.p_size) * 16777619u;
    h = (h ^ ng->rom.c_odd_size) * 16777619u;
    return h;
}

static void put_le32(u8 *b, u32 v) { b[0] = (u8)v; b[1] = (u8)(v >> 8); b[2] = (u8)(v >> 16); b[3] = (u8)(v >> 24); }
static u32 get_le32(const u8 *b) { return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24); }

bool neogeo_state_save(const neogeo_t *ng, const char *path) {
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    u8 hdr[16];
    memcpy(hdr, NEOGEO_STATE_MAGIC, 4);
    put_le32(hdr + 4, NEOGEO_STATE_VERSION);
    put_le32(hdr + 8, (u32)sizeof(neogeo_t));
    put_le32(hdr + 12, state_rom_signature(ng));
    bool ok = fwrite(hdr, 1, 16, f) == 16 && fwrite(ng, 1, sizeof(neogeo_t), f) == sizeof(neogeo_t);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
}

bool neogeo_state_load(neogeo_t *ng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 hdr[16];
    bool ok = fread(hdr, 1, 16, f) == 16 && memcmp(hdr, NEOGEO_STATE_MAGIC, 4) == 0 &&
              get_le32(hdr + 4) == NEOGEO_STATE_VERSION &&
              get_le32(hdr + 8) == (u32)sizeof(neogeo_t) &&
              get_le32(hdr + 12) == state_rom_signature(ng);
    neogeo_t *st = NULL;
    if (ok) {
        st = (neogeo_t *)malloc(sizeof(neogeo_t));
        ok = st && fread(st, 1, sizeof(neogeo_t), f) == sizeof(neogeo_t);
    }
    fclose(f);
    if (!ok) { free(st); return false; }

    /* Tudo que é ponteiro vem da instância viva (o endereço das ROMs e da
     * própria estrutura muda entre execuções). */
    st->rom = ng->rom;
    st->m68k.fast_rom = ng->m68k.fast_rom;
    st->m68k.fast_rom_limit = ng->m68k.fast_rom_limit;
    st->m68k.fast_ram = ng->m68k.fast_ram;
    st->m68k.user = ng->m68k.user;
    st->m68k.read8 = ng->m68k.read8;
    st->m68k.read16 = ng->m68k.read16;
    st->m68k.write8 = ng->m68k.write8;
    st->m68k.write16 = ng->m68k.write16;
    st->m68k.reset_instr = ng->m68k.reset_instr;
    st->m68k.irq_ack_fn = ng->m68k.irq_ack_fn;
    st->m68k.addr_err_fn = ng->m68k.addr_err_fn;
    st->z80.fast_ram = ng->z80.fast_ram;
    st->z80.fast_ram_limit = ng->z80.fast_ram_limit;
    st->z80.fast_ram_mask = ng->z80.fast_ram_mask;
    st->z80.ctx = ng->z80.ctx;
    st->z80.read = ng->z80.read;
    st->z80.write = ng->z80.write;
    st->z80.io_read = ng->z80.io_read;
    st->z80.io_write = ng->z80.io_write;
    memcpy(ng, st, sizeof(neogeo_t));
    free(st);
    return true;
}
