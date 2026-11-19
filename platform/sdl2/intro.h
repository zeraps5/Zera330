#ifndef ZERA330_INTRO_H
#define ZERA330_INTRO_H

#include <SDL.h>

enum { INTRO_DONE = 0, INTRO_QUIT = 1 };

/* Toca a abertura do arquivo .z3i. Retorna INTRO_QUIT se o usuário fechou a
 * janela ou apertou Esc (o frontend encerra), INTRO_DONE se terminou ou foi
 * pulada com outra tecla. Arquivo ausente ou inválido: retorna na hora.
 * volume: 0..100, o mesmo do jogo. */
int intro_play(SDL_Window *win, SDL_Renderer *ren, SDL_AudioDeviceID dev,
               const SDL_AudioSpec *have, const char *path, int volume);

#endif
