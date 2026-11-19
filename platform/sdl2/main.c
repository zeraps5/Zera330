/* Frontend SDL2 (Fase 6, adiantada por pedido do usuário pra ter feedback
 * visual do que já existe). Windows/desktop por ora — mesma base deve
 * funcionar sem alteração em SBC Linux ARM com SDL2 instalado (Fase 8).
 *
 * Uso:
 *   Zera330.exe                                      -> padrao de teste
 *   Zera330.exe     <dir_cartucho> [dir_bios]        -> carrega e roda
 *       (se dir_bios for omitido, procura a BIOS no proprio dir_cartucho)
 *
 * O título da janela mostra fps e % da velocidade real medidos ao vivo
 * (atualizado a cada ~500ms) — não confundir com o `SDL_Delay(16)` abaixo,
 * que é só um teto grosseiro de ~60fps, não um sincronismo de verdade
 * (TODO Fase 6b: vsync/frame pacing de verdade).
 */

#include <SDL.h>
#include "neocore/neogeo.h"
#include "intro.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#define MKDIR(d) mkdir(d, 0755)
#endif

/* Memória com bateria em `nvram/<jogo>.sav` (nome = arquivo do romset sem
 * diretório/extensão). NEOGEO_NO_NVRAM=1 desliga (boot limpo, pra comparar
 * contra o MAME sem save). */
static void nvram_path_for(const char *romset, char *out, size_t out_size) {
    const char *base = romset;
    for (const char *c = romset; *c; c++) if (*c == '/' || *c == '\\') base = c + 1;
    char name[256];
    snprintf(name, sizeof(name), "%s", base);
    char *dot = strrchr(name, '.');
    if (dot && dot != name) *dot = 0;
    snprintf(out, out_size, "nvram/%s.sav", name[0] ? name : "cart");
}

/* Clock real do 68000 do Neo-Geo (12MHz) dividido pelos ciclos/frame
 * (202752, ver Achado #23) = ~59.1856 Hz — a taxa de quadros "correta" pra
 * comparar contra o que estamos de fato entregando na tela. */
#define NEOGEO_TARGET_FPS (12000000.0 / 202752.0)

/* Escreve o framebuffer RGB888 como BMP 24-bit (sem depender de nenhuma lib
 * externa). Usado só pra depuração visual sem precisar de foco de janela
 * (screenshot via SO tem se mostrado pouco confiável neste ambiente). */
