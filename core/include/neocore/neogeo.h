#ifndef NEOCORE_NEOGEO_H
#define NEOCORE_NEOGEO_H

#include "types.h"
#include "m68k_core.h"
#include "z80.h"

/* Fase 3 do roadmap: mapa de memória e "cola" do sistema Neo-Geo, unindo os
 * dois cores de CPU portados (m68k_core / z80) com RAM, ROM de cartucho e os
 * registradores de hardware. Vídeo (LSPC2) e som (YM2610) ainda são stubs
 * nesta fase — só o suficiente pra CPU rodar e não travar em acesso a
 * registro desconhecido. Ver docs/ROADMAP.md e docs/ARCHITECTURE.md.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NEOGEO_BIOS_SIZE   (128u * 1024u)          /* 0x020000 */
#define NEOGEO_WRAM_SIZE   (64u * 1024u)           /* 0x010000 */
#define NEOGEO_BACKUP_RAM_SIZE (64u * 1024u)       /* 0xD00000-0xD0FFFF, bateria (persiste entre boots) */
#define NEOGEO_MEMCARD_SIZE (2u * 1024u)           /* 0x800000-0x800FFF, memory card 2KB/8-bit (confirmado: consolemods.org wiki) */
#define NEOGEO_CART_P_MAX  (8u * 1024u * 1024u)    /* maior P ROM combinado suportado por ora */
#define NEOGEO_SOUND_RAM_SIZE (2u * 1024u)         /* RAM do Z80 (0x0000-0x07FF, espelhada) */
/* M1 (programa do Z80). Achado #74: 128KB era pequeno demais — 21 dos 170
 * romsets aqui têm M1 de 256KB (bstars, garou, kof98, rbff2...) ou 512KB
 * (mslug3, kof2003...) e nem carregavam. O teto real é 512KB. */
#define NEOGEO_CART_M_MAX  (512u * 1024u)
#define NEOGEO_CART_V_MAX  (32u * 1024u * 1024u)   /* dados ADPCM combinados (V1-V4); alguns jogos (ex: Neo Bomberman, V1+V2=6MB) passam de 4MB */
/* Achado #49: um BANCO de paleta = 256 paletas x 16 cores = 4096 words. O
 * hardware tem DOIS bancos, selecionados pelo bit 7 do latch de sistema
 * ($3A0000) — confirmado em `m_paletteram.resize(0x1000 * 2)` e
 * `set_palette_bank()` (`src/mame/snk/neogeo_v.cpp`). A janela
 * $400000-$401FFF sempre mostra o banco ATIVO. */
#define NEOGEO_PALETTE_WORDS 4096u                 /* por BANCO */
#define NEOGEO_PALETTE_BANKS 2u
#define NEOGEO_VRAM_WORDS  0x8800u                 /* ~68KiB de VRAM do LSPC2, endereçável por word */
#define NEOGEO_SCREEN_W 320u
#define NEOGEO_SCREEN_H 224u
/* Linhas de vblank ANTES do início da área visível (NEOGEO_VBEND no MAME,
 * `src/mame/snk/neogeo_spr.h`: 0x010 = 16; a área visível vai da linha crua
 * 16 até a 239, = 224 linhas). O LSPC trabalha em linhas de varredura CRUAS,
 * então tanto a posição Y dos sprites (Achado #44) quanto a linha do tilemap
 * da fix layer (Achado #47) precisam descontar isso pra virar coordenada da
 * nossa tela de 0-223. */
#define NEOGEO_VBEND_LINES 16u

/* Endereços de registrador confirmados contra a NeoGeo Development Wiki
 * (wiki.neogeodev.org) em 2026-09-10 — ver docs/ARCHITECTURE.md pras fontes.
 * Diferente da Fase 3 original, estes não são mais palpites. */
enum {
    NEOGEO_REG_SOUND    = 0x320000, /* 68k<->Z80: escreve comando (dispara NMI no Z80), le resposta */
    NEOGEO_REG_VRAMADDR = 0x3C0000,
    NEOGEO_REG_VRAMRW   = 0x3C0002,
    NEOGEO_REG_VRAMMOD  = 0x3C0004,
    NEOGEO_REG_LSPCMODE = 0x3C0006,
    NEOGEO_REG_TIMERHIGH= 0x3C0008,
    NEOGEO_REG_TIMERLOW = 0x3C000A,
    NEOGEO_REG_IRQACK   = 0x3C000C,
    NEOGEO_REG_TIMERSTOP= 0x3C000E
};

enum {
    NEOGEO_IRQACK_RESET = 0x01,
    NEOGEO_IRQACK_TIMER = 0x02,
    NEOGEO_IRQACK_VBLANK = 0x04
};

/* Um "romset" carregado (ver core/src/system/rom_loader.c). Nomeação segue a
 * convenção MAME/FBNeo: p=programa(68k), s=fix/texto, m=sound ROM fixa
 * (Z80), v=samples ADPCM, c=tiles/sprites (não usado ainda na Fase 3, fica
 * pronto pra Fase 4 de vídeo). Buffers são alocados pelo loader; tamanho
 * real pode ser menor que o MAX acima. */
