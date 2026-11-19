/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Configuração do emulador lida de user\config\zera330.ini (o launcher grava;
 * ver platform/common/zera330_input.h). Sem o arquivo, tudo fica no padrão. */
#ifndef ZERA330_CONFIG_H
#define ZERA330_CONFIG_H

#include <SDL.h>
#include <stdbool.h>
#include "../common/zera330_input.h"

#define Z3_MAX_BINDS 4

typedef struct { int n; SDL_Scancode sc[Z3_MAX_BINDS]; } z3_keyset;
/* botão do controle, ou eixo (axis >= 0) com sentido (+1/-1; gatilho = +1) */
typedef struct { int n; int button[Z3_MAX_BINDS], axis[Z3_MAX_BINDS], sign[Z3_MAX_BINDS]; } z3_padset;

enum { Z3_ASPECT_4_3, Z3_ASPECT_SQUARE, Z3_ASPECT_STRETCH };

typedef struct {
    z3_keyset kbd[2][Z3_PLAYER_INPUTS], hk_kbd[Z3_HOTKEYS];
    z3_padset pad[2][Z3_PLAYER_INPUTS], hk_pad[Z3_HOTKEYS];
    char pad_guid[2][40];   /* "" = automático (1º controle livre) */
    bool analog;            /* analógico esquerdo também move */
    bool fullscreen, smooth, show_fps;
    int scale;              /* tamanho da janela: 2..5 x 224 linhas */
    int aspect;             /* Z3_ASPECT_* */
    int scanlines;          /* 0 desligadas, 1 leves, 2 fortes */
    int volume;             /* 0..100 */
    bool mute;
} z3_config;

/* Lê o arquivo (NULL ou ausente = só padrões). Chamar depois de SDL_Init. */
void z3_config_load(z3_config *c, const char *path);

#endif