static void write_bmp(const char *path, const u8 *rgb888, u32 w, u32 h) {
    u32 row_bytes = ((w * 3u + 3u) & ~3u); /* linhas de BMP alinhadas a 4 bytes */
    u32 img_size = row_bytes * h;
    u32 file_size = 54u + img_size;
    FILE *f = fopen(path, "wb");
    if (!f) return;

    u8 header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    memcpy(&header[2], &file_size, 4);
    u32 offset = 54;
    memcpy(&header[10], &offset, 4);
    u32 info_size = 40;
    memcpy(&header[14], &info_size, 4);
    memcpy(&header[18], &w, 4);
    s32 signed_h = (s32)h; /* positivo = bottom-up (padrao BMP) */
    memcpy(&header[22], &signed_h, 4);
    u16 planes = 1, bpp = 24;
    memcpy(&header[26], &planes, 2);
    memcpy(&header[28], &bpp, 2);
    memcpy(&header[34], &img_size, 4);
    fwrite(header, 1, 54, f);

    u8 *row = (u8 *)calloc(1, row_bytes);
    for (u32 y = 0; y < h; y++) {
        u32 src_y = h - 1 - y; /* BMP e bottom-up */
        for (u32 x = 0; x < w; x++) {
            const u8 *p = rgb888 + (src_y * w + x) * 3u;
            row[x * 3 + 0] = p[2]; /* BMP e BGR */
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        fwrite(row, 1, row_bytes, f);
    }
    free(row);
    fclose(f);
}

static void fill_test_pattern(u8 *rgb888, u32 frame) {
    for (u32 y = 0; y < NEOGEO_SCREEN_H; y++) {
        for (u32 x = 0; x < NEOGEO_SCREEN_W; x++) {
            u8 *p = rgb888 + (y * NEOGEO_SCREEN_W + x) * 3u;
            p[0] = (u8)(x + frame);
            p[1] = (u8)(y + frame);
            p[2] = (u8)((x ^ y) + frame);
        }
    }
}

/* Ícone da janela: o "z" em pixels do manual de identidade do zera330
 * (grade 7x7 grafite sobre amarelo neon, bloco do canto em azul profundo,
 * scanlines). Gerado aqui pra não depender de arquivo de imagem. */
static void set_brand_icon(SDL_Window *win) {
    static const char *z[7] = { "111111B", "....111", "...111.", "..111..",
                                ".111...", "111....", "1111111" };
    enum { SZ = 64, CELL = 7, GAP = 1, MARGIN = 4, RAD = 12 };
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SZ, SZ, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return;
    Uint32 *px = (Uint32 *)s->pixels;
    const Uint32 clear = SDL_MapRGBA(s->format, 0, 0, 0, 0);
    const Uint32 yellow = SDL_MapRGBA(s->format, 0xFF, 0xE6, 0x00, 0xFF);
    const Uint32 scan = SDL_MapRGBA(s->format, 0xE6, 0xCF, 0x00, 0xFF);
    const Uint32 ink = SDL_MapRGBA(s->format, 0x14, 0x16, 0x1A, 0xFF);
    const Uint32 blue = SDL_MapRGBA(s->format, 0x06, 0x50, 0x6F, 0xFF);
    for (int y = 0; y < SZ; y++)
        for (int x = 0; x < SZ; x++) {
            int cx = x < RAD ? RAD - x : (x >= SZ - RAD ? x - (SZ - RAD - 1) : 0);
            int cy = y < RAD ? RAD - y : (y >= SZ - RAD ? y - (SZ - RAD - 1) : 0);
            bool inside = cx * cx + cy * cy <= RAD * RAD;
            px[y * (s->pitch / 4) + x] = !inside ? clear : ((y & 1) ? scan : yellow);
        }
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 7; c++) {
            if (z[r][c] == '.') continue;
            Uint32 col = z[r][c] == 'B' ? blue : ink;
            for (int y = 0; y < CELL - GAP; y++)
                for (int x = 0; x < CELL - GAP; x++)
                    px[(MARGIN + 4 + r * CELL + y) * (s->pitch / 4) + MARGIN + 4 + c * CELL + x] = col;
        }
    SDL_SetWindowIcon(win, s);
    SDL_FreeSurface(s);
}

/* ---- Controles configuráveis (user\config\zera330.ini, ver config.h) ---- */

/* Jogador 1 e 2: primeiro o controle escolhido pelo GUID no launcher; quem
 * está em "automático" fica com o próximo controle livre, na ordem. */
static void open_pads(SDL_GameController *pads[2], const z3_config *cfg) {
    for (int i = 0; i < 2; i++) if (pads[i]) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
    int n = SDL_NumJoysticks();
    bool used[64] = {false};
    for (int p = 0; p < 2; p++) {
        if (!cfg->pad_guid[p][0]) continue;
        for (int j = 0; j < n && j < 64; j++) {
            char g[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(j), g, sizeof(g));
            if (!used[j] && SDL_IsGameController(j) && !strcmp(g, cfg->pad_guid[p]) &&
                (pads[p] = SDL_GameControllerOpen(j)) != NULL) { used[j] = true; break; }
        }
    }
    for (int p = 0; p < 2; p++) {
        if (pads[p] || cfg->pad_guid[p][0]) continue;
        for (int j = 0; j < n && j < 64; j++)
            if (!used[j] && SDL_IsGameController(j) && (pads[p] = SDL_GameControllerOpen(j)) != NULL) { used[j] = true; break; }
    }
    for (int p = 0; p < 2; p++)
        if (pads[p]) printf("[controle] jogador %d: %s\n", p + 1, SDL_GameControllerName(pads[p]));
}