typedef struct neogeo_romset {
    u8 *bios;      u32 bios_size;   /* BIOS do sistema 68k (não faz parte do cartucho) */
    /* "Audio BIOS" (sm1.sm1): driver de som compartilhado do Z80, análogo à
     * BIOS do 68k — fixo em 0x0000-0x7FFF no espaço de endereço do Z80,
     * igual pra todo cartucho. A M1 do cartucho entra via bankswitch (ver
     * comentário em core/src/system/neogeo.c). Achado comparando trace-a-
     * trace contra o MAME: sem isso, o Z80 executava a M1 crua no lugar
     * errado e nunca respondia ao 68k. */
    u8 *audiobios; u32 audiobios_size;
    /* SFIX (sfix.sfix): ROM de fix/texto PRÓPRIA da BIOS, análoga à
     * audiobios acima — fixa, igual pra todo cartucho, usada pela BIOS pra
     * desenhar suas próprias telas (self-test, memory card, CABINET/SLOT
     * etc). Achado #27 (ver docs/ROADMAP.md): faltava carregar isso por
     * completo — a fix layer sempre lia a S ROM do CARTUCHO (`s` abaixo),
     * mesmo em telas desenhadas pela BIOS, dando glifos corrompidos (dados
     * de gráfico do jogo interpretados como fonte) sempre que a BIOS usava
     * códigos de tile fora da faixa ASCII básica que por convenção costuma
     * se repetir em ambas as ROMs. */
    u8 *sfix;      u32 sfix_size;
    /* Achado #50: "000-lo.lo" — a ROM de lookup do ZOOM VERTICAL de sprite
     * (região "spritegen:zoomy" no MAME). Indexada por
     * `zoomy[(zoom_y << 8) | zoom_line]`, devolve num byte qual linha de
     * origem usar: nibble baixo = linha dentro do tile (0-15), nibble alto =
     * qual tile da coluna do sprite (0-31). É a curva NÃO-LINEAR real do
     * chip — antes disso usávamos uma aproximação linear. */
    u8 *zoomy;     u32 zoomy_size;
    u8 *p;         u32 p_size;      /* P ROM(s) concatenadas (programa 68k) */
    u8 *s;         u32 s_size;      /* S ROM (fix/texto, LSPC2) do CARTUCHO */
    u8 *m;         u32 m_size;      /* M1 (sound ROM fixa do Z80) */
    u8 *v;         u32 v_size;      /* V ROMs do ADPCM-A (v1..v4 ou v11..v14) */
    u8 *vb;        u32 vb_size;     /* V ROMs do ADPCM-B (v21..v24), barramento separado */
    /* C ROMs de sprite: NÃO são uma concatenação simples. Vêm em pares
     * (c1/c2, c3/c4, ...) onde o chip ímpar guarda os bitplanes 0-1 de cada
     * tile e o par (chip seguinte) guarda os bitplanes 2-3 — ver
     * core/src/video/lspc_sprites.c. Concatenamos todos os ímpares numa
     * "malha" e todos os pares na outra. */
    u8 *c_odd;     u32 c_odd_size;
    u8 *c_even;    u32 c_even_size;

    /* Achado #72: cartucho com chip SMA+CMC42 (King of Fighters '99) —
     * detectado no carregador (prefixo "251" + arquivo "*.neo-sma"), ANTES
     * do P ROM ser descriptografado (por isso não dá pra usar o NGH do
     * cabeçalho aqui, ele só existe DEPOIS da descriptografia). */
    bool is_kof99;
    /* Achado #79: tipo de chip SMA (NEOGEO_SMA_*), generalizando o KOF99.
     * `fix_bank_type` (0 normal, 1 garou/mslug3, 2 kof2000) diz como a fix
     * layer desses cartuchos — que é de 512KB, não 128KB — escolhe o banco. */
    u8 sma_type;
    u8 fix_bank_type;
    /* Achado #82: KOF98 MVS (criptografia própria + overlay em $000100). */
    bool is_kof98;
    /* Achado #83: cartucho NEO-PVC (mslug5, svc, kof2003) — 8KB de RAM de
     * proteção sobre $2FE000-$2FFFFF, que também faz o bankswitch. */
    bool is_pvc;
    /* Achado #83: 8KB de RAM do cartucho em $200000 (jockeygp). */
    bool has_cart_ram;
} neogeo_romset_t;

enum {
    NEOGEO_SMA_NONE = 0,
    NEOGEO_SMA_KOF99,
    NEOGEO_SMA_GAROU,
    NEOGEO_SMA_MSLUG3,
    NEOGEO_SMA_KOF2000,
};

/* Taxa de saída nativa do YM2610: 8 MHz / 144 ≈ 55555 Hz (o ADPCM-A roda a
 * 1/3 disso, 8e6/432 ≈ 18518 Hz). Geramos nesta taxa e deixamos o frontend
 * reamostrar pra taxa da placa de som. */
#define NEOGEO_AUDIO_RATE 55555u
#define NEOGEO_AUDIO_RING 8192u /* potência de 2; ~0,15 s (o frontend esvazia a cada frame) */

/* Um canal de ADPCM-A (6 no chip). Ver core/src/audio/ym2610.c. */
typedef struct {
    u32 cur_address;  /* endereço atual na V ROM, em bytes */
    u8  cur_byte;     /* byte lido (2 nibbles = 2 amostras) */
    u8  cur_nibble;   /* 0 = próximo é o nibble alto, 1 = o baixo */
    u8  playing;
    u8  step_index;   /* índice na tabela de passos (0-48) */
    s16 accumulator;  /* acumulador de 12 bits (dá a volta) */
} ym_adpcm_a_ch_t;

/* Um operador FM (são 4 por canal). O YM2610 tem 4 canais FM de 4
 * operadores cada. Campos "cache" são derivados dos registradores e
 * recalculados uma vez por amostra (`prepare`), não a cada pixel de som —
 * é assim que o ymfm faz e é o que torna viável em tempo real. */
