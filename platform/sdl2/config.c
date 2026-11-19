/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* INI mínimo, portátil (Windows e Linux): seções [x], chave=valor, ; comenta. */

static char *ini_load(const char *path) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (t && fread(t, 1, (size_t)n, f) != (size_t)n) { free(t); t = NULL; }
    if (t) t[n] = 0;
    fclose(f);
    return t;
}

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }
static bool same(const char *a, int alen, const char *b) {
    int i = 0;
    for (; i < alen && b[i]; i++) if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return false;
    return i == alen && !b[i];
}

/* Valor de [sec] key em out; false se a chave não existe (vazia existe). */
static bool ini_get(const char *text, const char *sec, const char *key, char *out, size_t cap) {
    if (!text) return false;
    bool in = false;
    for (const char *p = text; *p;) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        const char *a = p, *b = e;
        while (a < b && (*a == ' ' || *a == '\t' || (unsigned char)*a == 0xEF || (unsigned char)*a == 0xBB || (unsigned char)*a == 0xBF)) a++;
        while (b > a && (b[-1] == '\r' || b[-1] == ' ' || b[-1] == '\t')) b--;
        if (a < b && *a == '[') {
            const char *c = a + 1;
            while (c < b && *c != ']') c++;
            in = same(a + 1, (int)(c - a - 1), sec);
        } else if (in && a < b && *a != ';' && *a != '#') {
            const char *eq = a;
            while (eq < b && *eq != '=') eq++;
            const char *ke = eq;
            while (ke > a && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
            if (eq < b && same(a, (int)(ke - a), key)) {
                const char *v = eq + 1;
                while (v < b && (*v == ' ' || *v == '\t')) v++;
                size_t n = (size_t)(b - v);
                if (n >= cap) n = cap - 1;
                memcpy(out, v, n);
                out[n] = 0;
                return true;
            }
        }
        p = *e ? e + 1 : e;
    }
    return false;
}

static int ini_int(const char *text, const char *sec, const char *key, int def, int lo, int hi) {
    char v[32];
    if (!ini_get(text, sec, key, v, sizeof(v)) || !v[0]) return def;
    int x = atoi(v);
    return x < lo ? lo : x > hi ? hi : x;
}

/* Percorre "a,b , c" chamando fn em cada item não vazio. */
static void each_item(const char *list, void (*fn)(void *, const char *, int), void *ud) {
    for (const char *p = list; *p;) {
        while (*p == ' ' || *p == ',') p++;
        const char *s = p;
        while (*p && *p != ',') p++;
        const char *e = p;
        while (e > s && e[-1] == ' ') e--;
        if (e > s) fn(ud, s, (int)(e - s));
    }
}

static void add_key(void *ud, const char *s, int n) {
    z3_keyset *k = ud;
    int i = z3_key_find(s, n);
    if (i >= 0 && k->n < Z3_MAX_BINDS) k->sc[k->n++] = (SDL_Scancode)z3_keys[i].hid;
}

static void add_pad(void *ud, const char *s, int n) {
    z3_padset *p = ud;
    char name[32];
    if (n >= (int)sizeof(name) || p->n >= Z3_MAX_BINDS) return;
    memcpy(name, s, (size_t)n);
    name[n] = 0;
    int sign = 1;
    if (n > 1 && (name[n - 1] == '+' || name[n - 1] == '-')) { sign = name[n - 1] == '-' ? -1 : 1; name[n - 1] = 0; }
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name);
    if (b != SDL_CONTROLLER_BUTTON_INVALID) {
        p->button[p->n] = b; p->axis[p->n] = -1; p->sign[p->n] = 0; p->n++;
        return;
    }
    SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(name);
    if (a != SDL_CONTROLLER_AXIS_INVALID) {
        p->button[p->n] = -1; p->axis[p->n] = a; p->sign[p->n] = sign; p->n++;
    }
}

static void read_keys(const char *text, const char *sec, const char *key, const char *def, z3_keyset *out) {
    char v[256];
    out->n = 0;
    each_item(ini_get(text, sec, key, v, sizeof(v)) ? v : def, add_key, out);
}
static void read_pad(const char *text, const char *sec, const char *key, const char *def, z3_padset *out) {
    char v[256];
    out->n = 0;
    each_item(ini_get(text, sec, key, v, sizeof(v)) ? v : def, add_pad, out);
}

void z3_config_load(z3_config *c, const char *path) {
    char *t = ini_load(path);
    memset(c, 0, sizeof(*c));
    for (int p = 0; p < 2; p++) {
        char ks[16], ps[16], g[8], v[64];
        snprintf(ks, sizeof(ks), "p%d_teclado", p + 1);
        snprintf(ps, sizeof(ps), "p%d_controle", p + 1);
        for (int i = 0; i < Z3_PLAYER_INPUTS; i++) {
            read_keys(t, ks, z3_player_keys[i], z3_player_kbd_default[p][i], &c->kbd[p][i]);
            read_pad(t, ps, z3_player_keys[i], z3_player_pad_default[i], &c->pad[p][i]);
        }
        snprintf(g, sizeof(g), "p%d", p + 1);
        if (ini_get(t, "controle", g, v, sizeof(v)) && strcmp(v, "auto")) snprintf(c->pad_guid[p], sizeof(c->pad_guid[p]), "%s", v);
    }
    for (int i = 0; i < Z3_HOTKEYS; i++) {
        read_keys(t, "atalhos_teclado", z3_hotkey_keys[i], z3_hotkey_kbd_default[i], &c->hk_kbd[i]);
        read_pad(t, "atalhos_controle", z3_hotkey_keys[i], z3_hotkey_pad_default[i], &c->hk_pad[i]);
    }
    c->analog = ini_int(t, "controle", "analogico", 1, 0, 1) != 0;
    c->fullscreen = ini_int(t, "video", "tela_cheia", 0, 0, 1) != 0;
    c->scale = ini_int(t, "video", "escala", 3, 2, 5);
    c->aspect = ini_int(t, "video", "proporcao", Z3_ASPECT_4_3, 0, 2);
    c->smooth = ini_int(t, "video", "filtro", 0, 0, 1) != 0;
    c->scanlines = ini_int(t, "video", "scanlines", 0, 0, 2);
    c->show_fps = ini_int(t, "video", "mostrar_fps", 1, 0, 1) != 0;
    c->volume = ini_int(t, "audio", "volume", 100, 0, 100);
    c->mute = ini_int(t, "audio", "mudo", 0, 0, 1) != 0;
    free(t);
}