static bool keys_down(const u8 *keys, const z3_keyset *k) {
    for (int i = 0; i < k->n; i++) if (keys[k->sc[i]]) return true;
    return false;
}
static bool keys_has(const z3_keyset *k, SDL_Scancode sc) {
    for (int i = 0; i < k->n; i++) if (k->sc[i] == sc) return true;
    return false;
}
static bool pad_down(SDL_GameController *gc, const z3_padset *p) {
    if (!gc) return false;
    for (int i = 0; i < p->n; i++) {
        if (p->button[i] >= 0) {
            if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)p->button[i])) return true;
        } else {
            int v = SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)p->axis[i]) * p->sign[i];
            if (v > 16000) return true;
        }
    }
    return false;
}
static bool pad_has_button(const z3_padset *p, int button) {
    for (int i = 0; i < p->n; i++) if (p->button[i] == button) return true;
    return false;
}

/* Onde a imagem vai na janela: 4:3 como no monitor do fliperama, pixels
 * quadrados, ou esticada. */
static SDL_Rect game_rect(SDL_Renderer *ren, int aspect) {
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    SDL_Rect r = {0, 0, ww, wh};
    if (aspect == Z3_ASPECT_STRETCH) return r;
    const int an = aspect == Z3_ASPECT_4_3 ? 4 : (int)NEOGEO_SCREEN_W, ad = aspect == Z3_ASPECT_4_3 ? 3 : (int)NEOGEO_SCREEN_H;
    r.h = wh; r.w = wh * an / ad;
    if (r.w > ww) { r.w = ww; r.h = ww * ad / an; }
    r.x = (ww - r.w) / 2; r.y = (wh - r.h) / 2;
    return r;
}