typedef struct {
    u32 phase;          /* acumulador de fase 10.10 */
    u16 env_att;        /* atenuação do envelope, 10 bits (0 = som máximo) */
    u8  env_state;      /* 0=attack 1=decay 2=sustain 3=release */
    u8  key_state;      /* estado da tecla já processado */
    u8  keyon_live;     /* tecla pedida pelo registrador $28 */
    u8  ssg_inverted;

    /* cache */
    u32 block_freq;
    s32 detune;
    u32 multiple;       /* x.1 (0 vale 0.5) */
    u32 phase_step;     /* 0xFFFFFFFF = recalcular (PM ativo) */
    u32 total_level;
    u32 eg_sustain;
    u8  eg_rate[4];
} ym_fm_op_t;

typedef struct {
    ym_fm_op_t op[4];   /* na ordem "natural": op1, op2(mod), op3, op4 */
    s32 feedback[2];
    s32 feedback_in;
} ym_fm_ch_t;

/* Estado do YM2610: ADPCM-A + FM (4 canais) + timers. SSG e ADPCM-B ainda
 * não sintetizados (registradores guardados). */
typedef struct {
    u8 regs_a[256];
    u8 regs_b[256];
    ym_adpcm_a_ch_t adpcm_a[6];

    /* FM: 4 canais. Índices 0,1 = canais 1,2 do banco A ($30-$B6 com offset
     * 1 e 2); índices 2,3 = canais 1,2 do banco B. É por isso que o
     * ym2610 do ymfm usa channel_mask 0x36 (bits 1,2,4,5): o chip tem os
     * 6 slots do OPNA mas só 4 estão ligados. */
    ym_fm_ch_t fm[4];
    u32 env_counter;    /* contador x.2 de envelope (ver clock_fm) */
    u32 lfo_counter;
    u8  lfo_am;
    /* Achado #99: `fm_engine_base::clock` só roda `prepare` (cache, teclas,
     * máscara de canais ativos) se houve escrita FM (exceto $27) ou a cada
     * 4096 amostras; canal inativo não soma saída nem atualiza realimentação. */
    u8  fm_modified;
    u8  fm_active_mask;
    u32 fm_prepare_count;

    /* SSG (a parte "YM2149" do chip): 3 canais de onda quadrada + ruído +
     * um envelope compartilhado. Registradores $00-$0D do banco A. */
    u16 ssg_tone_count[3];
    u8  ssg_tone_state[3];
    u32 ssg_noise_count;
    u32 ssg_noise_state;
    u32 ssg_env_count;
    u32 ssg_env_state;
    u32 ssg_sampindex;  /* alterna a fase do reamostrador 9:2 */
    s32 ssg_last[3];    /* última saída de cada canal, pro reamostrador */

    /* ADPCM-B ("delta-T"): um canal só, com taxa de reprodução variável
     * (delta-N) e interpolação linear. Registradores $10-$1C do banco A. */
    u8  bstatus;        /* bit0=EOS, bit1=BRDY, bit2=tocando */
    u8  bnibble;
    u8  bbyte;
    u8  bdummy_read;
    u16 bposition;      /* acumulador fracionário 0.16 do delta-N */
    u32 bcuraddress;
    s32 baccumulator;
    s32 bprev_accum;
    s32 bstep;
    u8 adpcm_a_divider; /* conta 3 amostras de saída por passo do ADPCM-A */

    /* Timers A e B. É o que faz a MÚSICA andar: o driver de som programa um
     * timer e avança a sequência a cada IRQ dele. Sem isso o driver
     * inicializa, configura os instrumentos e para — foi exatamente o que
     * víamos (Achado #56). Períodos, em amostras de FM:
     *   A: 1024 - ((reg$24 << 2) | (reg$25 & 3))
     *   B: 16 * (256 - reg$26)
     * Registrador $27: bit0/1 = liga A/B, bit2/3 = habilita IRQ de A/B,
     * bit4/5 = limpa o flag de A/B. O status (portas $04/$06) devolve os
     * flags em bit0 (A) e bit1 (B). */
    s32 timer_a_count;
    s32 timer_b_count;
    u8  timer_flags;   /* bit0 = estourou A, bit1 = estourou B */
    /* Achado #86: `m_eos_status`/`m_flag_mask` do ymfm — bits 0-5 = canal de
     * ADPCM-A chegou ao fim, bit 7 = ADPCM-B, bit 6 = cópia interna "ao vivo"
     * do EOS do ADPCM-B (escondida de quem lê). */
    u8  eos_status;
    u8  eos_flag_mask;
    u8  irq_line;      /* estado atual da linha de IRQ pro Z80 */
} ym2610_t;

/* Libera os buffers de um romset (não libera a struct em si). */
void neogeo_romset_free(neogeo_romset_t *rs);

/* Carrega um romset a partir de um diretório contendo os arquivos padrão
 * (bios.rom ou neogeo.zip-extraído; p1.p1/p1.bin, s1.s1/s1.bin, m1.m1/m1.bin,
 * v1.v1..v4.v4, c1.c1..c8.c8 — variações de extensão aceitas, ver
 * rom_loader.c). Retorna false em erro (arquivo faltando, alocação falhou);
 * `out_err` (opcional) recebe uma mensagem curta descrevendo o problema. */
bool neogeo_romset_load_dir(neogeo_romset_t *rs, const char *dir_path,
                             char *out_err, u32 out_err_size);

