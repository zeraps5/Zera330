/* Abertura do zera330: toca assets/zera330_intro.z3i (gerado por
 * tools/intro_pack.py a partir do vídeo da marca) antes de cada jogo.
 * Formato descrito em tools/intro_pack.py. Decodifica com o miniz que já vem
 * no core, então funciona igual no Windows e no Linux ARM, sem FFmpeg. */
#include "intro.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return rd16(p) | ((unsigned long)rd16(p + 2) << 16); }

static unsigned char *load_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

/* Áudio mono/estéreo s16 do arquivo convertido pro formato do device. */
static void queue_audio(SDL_AudioDeviceID dev, const SDL_AudioSpec *have,
                        unsigned char *pcm, size_t bytes, int rate, int ch, int volume) {
    if (volume < 100) {   /* o arquivo é s16 little-endian */
        for (size_t i = 0; i + 1 < bytes; i += 2) {
            int v = (short)(pcm[i] | (pcm[i + 1] << 8)) * volume / 100;
            pcm[i] = (unsigned char)(v & 0xFF);
            pcm[i + 1] = (unsigned char)((v >> 8) & 0xFF);
        }
    }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, AUDIO_S16LSB, (Uint8)ch, rate, have->format, have->channels, have->freq) < 0) return;
    cvt.len = (int)bytes;
    cvt.buf = malloc(bytes * (size_t)cvt.len_mult);
    if (!cvt.buf) return;
    memcpy(cvt.buf, pcm, bytes);
    if (cvt.needed) SDL_ConvertAudio(&cvt); else cvt.len_cvt = cvt.len;
    SDL_QueueAudio(dev, cvt.buf, (Uint32)cvt.len_cvt);
    free(cvt.buf);
}

int intro_play(SDL_Window *win, SDL_Renderer *ren, SDL_AudioDeviceID dev,
               const SDL_AudioSpec *have, const char *path, int volume) {
    size_t size = 0;
    unsigned char *file = load_file(path, &size);
    if (!file) { printf("[intro] %s nao encontrado - pulando a abertura\n", path); return INTRO_DONE; }
    int result = INTRO_DONE;
    if (size < 36 || memcmp(file, "Z3IN", 4) != 0 || rd16(file + 4) != 1u) {
        printf("[intro] %s invalido - pulando a abertura\n", path);
        free(file);
        return INTRO_DONE;
    }
    const int w = (int)rd16(file + 6), h = (int)rd16(file + 8), frames = (int)rd16(file + 10);
    const unsigned fps_num = rd16(file + 12), fps_den = rd16(file + 14) ? rd16(file + 14) : 1u;
    const int rate = (int)rd32(file + 16), ch = (int)rd16(file + 20);
    const unsigned long araw = rd32(file + 24), az = rd32(file + 28);
    size_t pos = 32;
    const size_t npix = (size_t)w * (size_t)h * 3u;
    unsigned char *acc = calloc(1, npix), *rgb = malloc(npix), *pcm = malloc(araw ? araw : 1);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!acc || !rgb || !pcm || !tex || pos + az > size) goto out;

    if (dev && araw) {
        mz_ulong n = (mz_ulong)araw;
        if (mz_uncompress(pcm, &n, file + pos, (mz_ulong)az) == MZ_OK) {
            SDL_ClearQueuedAudio(dev);
            queue_audio(dev, have, pcm, (size_t)n, rate, ch, volume);
        }
    }
    pos += az;

    const Uint64 freq = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < frames && pos + 4 <= size; i++) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { result = INTRO_QUIT; goto out; }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) { result = INTRO_QUIT; goto out; }
            /* qualquer outra tecla, botão do controle ou clique pula a abertura */
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_MOUSEBUTTONDOWN) goto out;
        }
        const unsigned long zs = rd32(file + pos);
        pos += 4;
        if (pos + zs > size) break;
        mz_ulong n = (mz_ulong)npix;
        if (mz_uncompress(rgb, &n, file + pos, (mz_ulong)zs) != MZ_OK || n != npix) break;
        pos += zs;
        for (size_t k = 0; k < npix; k++) {
            acc[k] = (unsigned char)(acc[k] + rgb[k]);
            rgb[k] = (unsigned char)((acc[k] << 1) | (acc[k] >> 6)); /* 7 -> 8 bits */
        }
        {   /* ZERA330_INTRO_DUMP=<pasta>: salva os quadros 0, 45 e 75 (conferência) */
            const char *dd = getenv("ZERA330_INTRO_DUMP");
            if (dd && (i == 0 || i == 45 || i == 75)) {
                SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(rgb, w, h, 24, w * 3, SDL_PIXELFORMAT_RGB24);
                char fn[1024];
                snprintf(fn, sizeof(fn), "%s/intro_%02d.bmp", dd, i);
                if (sf) { SDL_SaveBMP(sf, fn); SDL_FreeSurface(sf); }
            }
        }
        /* espera a hora deste quadro (relógio de parede, sem acumular erro) */
        const Uint64 due = t0 + freq * (Uint64)i * fps_den / fps_num;
        while (SDL_GetPerformanceCounter() < due) SDL_Delay(1);

        SDL_UpdateTexture(tex, NULL, rgb, w * 3);
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_Rect dst;  /* 16:9 dentro da janela, faixas no grafite da marca */
        dst.w = ww; dst.h = ww * h / w;
        if (dst.h > wh) { dst.h = wh; dst.w = wh * w / h; }
        dst.x = (ww - dst.w) / 2; dst.y = (wh - dst.h) / 2;
        SDL_SetRenderDrawColor(ren, 0x14, 0x16, 0x1A, 0xFF);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);
    }
    {   /* segura o último quadro até completar a duração do vídeo */
        const Uint64 end = t0 + freq * (Uint64)frames * fps_den / fps_num;
        while (SDL_GetPerformanceCounter() < end) SDL_Delay(1);
    }
out:
    (void)win;
    if (dev) SDL_ClearQueuedAudio(dev);
    if (tex) SDL_DestroyTexture(tex);
    free(acc); free(rgb); free(pcm); free(file);
    return result;
}