/* Scanlines: escurece a parte de baixo de cada uma das 224 linhas. */
static void draw_scanlines(SDL_Renderer *ren, SDL_Rect r, int level) {
    static SDL_Rect rows[NEOGEO_SCREEN_H];
    if (!level || r.h < (int)NEOGEO_SCREEN_H * 2) return;
    for (int y = 0; y < (int)NEOGEO_SCREEN_H; y++) {
        int top = r.y + y * r.h / (int)NEOGEO_SCREEN_H, bot = r.y + (y + 1) * r.h / (int)NEOGEO_SCREEN_H;
        int h = (bot - top) / 3 > 0 ? (bot - top) / 3 : 1;
        rows[y].x = r.x; rows[y].w = r.w; rows[y].y = bot - h; rows[y].h = h;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, level == 1 ? 70 : 130);
    SDL_RenderFillRects(ren, rows, (int)NEOGEO_SCREEN_H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); /* saida sem buffer, pra log aparecer mesmo redirecionado */

    /* Achado #53 (investigação de perf): o core sozinho roda a ~390fps
     * (`debug_perf`), então algo no laço do SDL2 estava prendendo tudo em
     * ~29fps (49%). Suspeita: vsync do backend de renderização do SDL2
     * (D3D/OpenGL) prendendo `SDL_RenderPresent` na taxa do monitor, EM CIMA
     * do nosso próprio `SDL_Delay(16)` — os dois se somam. Desliga
     * explicitamente aqui; o hint precisa ser setado ANTES de
     * SDL_CreateRenderer. */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    SDL_GameController *pads[2] = { NULL, NULL };
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init falhou: %s\n", SDL_GetError());
        return 1;
    }

    /* Configurações do launcher: ZERA330_CONFIG aponta o arquivo; sem ela,
     * config/zera330.ini relativo à pasta de trabalho (o launcher roda o
     * emulador dentro de user\). */
    static z3_config cfg;
    {
        const char *cp = getenv("ZERA330_CONFIG");
        z3_config_load(&cfg, cp ? cp : "config/zera330.ini");
    }
    const char *game_title = getenv("ZERA330_TITULO");
    char base_title[256];
    if (game_title && *game_title) snprintf(base_title, sizeof(base_title), "zera330 — %s", game_title);
    else snprintf(base_title, sizeof(base_title), "zera330");

    /* filtro de escala: precisa estar definido antes de criar a textura */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth ? "linear" : "nearest");
    const int win_h = (int)NEOGEO_SCREEN_H * cfg.scale;
    const int win_w = cfg.aspect == Z3_ASPECT_4_3 ? win_h * 4 / 3 : (int)NEOGEO_SCREEN_W * cfg.scale;
    SDL_Window *win = SDL_CreateWindow(
        base_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow falhou: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    set_brand_icon(win);
    if (cfg.fullscreen) SDL_ShowCursor(SDL_DISABLE);

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);

    static u8 framebuf[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3];

    /* Fase 5: saída de áudio. Pedimos o device na taxa NATIVA do YM2610
     * (55555 Hz) e deixamos o SDL converter pra taxa da placa — assim o core
     * gera na taxa do chip e não precisamos de reamostragem própria.
     * Usamos SDL_QueueAudio (sem callback): o laço principal empurra as
     * amostras de cada frame, o que casa naturalmente com o nosso pacing. */
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = (int)NEOGEO_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL;
    SDL_AudioDeviceID audio_dev = 0;
    if (cfg.mute || cfg.volume == 0) {
        printf("[audio] sem som (configuracao)\n");
        SDL_memset(&have, 0, sizeof(have));
    } else if ((audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0)) == 0) {
        fprintf(stderr, "aviso: sem audio (%s) - seguindo mudo\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_dev, 0);
    }
    /* Amostras por frame: 55555 / 59.1856 nao e inteiro, entao acumulamos a
     * parte fracionaria pra nao dessincronizar com o tempo. */
    double audio_sample_accum = 0.0; (void)audio_sample_accum;
    static s16 audio_buf[4096];

    bool have_system = false;
    neogeo_t ng;
    neogeo_romset_t rom;
    char nvram_path[1024] = "";
    char state_path[1024] = "";   /* Achado #110: F5 salva, F7 carrega */
    char osd_msg[96] = "";
    Uint32 osd_until = 0;
    u32 nvram_sum = 0;
    memset(&rom, 0, sizeof(rom));

    if (argc > 1) {
        char err[256];
        const char *bios_dir = (argc > 2) ? argv[2] : NULL;
        if (neogeo_romset_load_dir_ex(&rom, argv[1], bios_dir, err, sizeof(err))) {
            neogeo_init(&ng, &rom);
            if (!getenv("NEOGEO_NO_NVRAM")) {
                nvram_path_for(argv[1], nvram_path, sizeof(nvram_path));
                if (neogeo_nvram_load(&ng, nvram_path)) printf("[nvram] carregado de %s\n", nvram_path);
                nvram_sum = neogeo_nvram_checksum(&ng);
            }
            neogeo_reset(&ng);
            have_system = true;
            {   /* states/<jogo>.sta — mesmo nome base do save de nvram */
                char base[1024];
                nvram_path_for(argv[1], base, sizeof(base));   /* "nvram/<jogo>.sav" */
                const char *nm = base + 6;                      /* pula "nvram/" */
                snprintf(state_path, sizeof(state_path), "states/%.*s.sta",
                         (int)(strlen(nm) - 4), nm);
            }
            printf("Romset carregado de '%s' (BIOS=%u P=%u S=%u M=%u V=%u C_odd=%u C_even=%u bytes)\n",
                   argv[1], rom.bios_size, rom.p_size, rom.s_size, rom.m_size, rom.v_size,
                   rom.c_odd_size, rom.c_even_size);
        } else {
            printf("Nao foi possivel carregar romset de '%s': %s\n", argv[1], err);
            printf("Mostrando padrao de teste em vez disso.\n");
        }
    } else {
        printf("Nenhum romset informado. Uso: %s <diretorio_do_romset>\n", argv[0]);
        printf("Mostrando padrao de teste.\n");
    }

    /* NEOGEO_DUMP_BMP: se definida, salva o framebuffer nesse caminho a
     * cada 30 frames (depuração visual sem depender de foco de janela).
     * NEOGEO_MAX_FRAMES: se definida, encerra sozinho depois de N frames
     * (util pra capturar automaticamente sem intervencao). */
    const char *dump_path = getenv("NEOGEO_DUMP_BMP");
    const char *max_frames_str = getenv("NEOGEO_MAX_FRAMES");
    long max_frames = max_frames_str ? atol(max_frames_str) : -1;

    bool running = true;
    u32 frame = 0;

    /* Abertura da marca antes de cada jogo (pula com qualquer tecla; Esc
     * fecha). ZERA330_SEM_INTRO=1 desliga; execuções automáticas
     * (NEOGEO_MAX_FRAMES) também não tocam. */
    if (have_system && !getenv("ZERA330_SEM_INTRO") && !max_frames_str) {
        char intro_path[1024];
        char *base = SDL_GetBasePath();
        snprintf(intro_path, sizeof(intro_path), "%sassets/zera330_intro.z3i", base ? base : "");
        SDL_free(base);
        if (intro_play(win, ren, audio_dev, &have, intro_path, cfg.volume) == INTRO_QUIT) running = false;
    }
    bool paused = false;
    char set_name[256];   /* nome do jogo pras capturas de tela: o do arquivo */
    {
        const char *b = argc > 1 ? argv[1] : "zera330";
        for (const char *c = b; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
        snprintf(set_name, sizeof(set_name), "%s", b);
        char *dot = strrchr(set_name, '.');
        if (dot && dot != set_name) *dot = 0;
    }

    /* FPS e "% de velocidade real" (medido / NEOGEO_TARGET_FPS) no título da
     * janela, atualizados a cada ~500ms — pedido pelo usuário pra ver de
     * cara se o core está rodando no ritmo certo (60fps "de mentirinha" via
     * SDL_Delay(16) não prova nada; isto mede o tempo de parede de verdade). */
    Uint32 fps_window_start = SDL_GetTicks();
    u32 fps_window_frames = 0;
    char title_buf[192];

    /* NEOGEO_PERF_LOG=1: imprime no stderr, a cada 60 frames, quanto tempo
     * médio cada etapa do laço está gastando — pra achar por que o fps caía
     * pra ~30 mesmo com o core rodando a ~390fps sozinho (`debug_perf`). */
    const bool dbg_perf = getenv("NEOGEO_PERF_LOG") != NULL;
    double perf_cpu_ms = 0.0, perf_render_ms = 0.0, perf_upload_ms = 0.0,
           perf_copy_ms = 0.0, perf_present_ms = 0.0;
    u32 perf_frames = 0;

    /* Achado #53 (ver docs/ROADMAP.md): `SDL_Delay(16)` no fim do laço era um
     * sleep FIXO somado por cima do tempo que CPU+render já tinham gastado —
     * não um teto de ~60fps de verdade. Com cenas mais pesadas (cpu+render
     * chegando a 15-17ms), o frame total virava 16+17=33ms (~30fps), batendo
     * exatamente com o que o usuário via no título. Corrigido pra pacing de
     * frame de verdade: mede quanto o frame gastou e só dorme o restante até
     * completar o intervalo-alvo (1000/NEOGEO_TARGET_FPS ms); se o frame já
     * levou mais que isso, não dorme (deixa o próximo frame recuperar). */
    const double target_frame_ms = 1000.0 / NEOGEO_TARGET_FPS;
    const double perf_freq = (double)SDL_GetPerformanceFrequency();

    while (running) {
        Uint64 frame_start_pc = SDL_GetPerformanceCounter();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            /* Atalhos (configuráveis no launcher): tecla ou botão do controle,
             * no momento em que aperta. Alt+Enter também alterna tela cheia. */
            int hk = -1;
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                if (ev.key.keysym.scancode == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT)) hk = Z3_HK_FULLSCREEN;
                else for (int i = 0; i < Z3_HOTKEYS && hk < 0; i++)
                    if (i != Z3_HK_SERVICE && keys_has(&cfg.hk_kbd[i], ev.key.keysym.scancode)) hk = i;
            }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN)
                for (int i = 0; i < Z3_HOTKEYS && hk < 0; i++)
                    if (i != Z3_HK_SERVICE && pad_has_button(&cfg.hk_pad[i], ev.cbutton.button)) hk = i;
            if (hk == Z3_HK_QUIT) running = false;
            if (hk == Z3_HK_FULLSCREEN) {
                bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                SDL_ShowCursor(fs ? SDL_ENABLE : SDL_DISABLE);
            }
            if (hk == Z3_HK_PAUSE && have_system) {
                paused = !paused;
                if (audio_dev != 0) SDL_ClearQueuedAudio(audio_dev);
                snprintf(osd_msg, sizeof(osd_msg), paused ? "pausado" : "continuando");
                osd_until = paused ? 0xFFFFFFFFu : SDL_GetTicks() + 1500u;
            }
            if (hk == Z3_HK_RESET && have_system) {
                neogeo_reset(&ng);
                if (audio_dev != 0) SDL_ClearQueuedAudio(audio_dev);
                snprintf(osd_msg, sizeof(osd_msg), "reiniciado");
                osd_until = SDL_GetTicks() + 2000u;
                paused = false;
            }
            if (hk == Z3_HK_SHOT) {
                char shot[512];
                time_t now_t = time(NULL);
                struct tm *tmv = localtime(&now_t);
                MKDIR("capturas");
                snprintf(shot, sizeof(shot), "capturas/%s-%04d%02d%02d-%02d%02d%02d.bmp", set_name,
                         tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday, tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
                write_bmp(shot, framebuf, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
                printf("[captura] %s\n", shot);
                snprintf(osd_msg, sizeof(osd_msg), "captura salva");
                osd_until = SDL_GetTicks() + 2000u;
            }
            /* Achado #110: save state */
            if (have_system && (hk == Z3_HK_SAVE || hk == Z3_HK_LOAD)) {
                bool ok;
                if (hk == Z3_HK_SAVE) {
                    MKDIR("states");
                    ok = neogeo_state_save(&ng, state_path);
                    snprintf(osd_msg, sizeof(osd_msg), ok ? "estado salvo" : "ERRO ao salvar estado");
                } else {
                    ok = neogeo_state_load(&ng, state_path);
                    if (ok && audio_dev != 0) SDL_ClearQueuedAudio(audio_dev);
                    snprintf(osd_msg, sizeof(osd_msg), ok ? "estado carregado" : "sem estado salvo (ou de outra versao)");
                }
                printf("[state] %s (%s)\n", osd_msg, state_path);
                osd_until = SDL_GetTicks() + 2000u;
                if (ok && hk == Z3_HK_LOAD) paused = false;
            }
            /* gamepads conectados/desconectados a quente */
            if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED) open_pads(pads, &cfg);
        }

        /* Controles, lidos a cada quadro do estado atual (segurar = segurar),
         * com as teclas e botões configurados no launcher (padrões em
         * platform/common/zera330_input.h): P1 setas/WASD + Z X C V, START 1
         * ou Enter, MOEDA 5 ou Backspace; P2 IJKL + B N M vírgula, START 2,
         * MOEDA 6; controle: direcional (e analógico), A B X Y, Start, Back. */
        if (have_system) {
            const u8 *keys = SDL_GetKeyboardState(NULL);
            u8 pl[2] = { 0xFFu, 0xFFu };
            u8 sys = 0xFFu, coin = 0xFFu;
            for (int p = 0; p < 2; p++) {
                SDL_GameController *gc = pads[p];
                for (int i = 0; i < Z3_PLAYER_INPUTS; i++) {
                    bool on = keys_down(keys, &cfg.kbd[p][i]) || pad_down(gc, &cfg.pad[p][i]);
                    if (!on && gc && cfg.analog && i <= Z3_RIGHT) {
                        const Sint16 dz = 16000;
                        Sint16 ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
                        Sint16 ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
                        on = (i == Z3_UP && ay < -dz) || (i == Z3_DOWN && ay > dz) ||
                             (i == Z3_LEFT && ax < -dz) || (i == Z3_RIGHT && ax > dz);
                    }
                    if (!on) continue;
                    if (i < Z3_START) pl[p] &= (u8)~(1u << i);   /* cima baixo esq dir A B C D = bits 0..7 */
                    else if (i == Z3_START) sys &= (u8)~(p == 0 ? 0x01u : 0x04u);
                    else coin &= (u8)~(p == 0 ? 0x01u : 0x02u);
                }
            }
            if (keys_down(keys, &cfg.hk_kbd[Z3_HK_SERVICE]) || pad_down(pads[0], &cfg.hk_pad[Z3_HK_SERVICE]) ||
                pad_down(pads[1], &cfg.hk_pad[Z3_HK_SERVICE]))
                coin &= (u8)~0x04u;
            ng.p1_buttons = pl[0];
            ng.p2_buttons = pl[1];
            ng.sys_buttons = sys;
            ng.coin_buttons = coin;
        }

        Uint64 pc0 = 0, pc1 = 0, pc2 = 0, pc3 = 0, pc4 = 0, pc5 = 0;
        if (dbg_perf) pc0 = SDL_GetPerformanceCounter();

        if (have_system && paused) {
            if (dbg_perf) pc1 = pc2 = SDL_GetPerformanceCounter();
        } else if (have_system) {
            /* Achado #68: um frame, rodado em 264 fatias de uma linha, com
             * cada linha visível desenhada logo depois de varrida. Substitui
             * o antigo "roda o frame inteiro e tira uma foto no vblank"
             * (Achado #54) — que continua correto pra tela estática, mas
             * perde qualquer efeito de raster feito pela IRQ de timer. */
            neogeo_run_frame_render(&ng, framebuf);

            if (audio_dev != 0) {
                /* Achado #88: o áudio já foi gerado DURANTE a emulação do
                 * frame, no tempo emulado — aqui só esvaziamos o buffer. */
                u32 want_samples = neogeo_audio_available(&ng);
                if (want_samples > 4096u) want_samples = 4096u;
                neogeo_audio_render(&ng, audio_buf, want_samples);
                if (cfg.volume < 100)
                    for (u32 k = 0; k < want_samples * 2u; k++) audio_buf[k] = (s16)(audio_buf[k] * cfg.volume / 100);
                /* Se a fila cresceu demais (host mais lento que o emulado),
                 * descarta este bloco em vez de acumular latencia. */
                if (SDL_GetQueuedAudioSize(audio_dev) < NEOGEO_AUDIO_RATE)
                    SDL_QueueAudio(audio_dev, audio_buf, want_samples * 2u * sizeof(s16));
            }
            if (dbg_perf) pc1 = pc2 = SDL_GetPerformanceCounter();
        } else {
            fill_test_pattern(framebuf, frame);
            if (dbg_perf) { pc1 = pc2 = SDL_GetPerformanceCounter(); }
        }

        SDL_UpdateTexture(tex, NULL, framebuf, (int)(NEOGEO_SCREEN_W * 3));
        if (dbg_perf) pc3 = SDL_GetPerformanceCounter();
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        {
            SDL_Rect dst = game_rect(ren, cfg.aspect);
            SDL_RenderCopy(ren, tex, NULL, &dst);
            draw_scanlines(ren, dst, cfg.scanlines);
        }
        if (dbg_perf) pc4 = SDL_GetPerformanceCounter();
        SDL_RenderPresent(ren);
        if (dbg_perf) pc5 = SDL_GetPerformanceCounter();

        if (dbg_perf) {
            double freq = (double)SDL_GetPerformanceFrequency();
            perf_cpu_ms    += (double)(pc1 - pc0) * 1000.0 / freq;
            perf_render_ms += (double)(pc2 - pc1) * 1000.0 / freq;
            perf_upload_ms += (double)(pc3 - pc2) * 1000.0 / freq;
            perf_copy_ms   += (double)(pc4 - pc3) * 1000.0 / freq;
            perf_present_ms+= (double)(pc5 - pc4) * 1000.0 / freq;
            perf_frames++;
            if (perf_frames >= 60) {
                fprintf(stderr, "[perf] media por frame (ultimos %u): cpu=%.2fms render=%.2fms upload_tex=%.2fms render_copy=%.2fms present=%.2fms\n",
                        perf_frames, perf_cpu_ms/perf_frames, perf_render_ms/perf_frames,
                        perf_upload_ms/perf_frames, perf_copy_ms/perf_frames, perf_present_ms/perf_frames);
                perf_cpu_ms = perf_render_ms = perf_upload_ms = perf_copy_ms = perf_present_ms = 0.0;
                perf_frames = 0;
            }
        }

        if (dump_path && (frame % 30u) == 0u) {
            write_bmp(dump_path, framebuf, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
        }

        /* Pacing de frame de verdade (ver Achado #53 no topo do laço): só
         * dorme o tempo que falta pra completar ~16.9ms (1000/59.1856), não
         * mais um "SDL_Delay(16)" fixo somado por cima do custo real do
         * frame. */
        /* Achado #98: prazo ABSOLUTO de alta resolução. O `SDL_Delay` trunca
         * os milissegundos (16,9 ms -> 16) e cada frame tinha seu próprio
         * alvo relativo, então o erro acumulava: medido 60,1 fps contra os
         * 59,19 Hz do Neo-Geo. Agora o prazo avança exatamente 1/59,1856 s
         * por frame; dorme em ms até ~2 ms antes e completa com espera ativa. */
        (void)frame_start_pc; (void)target_frame_ms;
        {
            static Uint64 deadline = 0;
            const Uint64 period = (Uint64)((double)perf_freq / NEOGEO_TARGET_FPS);
            Uint64 now_pc = SDL_GetPerformanceCounter();
            if (deadline == 0 || now_pc > deadline + period * 4u) deadline = now_pc; /* 1o frame ou atraso grande */
            deadline += period;
            for (;;) {
                now_pc = SDL_GetPerformanceCounter();
                if (now_pc >= deadline) break;
                double left_ms = (double)(deadline - now_pc) * 1000.0 / perf_freq;
                if (left_ms > 2.0) SDL_Delay((Uint32)(left_ms - 2.0));
            }
        }
        frame++;
        fps_window_frames++;

        /* Grava a memória com bateria a cada ~10 s se mudou (crash não perde). */
        if (have_system && nvram_path[0] && (frame % 600u) == 0u) {
            u32 sum = neogeo_nvram_checksum(&ng);
            if (sum != nvram_sum) {
                MKDIR("nvram");
                if (neogeo_nvram_save(&ng, nvram_path)) nvram_sum = sum;
            }
        }

        Uint32 now = SDL_GetTicks();
        Uint32 elapsed_ms = now - fps_window_start;
        if (elapsed_ms >= 500u) {
            double fps = (double)fps_window_frames * 1000.0 / (double)elapsed_ms;
            double speed_pct = (fps / NEOGEO_TARGET_FPS) * 100.0;
            const bool osd = SDL_GetTicks() < osd_until;
            if (cfg.show_fps && !paused)
                snprintf(title_buf, sizeof(title_buf), "%s — %.1f fps (%.0f%% da velocidade real)%s%s",
                         base_title, fps, speed_pct, osd ? " — " : "", osd ? osd_msg : "");
            else
                snprintf(title_buf, sizeof(title_buf), "%s%s%s", base_title, osd ? " — " : "", osd ? osd_msg : "");
            SDL_SetWindowTitle(win, title_buf);
            fps_window_start = now;
            fps_window_frames = 0;
        }

        {   /* Medição interna (sem o tempo de carga): do frame 60 até o fim. */
            static Uint64 bench_t0 = 0; static u32 bench_f0 = 0;
            if (frame == 60u) { bench_t0 = SDL_GetPerformanceCounter(); bench_f0 = frame; }
            if (max_frames >= 0 && (long)frame >= max_frames && bench_t0) {
                double secs = (double)(SDL_GetPerformanceCounter() - bench_t0) / perf_freq;
                printf("[bench] %u frames em %.3f s = %.3f fps (Neo-Geo: %.3f) = %.2f%%\n",
                       frame - bench_f0, secs, (frame - bench_f0) / secs, NEOGEO_TARGET_FPS,
                       100.0 * (frame - bench_f0) / secs / NEOGEO_TARGET_FPS);
                fflush(stdout);
            }
        }
        if (max_frames >= 0 && (long)frame >= max_frames) running = false;
    }

    if (dump_path) write_bmp(dump_path, framebuf, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);

    if (have_system && nvram_path[0]) {
        MKDIR("nvram");
        if (neogeo_nvram_save(&ng, nvram_path)) printf("[nvram] salvo em %s\n", nvram_path);
    }
    if (have_system) neogeo_romset_free(&rom);
    if (audio_dev != 0) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    for (int i = 0; i < 2; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
    SDL_Quit();
    return 0;
}