/* Como neogeo_romset_load_dir, mas com o diretório da BIOS separado do
 * diretório do cartucho — reflete a realidade (BIOS compartilhada entre
 * jogos). Passe `bios_dir=NULL` pra usar `cart_dir` pros dois (mesmo
 * comportamento de neogeo_romset_load_dir). */
bool neogeo_romset_load_dir_ex(neogeo_romset_t *rs, const char *cart_dir,
                                const char *bios_dir,
                                char *out_err, u32 out_err_size);

typedef struct neogeo {
    M68kCore m68k;
    Z80 z80;

    neogeo_romset_t rom;

    u8 wram[NEOGEO_WRAM_SIZE];
    /* Backup RAM (0xD00000-0xD0FFFF): mantida entre boots no hardware real
     * (bateria). A BIOS procura a assinatura "BACKUP RAM OK !" em $D00010 —
     * sem essa RAM implementada, a checagem falha sempre e o boot toma um
     * caminho diferente do hardware real (achado comparando trace-a-trace
     * contra o MAME, ver docs/ROADMAP.md). NÃO persiste entre execuções do
     * nosso emulador ainda (zerada a cada `neogeo_reset`) — TODO: salvar em
     * arquivo se quisermos fidelidade completa (config/high-scores etc). */
    u8 backup_ram[NEOGEO_BACKUP_RAM_SIZE];

    /* Memory card (0x800000-0x800FFF, 2KB/8-bit). A BIOS testa essa RAM no
     * boot (escreve/lê um padrão) — sem ela implementada, o teste falhava
     * sempre e o boot desviava pro handler de erro genérico (achado
     * comparando trace-a-trace contra o MAME, ver docs/ROADMAP.md). Não
     * simula um cartão real inserido/removido, só a RAM física. */
    u8 memcard[NEOGEO_MEMCARD_SIZE];

    u8 sound_ram[NEOGEO_SOUND_RAM_SIZE];
    u16 palette[NEOGEO_PALETTE_WORDS * NEOGEO_PALETTE_BANKS];
    /* Achado #49: deslocamento do banco ativo (0 ou NEOGEO_PALETTE_WORDS),
     * trocado pelo bit 7 do latch de sistema. */
    u32 palette_bank;
    /* "screen shadow" (bit0 do latch de sistema): escurece a tela inteira
     * trocando a tabela de conversão do DAC (ver neogeo_color_to_rgb888_ex). */
    u8  screen_shadow;

    /* LSPC2: VRAM interna do chip de vídeo (não mapeada direto no espaço do
     * 68000 — só acessível indiretamente via VRAMADDR/VRAMRW/VRAMMOD, como
     * no hardware real). Ver docs/ARCHITECTURE.md Fase 4. */
    u16 vram[NEOGEO_VRAM_WORDS];
    u16 vram_addr;
    s16 vram_mod;
    u16 lspcmode;
    /* Auto-animação de sprite (Achado #45): o LSPC troca sozinho os bits
     * baixos do código do tile por este contador, pra animar fogo/água/luzes
     * sem o 68000 precisar mexer em nada. `speed` e `disabled` vêm de
     * LSPCMODE (bits 15-8 e bit 3); o contador anda 1x a cada `speed+1`
     * frames (ver `auto_animation_timer_callback` no MAME). */
    u8  auto_anim_counter;
    u8  auto_anim_frame_counter;
    u8  auto_anim_speed;
    u8  auto_anim_disabled;

    /* Fase 5 — YM2610. O chip tem DOIS bancos de registrador, escritos por
     * pares de porta do Z80 ($04/$05 = banco A, $06/$07 = banco B): escreve
     * o número do registrador na porta de endereço, depois o valor na de
     * dado. Aqui só guardamos o estado (sem síntese ainda), o que já serve
     * pra inspecionar o que o driver de som do cartucho (M1) realmente faz.
     * Mapa (resumo): banco A $00-$0F = SSG, $10-$1C = ADPCM-B, $24-$27 =
     * timers, $28 = key on/off, $30-$B6 = FM ch1-2; banco B $00-$0D =
     * ADPCM-A (6 canais), $30-$B6 = FM ch3-4. */
    /* Fase 5: a NMI de som do Z80 pode ser ligada/desligada pelo próprio
     * driver (portas $08/$18). Começa LIGADA: o driver desliga durante o
     * processamento e religa quando está pronto. */
    u8  z80_nmi_enabled;

    u32 ym_cycle_accum;
    /* Achado #88: buffer circular de áudio gerado no tempo emulado. */
    s16 audio_ring[NEOGEO_AUDIO_RING * 2];
    u32 audio_rd, audio_wr;
    s16 audio_last_l, audio_last_r;
    u8  ym_mute_fm, ym_mute_adpcm, ym_mute_ssg;
    u8  ym_addr[2];
    /* Achado #86: latch de endereço de 9 bits do chip e fim do estado
     * "ocupado" (em ciclos do 68000). */
    u16 ym_address9;
    u64 ym_busy_end;
    /* Achado #87: ciclos do 68000 devidos (ou adiantados) ao Z80. */
    s64 z80_credit;
    u8  z80_nmi_line; /* Achado #89: saída do INPUT_MERGER (pendente E habilitada) */
    u32 ym_writes;
    ym2610_t ym;
    u32 timer_reload;   /* TIMERHIGH:TIMERLOW combinados */
    u16 timerstop;
    u8  irq_pending;    /* bits: ver NEOGEO_IRQACK_* — pendências antes do ack */
    u32 cycles_since_vblank;
    /* Achado #93: eventos de vídeo/interrupção do MAME, em ciclos do 68000
     * (0 = nenhum agendado). Ver neogeo.c. */
    u64 vbl_irq_time;       /* VBSTART + NEOGEO_VBLANK_IRQ_HTIM (29 ciclos) */
    u64 vbl_reload_time;    /* VBSTART + NEOGEO_VBLANK_RELOAD_HTIM (573 ciclos) */
    u64 anim_time;          /* NEOGEO_VSSTART (linha 256) */
    u64 disp_timer_time;    /* IRQ2 (display position / timer de raster) */
    u32 display_counter;
    u8  display_control;    /* bits 4-7 de LSPCMODE */
    u16 vram_read_buffer;
    /* Achado #94: instante (ciclos do 68000) em que cada fonte de IRQ foi
     * ligada: [0] IRQ3/reset, [1] timer, [2] vblank. */
    u64 irq_time[3];

    /* Pulso do RTC (bit6 de REG_STATUS_A): no hardware real é uma onda
     * quadrada de 1Hz vinda do chip de relógio — um oscilador PRÓPRIO,
     * independente do clock de vídeo. A BIOS conta vblanks entre bordas
     * desse pulso pra validar o timing — sem ele NUNCA mudar, ela trava
     * esperando uma borda que nunca vem (achado depurando o boot real,
     * ver docs/ROADMAP.md). Achado #23: tínhamos alternado a cada 30
     * VBLANKS (contagem de frame), mas isso NÃO é a mesma coisa que 0.5s
     * reais — 30 frames a 202752 ciclos cada = 6.082.560 ciclos, enquanto
     * 0.5s a 12MHz = exatamente 6.000.000 ciclos. Essa diferença
     * acumulava e por volta da 5ª troca (comparando WRAM completa contra
     * o MAME, achado #23) já tinha desviado o bastante pra causar uma
     * divergência real de 1 frame. Trocado pra contar CICLOS DE VERDADE
     * (`rtc_next_toggle_cycles`, comparado contra `master_cycles`), não
     * frames — não é um RTC de verdade (hora/data), só o pulso de 1Hz. */
    u8 rtc_pulse;            /* pino TP (STATUS_A bit 6) */
    u8 rtc_stb;              /* último estado do pino STB do RTC (Achado #65) */
    u8 vblank_count_for_rtc; /* só usado pela ferramenta antiga trace_boot.c */
    /* Achado #85: µPD4990A completo — ver core/src/system/upd4990a.c. */
    u8  rtc_shift[7];        /* 6 bytes de hora/data + registrador de comando */
    u8  rtc_time[6];         /* seg, min, hora, dia (BCD), mês<<4|dia_semana, ano (BCD) */
    u8  rtc_cmd, rtc_clk, rtc_data_in, rtc_data_out, rtc_testmode, rtc_clock_enabled;
    u64 rtc_sec_next;
    u64 rtc_tp_half, rtc_tp_next;   /* 0 = timer do TP parado */
    u64 rtc_do_half, rtc_do_next;   /* 0 = timer do DATA_OUT parado */

    /* Bankswitch do P ROM: qual banco de 1MB de `rom.p` está mapeado em
     * 0x200000-0x2FFFFF quando o P ROM é maior que 1MB. Esquema "padrão";
     * cartuchos com chip de proteção próprio (muito comuns no Neo-Geo) vão
     * precisar de tratamento especial mais adiante — não implementado
     * ainda (ver TODO em memmap.c). */
    u32 p_bank_offset;

    /* Achado #69: chip de proteção do cartucho (ALTERA) do Metal Slug X,
     * janela $2FFFE0-$2FFFEF. Ligado quando o NGH do cabeçalho da P ROM
     * ($000108, BCD) é 0250. */
    u8  prot_mslugx;
    u32 prot_counter;
    u16 prot_command;

    /* Achado #70: SRAM com bateria no cartucho ($200000-$201FFF), usada
     * pelo V-Liner (NGH 03E7). */
    u8  cart_sram_enabled;
    u8  cart_sram[8192];

    /* Achado #72: chip SMA (King of Fighters '99). Bankswitch usa uma
     * tabela de lookup própria (não a fórmula `(banco+1)*1MB` padrão) e o
     * jogo consulta um gerador de "número aleatório" (na real, um LFSR
     * bem simples) como parte da checagem anti-pirataria. */
    u8  prot_kof99;
    u16 sma_rng;
    /* Achado #79: SMA genérico — tipo e endereços dos registradores. */
    u8  sma_type;
    u32 sma_bank_addr, sma_rand_addr1, sma_rand_addr2;
    /* Achado #82: KOF98 — estado do overlay (0 normal, 1 "00C2 00FD",
     * 2 "NEO-") e as 2 words originais de $000100/$000102. */
    u8  prot_kof98;
    u8  kof98_state;
    u16 kof98_default[2];
    /* Achado #83: RAM do chip NEO-PVC (0x1000 words, `m_cart_ram` no MAME). */
    u8  prot_pvc;
    u16 pvc_ram[0x1000];

    /* Bankswitch das 4 janelas do Z80 (índices: 0=$F000-$F7FF/2KB,
     * 1=$E000-$EFFF/4KB, 2=$C000-$DFFF/8KB, 3=$8000-$BFFF/16KB), cada uma
     * guardando o offset (banco*tamanho_da_janela) dentro da M1 do
     * cartucho. Selecionado via portas de I/O $08-$0B (ver z80_io_read_cb).
     * Confirmado contra a NeoGeo Development Wiki ("Z80 port map"). */
    u32 z80_bank_offset[4];
    /* Achado #74: máscara de endereço das janelas de banco do Z80, calculada
     * igual ao `audio_cpu_banking_init` do MAME. */
    u32 z80_bank_mask;

    /* Latch de comunicação 68000 <-> Z80 (REG_SOUND @ 0x320000). Escrita do
     * 68k dispara NMI no Z80 (confirmado na neogeodev wiki); Z80 lê/confirma
     * via porta de I/O $00. TODO: portas $08/$18 (habilita/desabilita NMI de
     * som) ainda não implementadas — NMI sempre habilitado por ora. */
    u8 sound_latch_to_z80;
    u32 sound_latch_to_z80_write_count; /* diagnostico temporario, Achado #25 */
    u8 sound_latch_to_68k;   /* Z80 escreve aqui (porta de I/O $00), 68k lê */
    bool sound_latch_pending;

    /* REG_P1CNT ($300000, byte alto): estado dos botões/direções do P1,
     * ativo-baixo (bit=0 significa pressionado). Sem nenhum frontend de
     * input real ligado ainda (Fase 6), o valor default é 0xFF (nada
     * pressionado); ferramentas de depuração/frontend podem escrever aqui
     * pra simular um botão sendo apertado (ex.: destravar telas de
     * self-test da BIOS que esperam A+B+C simultâneos — ver Achado #19,
     * docs/ROADMAP.md). Bits (confirmados na wiki): 0=Up,1=Down,2=Left,
     * 3=Right,4=A,5=B,6=C,7=D. */
    u8 p1_buttons;
    /* Entradas do conector JAMMA (MVS), todas ativo-baixo, 0xFF = nada
     * apertado. Mapa do MAME (`neogeo.cpp`, portas P2/SYSTEM/AUDIO_COIN):
     *   p2_buttons  ($340000) mesmo layout do P1
     *   sys_buttons ($380000) bit0 START1, bit1 SELECT1, bit2 START2, bit3 SELECT2
     *   coin_buttons($320001) bit0 COIN1, bit1 COIN2, bit2 SERVICE, bit3 COIN3, bit4 COIN4
     * O frontend escreve aqui a cada quadro. */
    u8 p2_buttons;
    u8 sys_buttons;
    u8 coin_buttons;

    /* Achado #27 (ver docs/ROADMAP.md): registrador de controle do sistema
     * ($3A0000-$3A001F, latch HC259 real — cada escrita seta/limpa 1 dos 8
     * bits, escolhido por bits 1-3 do offset de endereço, valor pelo bit 3).
     * bit5 (`set_use_cart_audio` no MAME real) controla JUNTO o banco de ROM
     * de áudio do Z80 E a fonte da fix layer: 0=BIOS (SFIX/audio BIOS),
     * 1=cartucho (S1/M1). Default 0 no reset (confirmado no MAME:
     * `m_use_cart_audio = 0` na declaração do member em neogeo.h) — a BIOS
     * troca pra 1 em algum ponto do boot depois de validar o cartucho.
     * bit1 (`use_cart_vectors`) existe no hardware real mas ainda não afeta
     * nosso mapa de memória (`is_bios_addr` é fixo) — TODO futuro. */
    u8 use_cart_fix;
    u8 save_ram_unlocked; /* Achado #84: bit 6 do latch de sistema */

    /* Achado #40 (ver docs/ROADMAP.md): bit1 do mesmo latch de sistema
     * ($3A0002 = limpa, $3A0012 = seta). Controla de onde vem a TABELA DE
     * VETORES em $000000-$00007F: 0 = BIOS (padrão no reset, é assim que a
     * CPU busca SSP/PC iniciais), 1 = CARTUCHO. A BIOS seta esse bit
     * (`$C11FAA: move.b D0,$3a0013`) pouco antes de entregar o controle pro
     * jogo, justamente pra que as interrupções (vblank etc) passem a usar os
     * handlers DO JOGO. Sem isso o jogo nunca roda: a ISR continua sendo a
     * da BIOS. */
    u8 use_cart_vectors;

    /* Achado #32 (ver docs/ROADMAP.md): simulação de START/moeda — como o
     * Neo-Geo não tem START/COIN no meio de P1CNT (só direções+A/B/C/D),
     * e o jogo lê START via um byte de estado debounced em WRAM
     * (`wram[0xFD97]` bit4, offset `(a97,A5)` com A5=$10F300) em vez de um
     * registrador de hardware direto, simular isso precisa de um HOOK
     * disparado no PC exato da leitura (`$C190E2`), não só uma janela de
     * vblank — daí esses campos, checados a cada instrução dentro de
     * `neogeo_run_slice` (não é um registrador real, é só o jeito de
     * ferramentas de debug/frontend pedirem "simula start agora" de forma
     * confiável). `debug_force_start_frames`: quantos vblanks ainda faltam
     * pra manter o "botão" simulado ligado (decrementado a cada vblank
     * completo); zero = desligado. */
    u32 debug_force_start_frames;
    /* Achado #34: mesma ideia, mas pra COIN1 (moeda) — ver
     * `neogeo_status_a()` em neogeo.c. */
    u32 debug_force_coin_frames;
    /* SERVICE (bit2 de STATUS_A, Achado #36) — mesmo padrão. */
    u32 debug_force_service_frames;

    u64 master_cycles;

    /* Achado #73: watchdog. `watchdog_deadline` é o valor de `master_cycles`
     * em que a placa reseta se ninguém escrever em $300001 antes. Contador de
     * resets só pra depuração/testes. */
    u64 watchdog_deadline;
    u32 watchdog_resets;
    /* Achado #105: ciclos do microcódigo de reset do 68000 ainda a consumir
     * antes da 1ª instrução (34 no MAME, `state_reset_if`). */
    u32 m68k_reset_stall;
} neogeo_t;

