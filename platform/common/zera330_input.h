/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controles do zera330, compartilhados entre o launcher (que edita) e o
 * emulador (que lê) — os dois usam esta mesma tabela, então o que o launcher
 * grava é exatamente o que o jogo entende.
 *
 * user\config\zera330.ini:
 *   [p1_teclado] [p2_teclado] [atalhos_teclado]   cima=up,w   (nomes de tecla abaixo)
 *   [p1_controle] [p2_controle] [atalhos_controle] a=a        (nomes de botão do SDL)
 *   [controle] p1=auto|<guid>  p2=auto|<guid>  analogico=1
 * Chave ausente = padrão; chave vazia = nada. Vários valores separados por vírgula.
 *
 * Teclas pelo lugar no teclado (scancode), não pela letra: no ABNT2 a tecla do
 * "Ç" é "semicolon". Cada tecla tem o código USB HID (= SDL_Scancode) e o
 * scancode PS/2 do Windows (0x100 = prefixo E0), que o launcher usa pra
 * capturar e pra mostrar o nome no idioma do teclado do usuário. */
#ifndef ZERA330_INPUT_H
#define ZERA330_INPUT_H

typedef struct { const char *name; int hid; int ps2; } z3_key;

static const z3_key z3_keys[] = {
    {"a", 4, 0x1E}, {"b", 5, 0x30}, {"c", 6, 0x2E}, {"d", 7, 0x20}, {"e", 8, 0x12}, {"f", 9, 0x21},
    {"g", 10, 0x22}, {"h", 11, 0x23}, {"i", 12, 0x17}, {"j", 13, 0x24}, {"k", 14, 0x25}, {"l", 15, 0x26},
    {"m", 16, 0x32}, {"n", 17, 0x31}, {"o", 18, 0x18}, {"p", 19, 0x19}, {"q", 20, 0x10}, {"r", 21, 0x13},
    {"s", 22, 0x1F}, {"t", 23, 0x14}, {"u", 24, 0x16}, {"v", 25, 0x2F}, {"w", 26, 0x11}, {"x", 27, 0x2D},
    {"y", 28, 0x15}, {"z", 29, 0x2C},
    {"1", 30, 0x02}, {"2", 31, 0x03}, {"3", 32, 0x04}, {"4", 33, 0x05}, {"5", 34, 0x06},
    {"6", 35, 0x07}, {"7", 36, 0x08}, {"8", 37, 0x09}, {"9", 38, 0x0A}, {"0", 39, 0x0B},
    {"enter", 40, 0x1C}, {"esc", 41, 0x01}, {"backspace", 42, 0x0E}, {"tab", 43, 0x0F}, {"space", 44, 0x39},
    {"minus", 45, 0x0C}, {"equals", 46, 0x0D}, {"lbracket", 47, 0x1A}, {"rbracket", 48, 0x1B},
    {"backslash", 49, 0x2B}, {"semicolon", 51, 0x27}, {"apostrophe", 52, 0x28}, {"grave", 53, 0x29},
    {"comma", 54, 0x33}, {"period", 55, 0x34}, {"slash", 56, 0x35}, {"capslock", 57, 0x3A},
    {"f1", 58, 0x3B}, {"f2", 59, 0x3C}, {"f3", 60, 0x3D}, {"f4", 61, 0x3E}, {"f5", 62, 0x3F}, {"f6", 63, 0x40},
    {"f7", 64, 0x41}, {"f8", 65, 0x42}, {"f9", 66, 0x43}, {"f10", 67, 0x44}, {"f11", 68, 0x57}, {"f12", 69, 0x58},
    {"scrolllock", 71, 0x46},
    {"insert", 73, 0x152}, {"home", 74, 0x147}, {"pageup", 75, 0x149}, {"delete", 76, 0x153},
    {"end", 77, 0x14F}, {"pagedown", 78, 0x151},
    {"right", 79, 0x14D}, {"left", 80, 0x14B}, {"down", 81, 0x150}, {"up", 82, 0x148},
    {"kp_divide", 84, 0x135}, {"kp_multiply", 85, 0x37}, {"kp_minus", 86, 0x4A}, {"kp_plus", 87, 0x4E},
    {"kp_enter", 88, 0x11C},
    {"kp1", 89, 0x4F}, {"kp2", 90, 0x50}, {"kp3", 91, 0x51}, {"kp4", 92, 0x4B}, {"kp5", 93, 0x4C},
    {"kp6", 94, 0x4D}, {"kp7", 95, 0x47}, {"kp8", 96, 0x48}, {"kp9", 97, 0x49}, {"kp0", 98, 0x52},
    {"kp_period", 99, 0x53}, {"nonusbackslash", 100, 0x56}, {"kp_comma", 133, 0x7E}, {"intl1", 135, 0x73},
    {"lctrl", 224, 0x1D}, {"lshift", 225, 0x2A}, {"lalt", 226, 0x38},
    {"rctrl", 228, 0x11D}, {"rshift", 229, 0x36}, {"ralt", 230, 0x138},
};
#define Z3_KEY_COUNT ((int)(sizeof(z3_keys) / sizeof(z3_keys[0])))

/* Entradas de um jogador do Neo Geo, na ordem dos bits do conector. */
enum { Z3_UP, Z3_DOWN, Z3_LEFT, Z3_RIGHT, Z3_A, Z3_B, Z3_C, Z3_D, Z3_START, Z3_COIN, Z3_PLAYER_INPUTS };
static const char *const z3_player_keys[Z3_PLAYER_INPUTS] = {
    "cima", "baixo", "esquerda", "direita", "a", "b", "c", "d", "start", "moeda"};
static const char *const z3_player_kbd_default[2][Z3_PLAYER_INPUTS] = {
    {"up,w", "down,s", "left,a", "right,d", "z", "x", "c", "v", "1,enter", "5,backspace"},
    {"i", "k", "j", "l", "b", "n", "m", "comma", "2", "6"}};
/* Controle: botões do SDL_GameController ("a", "dpup"...) ou eixo com
 * sentido ("leftx-", "lefty+", "lefttrigger"). Os dois jogadores começam
 * iguais: cada um no seu controle. */
static const char *const z3_player_pad_default[Z3_PLAYER_INPUTS] = {
    "dpup", "dpdown", "dpleft", "dpright", "a", "b", "x", "y", "start", "back"};

/* Atalhos do emulador (o serviço é a chave SERVICE da placa, segurada). */
enum { Z3_HK_SERVICE, Z3_HK_PAUSE, Z3_HK_RESET, Z3_HK_SAVE, Z3_HK_LOAD, Z3_HK_FULLSCREEN, Z3_HK_SHOT,
       Z3_HK_QUIT, Z3_HOTKEYS };
static const char *const z3_hotkey_keys[Z3_HOTKEYS] = {
    "servico", "pausa", "reiniciar", "salvar_estado", "carregar_estado", "tela_cheia", "captura", "sair"};
static const char *const z3_hotkey_kbd_default[Z3_HOTKEYS] = {"f1", "p", "f3", "f5", "f7", "f11", "f12", "esc"};
static const char *const z3_hotkey_pad_default[Z3_HOTKEYS] = {"", "", "", "", "", "", "", ""};

static int z3_key_find(const char *name, int len) {
    for (int i = 0; i < Z3_KEY_COUNT; i++) {
        const char *k = z3_keys[i].name;
        int n = 0;
        while (k[n]) n++;
        if (n != len) continue;
        int j = 0;
        while (j < len && k[j] == name[j]) j++;
        if (j == len) return i;
    }
    return -1;
}

#endif