/* Inicializa o sistema com um romset já carregado (a struct `rom` é copiada
 * por valor — os buffers continuam de propriedade de quem chamou
 * neogeo_romset_load_dir, o `neogeo_t` não faz free deles). */
void neogeo_init(neogeo_t *ng, const neogeo_romset_t *rom);

/* Reset do sistema: reseta 68000 e Z80, zera RAM, latches, seleciona o
 * primeiro banco de P ROM. */
void neogeo_reset(neogeo_t *ng);

/* Memória com bateria (backup RAM da placa MVS + SRAM de cartucho, V-Liner/
 * Jockey GP) em arquivo. Formato: "NGNV" + versão (u32 LE) + backup RAM
 * (64KB, na ordem do espaço do 68000) + SRAM do cartucho (8KB).
 * load devolve false se o arquivo não existe ou é inválido (estado intacto). */
bool neogeo_nvram_load(neogeo_t *ng, const char *path);
bool neogeo_nvram_save(const neogeo_t *ng, const char *path);
/* Soma simples do conteúdo persistente, pra detectar mudança e só gravar
 * quando precisa. */
u32 neogeo_nvram_checksum(const neogeo_t *ng);

/* Save state (Achado #110): o neogeo_t inteiro — CPUs, chips, RAMs, fase de
 * vídeo e som — num arquivo. Formato: "NGST" + versão + sizeof(neogeo_t) +
 * assinatura da ROM + bytes da estrutura. Ao carregar, ROMs e ponteiros
 * (callbacks e atalhos de memória das CPUs) vêm da instância viva; o arquivo
 * só é aceito se for do mesmo jogo e da mesma versão do emulador. */
bool neogeo_state_save(const neogeo_t *ng, const char *path);
bool neogeo_state_load(neogeo_t *ng, const char *path);

/* Achado #85: relógio µPD4990A. */
void neogeo_rtc_init(neogeo_t *ng);
void neogeo_rtc_write(neogeo_t *ng, u8 val);
void neogeo_rtc_tick(neogeo_t *ng);

/* Achado #74: bankswitch de áudio do Z80, igual ao `audio_cpu_banking_init`
 * do MAME. `neogeo_z80_banking_init` calcula a máscara a partir do tamanho da
 * M1 e põe os bancos iniciais; `neogeo_z80_set_bank` troca uma janela
 * (region 0=$F000/2K, 1=$E000/4K, 2=$C000/8K, 3=$8000/16K). */
void neogeo_z80_banking_init(neogeo_t *ng);
void neogeo_z80_set_bank(neogeo_t *ng, u32 region, u8 bank);

/* Avança o sistema em um quantum pequeno de ciclos de clock do 68000,
 * intercalando com o Z80 na proporção correta de clock
 * (68000 @ 12MHz : Z80 @ 4MHz = 3:1). Retorna ciclos de 68000 efetivamente
 * avançados. Não faz nada de vídeo ainda (Fase 4) — quem chama por ora só
 * valida que as duas CPUs rodam sem travar contra o mapa de memória real. */
u32 neogeo_run_slice(neogeo_t *ng, u32 m68k_cycles_budget);

/* Roda até a próxima borda de vblank (Achado #54). É ESTE que quem desenha
 * deve usar: renderizar logo depois desta chamada pega a VRAM/paleta no
 * mesmo estado que o hardware real acabou de mostrar na tela, em vez de um
 * ponto arbitrário no meio da rotina de atualização do jogo (que produzia
 * frames "rasgados" com paleta nova + tiles velhos). */
u32 neogeo_run_until_vblank(neogeo_t *ng);

/* ---- Áudio (YM2610, Fase 5) ----------------------------------------------
 * `neogeo_audio_render` gera `num_samples` quadros ESTÉREO intercalados
 * (L,R) em 16 bits com sinal, na taxa NEOGEO_AUDIO_RATE. Só a parte
 * ADPCM-A está implementada por ora (percussão/vozes/efeitos); FM e SSG
 * ainda não geram som. */
void neogeo_audio_render(neogeo_t *ng, s16 *out, u32 num_samples);
/* Achado #88: gera 1 amostra no tempo emulado (chamado pelo laço das CPUs a
 * cada 216 ciclos do 68000) e quantas amostras há prontas no buffer. */
void neogeo_ym2610_generate_sample(neogeo_t *ng);
u32 neogeo_audio_available(const neogeo_t *ng);
void neogeo_ym2610_write_reg(neogeo_t *ng, u32 bank, u8 reg, u8 val);
void neogeo_ym2610_reset(neogeo_t *ng);
/* Avança os timers do YM2610 em `fm_samples` amostras (1 amostra de FM =
 * 216 ciclos do 68000). Chamado pelo laço de CPU: é o que gera a IRQ que
 * faz o driver de som avançar a música. */
void neogeo_ym2610_timer_tick(neogeo_t *ng, u32 fm_samples);
u8 neogeo_ym2610_read_status(const neogeo_t *ng);
u8 neogeo_ym2610_read_status_hi(const neogeo_t *ng);

/* Achado #72: chip SMA+CMC42 (King of Fighters '99) — ver
 * core/src/system/prot_sma_kof99.c. */
u32 neogeo_kof99_bank_base(u16 sel);
u16 neogeo_sma_random_step(u16 *state);
void neogeo_kof99_decrypt_68k(u8 *p /* buffer de EXATAMENTE 0x900000 bytes */);
/* Achado #79: SMA genérico (kof99, garou, mslug3, kof2000). */
u32 neogeo_sma_bank_base(int type, u16 sel);
void neogeo_sma_decrypt_68k(int type, u8 *p /* 0x900000 bytes */);
void neogeo_sma_addrs(int type, u32 *bank_addr, u32 *rand_addr1, u32 *rand_addr2);
/* Achado #82: KOF98 MVS. `p` precisa ter 0x600000 bytes (big-endian). */
void neogeo_kof98_decrypt_68k(u8 *p);
/* Achado #83: família CMC50/PCM2/PVC — ver core/src/system/prot_cmc50.c. */
void neogeo_kof2002_decrypt_68k(u8 *cpurom);
void neogeo_samsho5_decrypt_68k(u8 *cpurom);
void neogeo_samsh5sp_decrypt_68k(u8 *cpurom);
void neogeo_mslug5_decrypt_68k(u8 *rom);
void neogeo_svc_decrypt_68k(u8 *rom);
void neogeo_kof2003_decrypt_68k(u8 *rom);
void neogeo_pcm2_decrypt(u8 *ymrom, u32 ymsize, int value);
void neogeo_pcm2_swap(u8 *ymrom, int value);
void neogeo_cmc42_gfx_decrypt(u8 *rom, u32 rom_size, int extra_xor);
/* Achado #81: CMC50 (kof2000...) — mesmo algoritmo, outras tabelas — e a M1
 * cifrada. `out` recebe 0x80000 bytes. */
void neogeo_cmc50_gfx_decrypt(u8 *rom, u32 rom_size, int extra_xor);
void neogeo_cmc50_m1_decrypt(const u8 *in, u32 in_size, u8 *out);
void neogeo_cmc_sfix_decrypt(const u8 *rom, u32 rom_size, u8 *fixed, u32 fixed_size);

/* Converte uma cor de 16 bits do formato de paleta do Neo-Geo (bit15 = "dark
 * bit", bits14-10=R, bits9-5=G, bits4-0=B, 5 bits por canal) pra RGB888.
 * Reconstruído a partir da NeoGeo Development Wiki (ver ARCHITECTURE.md);
 * o efeito exato do dark bit (aqui: escurece o resultado final pela metade)
 * é a parte com menos confiança — validar visualmente quando tivermos vídeo
 * de referência. */
void neogeo_color_to_rgb888(u16 color, u8 *r, u8 *g, u8 *b);

/* Como acima, mas com o modo "shadow" (bit0 do latch de sistema / pen de
 * sombra dos sprites), que no hardware é outro resistor de pull-down. */
void neogeo_color_to_rgb888_ex(u16 color, bool shadow, u8 *r, u8 *g, u8 *b);

/* Renderiza a camada fix (texto/HUD, tiles 8x8 4bpp) por cima do que já
 * estiver em `out_rgb888` (NÃO limpa o buffer — pixels transparentes do fix
 * layer mantêm o que já estava desenhado, tipicamente os sprites). Buffer
 * de NEOGEO_SCREEN_W x NEOGEO_SCREEN_H pixels RGB888. Use
 * neogeo_render_frame() pra um frame completo (limpa + sprites + fix). */
void neogeo_render_fix_layer(const neogeo_t *ng, u8 *out_rgb888);

/* Renderiza os sprites (LSPC2 SCB1-4) por cima do que já estiver em
 * `out_rgb888` — também não limpa. CONFIANÇA BAIXA/MÉDIA: o layout de bits
 * exato de SCB1 (palheta/flip/tile alto) e o formato de tile do C ROM
 * (blocos 8x8, ordem de bitplane) foram reconstruídos a partir de resumos
 * da NeoGeo Development Wiki e AINDA NÃO foram validados visualmente contra
 * hardware/imagem de referência real. Ver TODOs em
 * core/src/video/lspc_sprites.c antes de confiar no resultado pixel a
 * pixel. Shrink vertical usa aproximação linear, não a tabela real do chip
 * (L0 ROM) — formato visualmente plausível, não bit-exato. */
void neogeo_render_sprites(const neogeo_t *ng, u8 *out_rgb888);

/* Versões por FAIXA de linhas (y1 exclusivo), usadas pelo render linha a
 * linha do Achado #68. */
void neogeo_render_sprites_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1);
void neogeo_render_fix_layer_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1);
void neogeo_render_frame_range(const neogeo_t *ng, u8 *out_rgb888, u32 y0, u32 y1);

/* Roda UM frame dividido em 264 fatias de uma linha, desenhando cada linha
 * visível logo depois de varrê-la (Achado #68). É o caminho correto pra
 * efeitos de raster (IRQ de timer trocando paleta no meio da tela).
 * `out_rgb888` pode ser NULL pra só rodar a CPU. */
u32 neogeo_run_frame_render(neogeo_t *ng, u8 *out_rgb888);

/* Frame completo: limpa o buffer, desenha sprites, depois a fix layer por
 * cima (aproximação de prioridade — a ordem real de prioridade entre
 * sprites e fix ainda não foi implementada, ver TODO). */
void neogeo_render_frame(const neogeo_t *ng, u8 *out_rgb888);

#ifdef __cplusplus
}
#endif

#endif /* NEOCORE_NEOGEO_H */
