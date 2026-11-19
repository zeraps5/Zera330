/* SPDX-License-Identifier: GPL-2.0-or-later */
/* zera330 launcher (Windows): lista os jogos da pasta de jogos, roda cada um
 * pelo zera330c.exe, guarda o log de cada partida (user\logs\<set>.log) e o
 * tempo jogado, e edita as configurações (user\config\zera330.ini). Tudo fica
 * na pasta do launcher: o pacote é portátil. A estrutura (faixa de título,
 * lista, seis botões, barra de status, escala por DPI, atalho --jogar) segue
 * o launcher do ZeraPS5 (frontends/launcher/launcher.c, GPL-2.0-or-later). */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

#ifndef ZERA330_VERSION
#define ZERA330_VERSION "0.1.0"
#endif
#ifndef ZERA330_GIT_COMMIT
#define ZERA330_GIT_COMMIT "local"
#endif

#define APP_NAME L"zera330"
#define MAX_GAMES 1024
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

enum {
    ID_LIST = 100,
    ID_PLAY = 200, ID_LOG, ID_SAVES, ID_SHORTCUT, ID_REFRESH, ID_SETTINGS,
    ID_TIMER = 300,
    ID_SET_GAMES = 400, ID_SET_GAMES_BROWSE, ID_SET_BIOS, ID_SET_BIOS_BROWSE, ID_SET_INTRO, ID_SET_OK, ID_SET_CANCEL,
};

/* ---- Catálogo (gerado do MAME por tools/gen_game_list.py) ---- */
typedef struct { const char *set, *title, *year, *maker; } catalog_entry;
static const catalog_entry k_catalog[] = {
#include "games.inc"
};

static const catalog_entry *catalog_find(const char *set) {
    size_t lo = 0, hi = ARRAY_COUNT(k_catalog);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(set, k_catalog[mid].set);
        if (!c) return &k_catalog[mid];
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

/* ---- GDI+ (API plana, como no ZeraPS5): só pra carregar o logo PNG ---- */
typedef struct { UINT32 version; void *callback; BOOL no_thread, no_codecs; } gdip_input;
int WINAPI GdiplusStartup(ULONG_PTR *token, const gdip_input *input, void *output);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *file, void **bitmap);
int WINAPI GdipCreateHBITMAPFromBitmap(void *bitmap, HBITMAP *out, DWORD background);
int WINAPI GdipGetImageWidth(void *image, UINT *w);
int WINAPI GdipGetImageHeight(void *image, UINT *h);
int WINAPI GdipDisposeImage(void *image);

typedef struct {
    wchar_t set[32], title[256], year[8], maker[96], zip[MAX_PATH];
} game;

static game g_games[MAX_GAMES];
static int g_count;
static wchar_t g_base[MAX_PATH];      /* a pasta do launcher */
static wchar_t g_ini[MAX_PATH];       /* user\config\zera330.ini */
static wchar_t g_time_ini[MAX_PATH];  /* user\config\tempo.ini */
static wchar_t g_games_dir[MAX_PATH], g_bios_dir[MAX_PATH];
static bool g_intro = true;
static HWND g_main, g_list, g_status, g_banner, g_buttons[6];
static HFONT g_font, g_font_banner;
static HBITMAP g_logo;
static int g_logo_w, g_logo_h;
static int g_dpi = 96;
static PROCESS_INFORMATION g_proc;
static int g_proc_game = -1;
static ULONGLONG g_play_started;
static wchar_t g_autoplay[64];        /* --jogar <set>: atalho da área de trabalho */

static const wchar_t *const k_columns[] = {L"Jogo", L"Set", L"Ano", L"Fabricante", L"Tempo jogado", L"Arquivo"};
static const int k_widths[] = {420, 80, 55, 150, 100, 110};
#define PLAYTIME_COLUMN 4
static const wchar_t *const k_button_text[6] = {L"Jogar", L"Ver log", L"Pasta de saves",
                                                L"Criar atalho", L"Recarregar", L"Configurações"};

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static bool file_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dir_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static void status(const wchar_t *text) { SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)text); }

/* Caminho da configuração: relativo à pasta do launcher quando está dentro dela. */
static void full_path(const wchar_t *value, wchar_t *out) {
    if (!value[0]) { out[0] = 0; return; }
    if (value[1] == L':' || (value[0] == L'\\' && value[1] == L'\\')) { wcsncpy(out, value, MAX_PATH - 1); out[MAX_PATH - 1] = 0; return; }
    wchar_t tmp[MAX_PATH];
    swprintf(tmp, MAX_PATH, L"%ls\\%ls", g_base, value);
    if (!GetFullPathNameW(tmp, MAX_PATH, out, NULL)) wcscpy(out, tmp);
}
static void relative_path(const wchar_t *path, wchar_t *out) {
    size_t n = wcslen(g_base);
    if (!_wcsnicmp(path, g_base, n) && path[n] == L'\\') wcscpy(out, path + n + 1);
    else wcscpy(out, path);
}

/* Sem configuração: procura roms\zips e roms\neogeo na pasta do launcher e
 * até 4 pastas acima (o build de desenvolvimento fica em build\bin). */
static void default_folder(const wchar_t *sub, wchar_t *out) {
    wchar_t dir[MAX_PATH], cand[MAX_PATH];
    wcscpy(dir, g_base);
    for (int up = 0; up <= 4; up++) {
        swprintf(cand, MAX_PATH, L"%ls\\%ls", dir, sub);
        if (dir_exists(cand)) { GetFullPathNameW(cand, MAX_PATH, out, NULL); return; }
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (!slash) break;
        *slash = 0;
    }
    swprintf(out, MAX_PATH, L"%ls\\%ls", g_base, sub);
}

static void load_settings(void) {
    wchar_t v[MAX_PATH];
    GetPrivateProfileStringW(L"pastas", L"jogos", L"", v, MAX_PATH, g_ini);
    if (v[0]) full_path(v, g_games_dir); else default_folder(L"roms\\zips", g_games_dir);
    GetPrivateProfileStringW(L"pastas", L"bios", L"", v, MAX_PATH, g_ini);
    if (v[0]) full_path(v, g_bios_dir); else default_folder(L"roms\\neogeo", g_bios_dir);
    g_intro = GetPrivateProfileIntW(L"geral", L"abertura", 1, g_ini) != 0;
}

static bool save_settings(void) {
    wchar_t rel[MAX_PATH];
    bool ok = true;
    relative_path(g_games_dir, rel);
    ok &= WritePrivateProfileStringW(L"pastas", L"jogos", rel, g_ini) != 0;
    relative_path(g_bios_dir, rel);
    ok &= WritePrivateProfileStringW(L"pastas", L"bios", rel, g_ini) != 0;
    ok &= WritePrivateProfileStringW(L"geral", L"abertura", g_intro ? L"1" : L"0", g_ini) != 0;
    return ok;
}

/* ---- Tempo jogado: user\config\tempo.ini, [tempo] <set>=segundos ---- */
static unsigned playtime_get(const wchar_t *set) { return (unsigned)GetPrivateProfileIntW(L"tempo", set, 0, g_time_ini); }
static void playtime_add(const wchar_t *set, unsigned seconds) {
    wchar_t v[32];
    swprintf(v, 32, L"%u", playtime_get(set) + seconds);
    WritePrivateProfileStringW(L"tempo", set, v, g_time_ini);
}
static void set_playtime_cell(int row, unsigned seconds) {
    wchar_t text[40];
    swprintf(text, ARRAY_COUNT(text), L"%u:%02u:%02u", seconds / 3600u, (seconds / 60u) % 60u, seconds % 60u);
    ListView_SetItemText(g_list, row, PLAYTIME_COLUMN, text);
}

/* ---- Lista de jogos ---- */
static int compare_games(const void *a, const void *b) {
    return _wcsicmp(((const game *)a)->title, ((const game *)b)->title);
}

static void scan_games(void) {
    g_count = 0;
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*.zip", g_games_dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (g_count >= MAX_GAMES) break;
        wchar_t set[32];
        wcsncpy(set, fd.cFileName, 31);
        set[31] = 0;
        wchar_t *dot = wcsrchr(set, L'.');
        if (dot) *dot = 0;
        if (!_wcsicmp(set, L"neogeo")) continue; /* a BIOS não é jogo */
        game *g = &g_games[g_count++];
        memset(g, 0, sizeof(*g));
        wcscpy(g->set, set);
        swprintf(g->zip, MAX_PATH, L"%ls\\%ls", g_games_dir, fd.cFileName);
        char a[32];
        WideCharToMultiByte(CP_UTF8, 0, set, -1, a, sizeof(a), NULL, NULL);
        const catalog_entry *e = catalog_find(a);
        if (e) {
            MultiByteToWideChar(CP_UTF8, 0, e->title, -1, g->title, ARRAY_COUNT(g->title));
            MultiByteToWideChar(CP_UTF8, 0, e->year, -1, g->year, ARRAY_COUNT(g->year));
            MultiByteToWideChar(CP_UTF8, 0, e->maker, -1, g->maker, ARRAY_COUNT(g->maker));
        } else {
            wcscpy(g->title, set);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(g_games, (size_t)g_count, sizeof(game), compare_games);
}

static void fill_list(void) {
    ListView_DeleteAllItems(g_list);
    scan_games();
    for (int i = 0; i < g_count; i++) {
        LVITEMW it = {0};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = g_games[i].title;
        it.lParam = i;
        ListView_InsertItem(g_list, &it);
        ListView_SetItemText(g_list, i, 1, g_games[i].set);
        ListView_SetItemText(g_list, i, 2, g_games[i].year);
        ListView_SetItemText(g_list, i, 3, g_games[i].maker);
        set_playtime_cell(i, playtime_get(g_games[i].set));
        const wchar_t *file = wcsrchr(g_games[i].zip, L'\\'); /* o arquivo, sem a pasta (ela está em Configurações) */
        ListView_SetItemText(g_list, i, 5, (wchar_t *)(file ? file + 1 : g_games[i].zip));
    }
    if (g_count) ListView_SetItemState(g_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvalidateRect(g_banner, NULL, TRUE);
}

static int selected_game(void) {
    int item = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (item < 0) return -1;
    LVITEMW it = {0};
    it.mask = LVIF_PARAM;
    it.iItem = item;
    ListView_GetItem(g_list, &it);
    return (int)it.lParam;
}

/* ---- Rodar um jogo ---- */
static bool game_running(void) {
    return g_proc.hProcess && WaitForSingleObject(g_proc.hProcess, 0) == WAIT_TIMEOUT;
}

static void log_path(const game *g, wchar_t *out) { swprintf(out, MAX_PATH, L"%ls\\user\\logs\\%ls.log", g_base, g->set); }

static void run_game(int gi) {
    if (game_running()) {
        MessageBoxW(g_main, L"Já tem um jogo rodando. Feche a janela dele antes de abrir outro.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    const game *g = &g_games[gi];
    wchar_t exe[MAX_PATH], cmd[3 * MAX_PATH + 16], logp[MAX_PATH], user[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\zera330c.exe", g_base);
    if (!file_exists(exe)) {
        MessageBoxW(g_main, L"zera330c.exe não está na pasta do launcher.", APP_NAME, MB_ICONERROR);
        return;
    }
    if (!dir_exists(g_bios_dir)) {
        MessageBoxW(g_main, L"A pasta da BIOS não existe. Escolha a pasta em Configurações.", APP_NAME, MB_ICONWARNING);
        return;
    }
    swprintf(cmd, ARRAY_COUNT(cmd), L"\"%ls\" \"%ls\" \"%ls\"", exe, g->zip, g_bios_dir);
    swprintf(user, MAX_PATH, L"%ls\\user", g_base);
    log_path(g, logp);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE log = CreateFileW(logp, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = log;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    SetEnvironmentVariableW(L"ZERA330_SEM_INTRO", g_intro ? NULL : L"1");
    SetEnvironmentVariableW(L"ZERA330_CONFIG", g_ini);       /* controles, vídeo e som */
    SetEnvironmentVariableW(L"ZERA330_TITULO", g->title);    /* título da janela do jogo */
    /* nvram\ e states\ do emulador ficam dentro de user\; CREATE_NO_WINDOW:
     * sem a janela preta de console (a saída vai pro log) */
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, log != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, NULL, user, &si, &g_proc);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        memset(&g_proc, 0, sizeof(g_proc));
        MessageBoxW(g_main, L"Não foi possível iniciar o jogo.", APP_NAME, MB_ICONERROR);
        return;
    }
    CloseHandle(g_proc.hThread);
    g_proc.hThread = NULL;
    g_proc_game = gi;
    g_play_started = GetTickCount64();
    wchar_t msg[400];
    swprintf(msg, ARRAY_COUNT(msg), L"Jogando: %ls", g->title);
    status(msg);
}

static void poll_game(void) {
    if (!g_proc.hProcess || game_running()) return;
    DWORD code = 0;
    GetExitCodeProcess(g_proc.hProcess, &code);
    CloseHandle(g_proc.hProcess);
    memset(&g_proc, 0, sizeof(g_proc));
    if (g_proc_game >= 0) {
        const game *g = &g_games[g_proc_game];
        playtime_add(g->set, (unsigned)((GetTickCount64() - g_play_started) / 1000u));
        for (int row = 0; row < ListView_GetItemCount(g_list); row++) {
            LVITEMW it = {0};
            it.mask = LVIF_PARAM;
            it.iItem = row;
            ListView_GetItem(g_list, &it);
            if ((int)it.lParam == g_proc_game) set_playtime_cell(row, playtime_get(g->set));
        }
        wchar_t msg[400];
        if (code == 0) swprintf(msg, ARRAY_COUNT(msg), L"%ls fechado.", g->title);
        else swprintf(msg, ARRAY_COUNT(msg), L"%ls terminou com erro (código %lu). Veja o log.", g->title, code);
        status(msg);
    }
    g_proc_game = -1;
}

static void view_log(const game *g) {
    wchar_t p[MAX_PATH];
    log_path(g, p);
    if (!file_exists(p)) { MessageBoxW(g_main, L"Esse jogo ainda não tem log: jogue uma vez.", APP_NAME, MB_ICONINFORMATION); return; }
    ShellExecuteW(g_main, L"open", L"notepad.exe", p, NULL, SW_SHOWNORMAL);
}

static void open_saves(void) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\user", g_base);
    ShellExecuteW(g_main, L"open", p, NULL, NULL, SW_SHOWNORMAL);
}

/* Atalho na área de trabalho: Zera330.exe --jogar <set> (como no ZeraPS5). */
static void create_shortcut(const game *g) {
    static const GUID clsid_shell_link = {0x00021401, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
    static const GUID iid_shell_link = {0x000214F9, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
    static const GUID iid_persist_file = {0x0000010B, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
    wchar_t exe[MAX_PATH], desktop[MAX_PATH], args[64], name[200], link[MAX_PATH + 220];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) desktop[0] = 0;
    size_t n = 0;
    for (const wchar_t *q = g->title; *q && n + 1 < 120; q++)
        name[n++] = wcschr(L"\\/:*?\"<>|", *q) || *q < 32 ? L' ' : *q;
    name[n] = 0;
    swprintf(args, ARRAY_COUNT(args), L"--jogar %ls", g->set);
    swprintf(link, ARRAY_COUNT(link), L"%ls\\%ls.lnk", desktop, name);
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    bool ok = desktop[0] && SUCCEEDED(CoCreateInstance(&clsid_shell_link, NULL, CLSCTX_INPROC_SERVER, &iid_shell_link, (void **)&sl));
    if (ok) {
        sl->lpVtbl->SetPath(sl, exe);
        sl->lpVtbl->SetArguments(sl, args);
        sl->lpVtbl->SetWorkingDirectory(sl, g_base);
        sl->lpVtbl->SetIconLocation(sl, exe, 0);
        sl->lpVtbl->SetDescription(sl, g->title);
        ok = SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &iid_persist_file, (void **)&pf)) &&
             SUCCEEDED(pf->lpVtbl->Save(pf, link, TRUE));
        if (pf) pf->lpVtbl->Release(pf);
        sl->lpVtbl->Release(sl);
    }
    status(ok ? L"Atalho criado na área de trabalho." : L"Não foi possível criar o atalho.");
}

/* --jogar <set>: seleciona o jogo do atalho e começa */
static void autoplay(void) {
    if (!g_autoplay[0]) return;
    for (int row = 0; row < ListView_GetItemCount(g_list); row++) {
        LVITEMW it = {0};
        it.mask = LVIF_PARAM;
        it.iItem = row;
        ListView_GetItem(g_list, &it);
        if (_wcsicmp(g_games[it.lParam].set, g_autoplay)) continue;
        ListView_SetItemState(g_list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g_list, row, FALSE);
        g_autoplay[0] = 0;
        PostMessageW(g_main, WM_COMMAND, ID_PLAY, 0);
        return;
    }
    wchar_t msg[200];
    swprintf(msg, ARRAY_COUNT(msg), L"O jogo do atalho não está na pasta de jogos: %ls", g_autoplay);
    g_autoplay[0] = 0;
    MessageBoxW(g_main, msg, APP_NAME, MB_ICONWARNING);
}

/* ---- Janelas de diálogo modais (como no ZeraPS5) ---- */
static HWND add_control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id);

static HWND create_dialog(const wchar_t *cls, const wchar_t *title, int w, int h, HWND owner) {
    RECT r = {0, 0, S(w), S(h)};
    AdjustWindowRectEx(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    RECT m;
    GetWindowRect(owner, &m);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = m.left + ((m.right - m.left) - ww) / 2, y = m.top + ((m.bottom - m.top) - wh) / 2;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (x + ww > work.right) x = work.right - ww;
    if (y + wh > work.bottom) y = work.bottom - wh;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    return CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title, WS_CAPTION | WS_SYSMENU,
                           x, y, ww, wh, owner, NULL, GetModuleHandleW(NULL), NULL);
}

/* dialog_keys: Tab/Enter/Esc navegam (falso na captura de tecla, que quer todas) */
static void run_modal(HWND w, HWND owner, bool dialog_keys) {
    ShowWindow(w, SW_SHOW);
    EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(w) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (dialog_keys && IsDialogMessageW(w, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

static HWND add_combo(HWND parent, int x, int y, int w, const wchar_t *const *items, int n, int sel) {
    HWND c = add_control(parent, WC_COMBOBOXW, NULL, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, 300, 0);
    for (int i = 0; i < n; i++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(c, CB_SETCURSEL, (WPARAM)(sel < 0 || sel >= n ? 0 : sel), 0);
    return c;
}
static int ini_int(const wchar_t *sec, const wchar_t *key, int def, int lo, int hi) {
    int v = (int)GetPrivateProfileIntW(sec, key, def, g_ini);
    return v < lo ? lo : v > hi ? hi : v;
}
static void ini_put_int(const wchar_t *sec, const wchar_t *key, int v) {
    wchar_t t[16];
    swprintf(t, 16, L"%d", v);
    WritePrivateProfileStringW(sec, key, t, g_ini);
}

/* ---- Controles: SDL2.dll (o mesmo do jogo), carregado só quando a tela de
 * controles abre — o launcher vê o controle exatamente como o jogo vai ver. ---- */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../../platform/common/zera330_input.h"

static struct {
    HMODULE dll;
    SDL_bool (SDLCALL *SetHint)(const char *, const char *);
    int (SDLCALL *Init)(Uint32);
    void (SDLCALL *QuitSubSystem)(Uint32);
    void (SDLCALL *PumpEvents)(void);
    int (SDLCALL *NumJoysticks)(void);
    SDL_bool (SDLCALL *IsGameController)(int);
    SDL_GameController *(SDLCALL *Open)(int);
    void (SDLCALL *Close)(SDL_GameController *);
    Uint8 (SDLCALL *GetButton)(SDL_GameController *, SDL_GameControllerButton);
    Sint16 (SDLCALL *GetAxis)(SDL_GameController *, SDL_GameControllerAxis);
    const char *(SDLCALL *NameForIndex)(int);
    SDL_JoystickGUID (SDLCALL *DeviceGUID)(int);
    void (SDLCALL *GUIDString)(SDL_JoystickGUID, char *, int);
    const char *(SDLCALL *ButtonName)(SDL_GameControllerButton);
    const char *(SDLCALL *AxisName)(SDL_GameControllerAxis);
    SDL_GameControllerButton (SDLCALL *ButtonFromName)(const char *);
    SDL_GameControllerAxis (SDLCALL *AxisFromName)(const char *);
    bool ready;
} sdl;

#define MAX_PADS 8
static SDL_GameController *g_pads[MAX_PADS];
static char g_pad_guid[MAX_PADS][40];
static wchar_t g_pad_name[MAX_PADS][128];
static int g_pad_count, g_pad_joys = -1;

static bool pads_start(void) {
    if (sdl.ready) return true;
    if (!sdl.dll) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\SDL2.dll", g_base);
        sdl.dll = LoadLibraryW(p);
        if (!sdl.dll) return false;
#define LOAD(field, name) *(FARPROC *)&sdl.field = GetProcAddress(sdl.dll, name); if (!sdl.field) return false
        LOAD(SetHint, "SDL_SetHint"); LOAD(Init, "SDL_Init"); LOAD(QuitSubSystem, "SDL_QuitSubSystem");
        LOAD(PumpEvents, "SDL_PumpEvents"); LOAD(NumJoysticks, "SDL_NumJoysticks");
        LOAD(IsGameController, "SDL_IsGameController"); LOAD(Open, "SDL_GameControllerOpen");
        LOAD(Close, "SDL_GameControllerClose"); LOAD(GetButton, "SDL_GameControllerGetButton");
        LOAD(GetAxis, "SDL_GameControllerGetAxis"); LOAD(NameForIndex, "SDL_GameControllerNameForIndex");
        LOAD(DeviceGUID, "SDL_JoystickGetDeviceGUID"); LOAD(GUIDString, "SDL_JoystickGetGUIDString");
        LOAD(ButtonName, "SDL_GameControllerGetStringForButton"); LOAD(AxisName, "SDL_GameControllerGetStringForAxis");
        LOAD(ButtonFromName, "SDL_GameControllerGetButtonFromString");
        LOAD(AxisFromName, "SDL_GameControllerGetAxisFromString");
#undef LOAD
    }
    sdl.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1"); /* o launcher não tem janela SDL */
    sdl.ready = sdl.Init(SDL_INIT_GAMECONTROLLER) == 0;
    g_pad_joys = -1;
    return sdl.ready;
}

static void pads_close(void) {
    for (int i = 0; i < g_pad_count; i++) if (g_pads[i]) sdl.Close(g_pads[i]);
    memset(g_pads, 0, sizeof(g_pads));
    g_pad_count = 0;
}

static void pads_stop(void) {
    if (!sdl.ready) return;
    pads_close();
    sdl.QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    sdl.ready = false;
}

/* Atualiza o estado; true se a lista de controles mudou (liga/desliga). */
static bool pads_poll(void) {
    if (!sdl.ready) return false;
    sdl.PumpEvents();
    int n = sdl.NumJoysticks();
    if (n == g_pad_joys) return false;
    pads_close();
    g_pad_joys = n;
    for (int j = 0; j < n && g_pad_count < MAX_PADS; j++) {
        if (!sdl.IsGameController(j)) continue;
        SDL_GameController *gc = sdl.Open(j);
        if (!gc) continue;
        g_pads[g_pad_count] = gc;
        sdl.GUIDString(sdl.DeviceGUID(j), g_pad_guid[g_pad_count], 40);
        const char *nm = sdl.NameForIndex(j);
        MultiByteToWideChar(CP_UTF8, 0, nm ? nm : "Controle", -1, g_pad_name[g_pad_count], 128);
        g_pad_count++;
    }
    return true;
}

/* Qual controle é de cada jogador: igual ao emulador (open_pads em main.c). */
static int pad_of_player(int player, char guid[2][40]) {
    int owner[MAX_PADS];
    for (int i = 0; i < MAX_PADS; i++) owner[i] = -1;
    for (int p = 0; p < 2; p++) {
        if (!guid[p][0]) continue;
        for (int i = 0; i < g_pad_count; i++)
            if (owner[i] < 0 && !strcmp(g_pad_guid[i], guid[p])) { owner[i] = p; break; }
    }
    for (int p = 0; p < 2; p++) {
        if (guid[p][0]) continue;
        for (int i = 0; i < g_pad_count; i++) if (owner[i] < 0) { owner[i] = p; break; }
    }
    for (int i = 0; i < g_pad_count; i++) if (owner[i] == player) return i;
    return -1;
}

/* "a", "dpup", "leftx-", "lefttrigger": está apertado neste controle? */
static bool pad_token_down(int pad, const char *tok) {
    if (pad < 0 || !sdl.ready) return false;
    char name[32];
    snprintf(name, sizeof(name), "%s", tok);
    size_t n = strlen(name);
    int sign = 1;
    if (n > 1 && (name[n - 1] == '+' || name[n - 1] == '-')) { sign = name[n - 1] == '-' ? -1 : 1; name[n - 1] = 0; }
    SDL_GameControllerButton b = sdl.ButtonFromName(name);
    if (b != SDL_CONTROLLER_BUTTON_INVALID) return sdl.GetButton(g_pads[pad], b) != 0;
    SDL_GameControllerAxis a = sdl.AxisFromName(name);
    return a != SDL_CONTROLLER_AXIS_INVALID && sdl.GetAxis(g_pads[pad], a) * sign > 16000;
}

/* ---- Nomes amigáveis de teclas e botões ---- */
static void key_label(const char *tok, wchar_t *out, int cap) {
    int i = z3_key_find(tok, (int)strlen(tok));
    out[0] = 0;
    if (i >= 0) {
        int ps2 = z3_keys[i].ps2;
        LONG lp = ((LONG)(ps2 & 0xFF) << 16) | ((ps2 & 0x100) ? (1L << 24) : 0);
        if (GetKeyNameTextW(lp, out, cap) > 0) {
            if (out[0] && !out[1]) out[0] = towupper(out[0]);
            return;
        }
    }
    MultiByteToWideChar(CP_UTF8, 0, tok, -1, out, cap);
    CharUpperW(out);
}

static void pad_label(const char *tok, wchar_t *out, int cap) {
    static const struct { const char *tok; const wchar_t *name; } k[] = {
        {"a", L"A"}, {"b", L"B"}, {"x", L"X"}, {"y", L"Y"}, {"back", L"Back / Select"}, {"guide", L"Guia"},
        {"start", L"Start"}, {"leftstick", L"L3"}, {"rightstick", L"R3"}, {"leftshoulder", L"LB / L1"},
        {"rightshoulder", L"RB / R1"}, {"dpup", L"Direcional ↑"}, {"dpdown", L"Direcional ↓"},
        {"dpleft", L"Direcional ←"}, {"dpright", L"Direcional →"}, {"misc1", L"Misc"}, {"touchpad", L"Touchpad"},
        {"paddle1", L"Pá 1"}, {"paddle2", L"Pá 2"}, {"paddle3", L"Pá 3"}, {"paddle4", L"Pá 4"},
        {"lefttrigger", L"LT / L2"}, {"righttrigger", L"RT / R2"},
        {"leftx-", L"Analógico esq. ←"}, {"leftx+", L"Analógico esq. →"}, {"lefty-", L"Analógico esq. ↑"},
        {"lefty+", L"Analógico esq. ↓"}, {"rightx-", L"Analógico dir. ←"}, {"rightx+", L"Analógico dir. →"},
        {"righty-", L"Analógico dir. ↑"}, {"righty+", L"Analógico dir. ↓"},
    };
    for (size_t i = 0; i < ARRAY_COUNT(k); i++)
        if (!strcmp(k[i].tok, tok)) { swprintf(out, (size_t)cap, L"%ls", k[i].name); return; }
    MultiByteToWideChar(CP_UTF8, 0, tok, -1, out, cap);
}

/* "up,w" -> "↑ / W" (nomes no idioma do teclado do usuário) */
static void binds_text(const char *list, bool pad, wchar_t *out, int cap) {
    out[0] = 0;
    char tok[32];
    for (const char *p = list; *p;) {
        while (*p == ',' || *p == ' ') p++;
        size_t n = 0;
        while (p[n] && p[n] != ',') n++;
        if (!n) break;
        snprintf(tok, sizeof(tok), "%.*s", (int)(n < 31 ? n : 31), p);
        p += n;
        wchar_t one[64];
        if (pad) pad_label(tok, one, 64); else key_label(tok, one, 64);
        if (out[0] && wcslen(out) + 4 < (size_t)cap) wcscat(out, L" / ");
        if (wcslen(out) + wcslen(one) + 1 < (size_t)cap) wcscat(out, one);
    }
    if (!out[0]) swprintf(out, (size_t)cap, L"—");
}

/* ---- Tela de controles ---- */
enum { CT_P1, CT_P2, CT_HOTKEYS, CT_TABS, CT_MAX_ROWS = Z3_PLAYER_INPUTS };
enum {
    ID_CT_TAB = 500, ID_CT_PANEL, ID_CT_LIST, ID_CT_SET, ID_CT_ADD, ID_CT_CLEAR, ID_CT_DEFAULTS, ID_CT_PAD,
    ID_CT_ANALOG, ID_CT_CLEAR_KEY, ID_CT_CLEAR_PAD, ID_CT_TIMER = 530, ID_CAP_TIMER,
};
static const wchar_t *const k_ct_tabs[CT_TABS] = {L"Jogador 1", L"Jogador 2", L"Atalhos"};
static const wchar_t *const k_ct_sec_kbd[CT_TABS] = {L"p1_teclado", L"p2_teclado", L"atalhos_teclado"};
static const wchar_t *const k_ct_sec_pad[CT_TABS] = {L"p1_controle", L"p2_controle", L"atalhos_controle"};
static const wchar_t *const k_player_labels[Z3_PLAYER_INPUTS] = {
    L"Cima", L"Baixo", L"Esquerda", L"Direita", L"Botão A", L"Botão B", L"Botão C", L"Botão D", L"Start", L"Moeda (ficha)"};
static const wchar_t *const k_hotkey_labels[Z3_HOTKEYS] = {
    L"Botão de serviço (Service)", L"Pausar / continuar", L"Reiniciar o jogo", L"Salvar estado",
    L"Carregar estado", L"Tela cheia", L"Captura de tela", L"Sair do jogo"};

static char g_ct_kbd[CT_TABS][CT_MAX_ROWS][96], g_ct_pad[CT_TABS][CT_MAX_ROWS][96];
static char g_ct_guid[2][40];
static char g_ct_combo_guid[MAX_PADS + 2][40];
static bool g_ct_analog;
static int g_ct_tab, g_ct_capture_row = -1;
static unsigned g_ct_pressed;
static HWND g_ct, g_ct_tabctl, g_ct_panel, g_ct_list, g_ct_pad_label, g_ct_pad_combo, g_ct_analog_box, g_ct_hint,
    g_ct_warn, g_ct_pads_text, g_ct_hotkey_text;
static HFONT g_font_bold, g_font_small;

static int ct_rows(int tab) { return tab == CT_HOTKEYS ? Z3_HOTKEYS : Z3_PLAYER_INPUTS; }
static const char *ct_key(int tab, int i) { return tab == CT_HOTKEYS ? z3_hotkey_keys[i] : z3_player_keys[i]; }
static const wchar_t *ct_label(int tab, int i) { return tab == CT_HOTKEYS ? k_hotkey_labels[i] : k_player_labels[i]; }
static const char *ct_default(int tab, int i, bool pad) {
    if (tab == CT_HOTKEYS) return pad ? z3_hotkey_pad_default[i] : z3_hotkey_kbd_default[i];
    return pad ? z3_player_pad_default[i] : z3_player_kbd_default[tab][i];
}

static void ct_load(void) {
    for (int t = 0; t < CT_TABS; t++)
        for (int i = 0; i < ct_rows(t); i++)
            for (int pad = 0; pad < 2; pad++) {
                wchar_t key[32], v[96];
                MultiByteToWideChar(CP_UTF8, 0, ct_key(t, i), -1, key, 32);
                GetPrivateProfileStringW(pad ? k_ct_sec_pad[t] : k_ct_sec_kbd[t], key, L"\x01", v, 96, g_ini);
                char *dst = pad ? g_ct_pad[t][i] : g_ct_kbd[t][i];
                if (v[0] == 1) snprintf(dst, 96, "%s", ct_default(t, i, pad != 0));
                else WideCharToMultiByte(CP_UTF8, 0, v, -1, dst, 96, NULL, NULL);
            }
    for (int p = 0; p < 2; p++) {
        wchar_t v[40];
        GetPrivateProfileStringW(L"controle", p ? L"p2" : L"p1", L"auto", v, 40, g_ini);
        WideCharToMultiByte(CP_UTF8, 0, v, -1, g_ct_guid[p], 40, NULL, NULL);
        if (!strcmp(g_ct_guid[p], "auto")) g_ct_guid[p][0] = 0;
    }
    g_ct_analog = ini_int(L"controle", L"analogico", 1, 0, 1) != 0;
}

static void ct_save(void) {
    for (int t = 0; t < CT_TABS; t++)
        for (int i = 0; i < ct_rows(t); i++)
            for (int pad = 0; pad < 2; pad++) {
                wchar_t key[32], v[96];
                MultiByteToWideChar(CP_UTF8, 0, ct_key(t, i), -1, key, 32);
                MultiByteToWideChar(CP_UTF8, 0, pad ? g_ct_pad[t][i] : g_ct_kbd[t][i], -1, v, 96);
                WritePrivateProfileStringW(pad ? k_ct_sec_pad[t] : k_ct_sec_kbd[t], key, v, g_ini);
            }
    for (int p = 0; p < 2; p++) {
        wchar_t v[40];
        MultiByteToWideChar(CP_UTF8, 0, g_ct_guid[p][0] ? g_ct_guid[p] : "auto", -1, v, 40);
        WritePrivateProfileStringW(L"controle", p ? L"p2" : L"p1", v, g_ini);
    }
    ini_put_int(L"controle", L"analogico", g_ct_analog ? 1 : 0);
}

static bool list_has(const char *list, const char *tok) {
    size_t n = strlen(tok);
    for (const char *p = list; *p;) {
        while (*p == ',' || *p == ' ') p++;
        size_t m = 0;
        while (p[m] && p[m] != ',') m++;
        if (m == n && !strncmp(p, tok, n)) return true;
        p += m;
    }
    return false;
}

/* ---- O painel: o controle do fliperama desenhado, aceso ao vivo ---- */
typedef struct { float x, y; } gp_point;
int WINAPI GdipCreateFromHDC(HDC dc, void **g);
int WINAPI GdipDeleteGraphics(void *g);
int WINAPI GdipSetSmoothingMode(void *g, int mode);
int WINAPI GdipCreateSolidFill(DWORD argb, void **brush);
int WINAPI GdipDeleteBrush(void *brush);
int WINAPI GdipCreatePen1(DWORD argb, float width, int unit, void **pen);
int WINAPI GdipDeletePen(void *pen);
int WINAPI GdipSetPenDashStyle(void *pen, int style);
int WINAPI GdipFillEllipse(void *g, void *brush, float x, float y, float w, float h);
int WINAPI GdipDrawEllipse(void *g, void *pen, float x, float y, float w, float h);
int WINAPI GdipCreatePath(int fill_mode, void **path);
int WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipClosePathFigure(void *path);
int WINAPI GdipFillPath(void *g, void *brush, void *path);
int WINAPI GdipDrawPath(void *g, void *pen, void *path);
int WINAPI GdipDeletePath(void *path);
int WINAPI GdipFillPolygon(void *g, void *brush, const gp_point *pts, int count, int fill_mode);
int WINAPI GdipDrawLine(void *g, void *pen, float x1, float y1, float x2, float y2);
int WINAPI GdipSetPenStartCap(void *pen, int cap);
int WINAPI GdipSetPenEndCap(void *pen, int cap);

#define PANEL_W 300
#define PANEL_H 250
static const struct { float x, y; DWORD color; const wchar_t *letter; } k_panel_buttons[4] = {
    {160, 158, 0xFFE23B3B, L"A"}, {200, 144, 0xFFF2C200, L"B"}, {240, 136, 0xFF2FB344, L"C"}, {279, 140, 0xFF2D7FF9, L"D"}};
#define STICK_X 78.0f
#define STICK_Y 140.0f
#define STICK_R 48.0f
#define BUTTON_R 17.0f
static const RECT k_pill[2] = {{150, 16, 212, 38}, {222, 16, 284, 38}}; /* moeda, start */

static void gp_fill_circle(void *g, DWORD argb, float cx, float cy, float r, float k) {
    void *b;
    GdipCreateSolidFill(argb, &b);
    GdipFillEllipse(g, b, (cx - r) * k, (cy - r) * k, 2 * r * k, 2 * r * k);
    GdipDeleteBrush(b);
}
static void gp_ring(void *g, DWORD argb, float cx, float cy, float r, float width, float k, bool dashed) {
    void *p;
    GdipCreatePen1(argb, width * k, 2, &p);
    if (dashed) GdipSetPenDashStyle(p, 1);
    GdipDrawEllipse(g, p, (cx - r) * k, (cy - r) * k, 2 * r * k, 2 * r * k);
    GdipDeletePen(p);
}
static void gp_round_rect(void *g, DWORD fill, DWORD line, float x, float y, float w, float h, float rad, float k) {
    void *path, *b, *p;
    x *= k; y *= k; w *= k; h *= k; rad *= k;
    GdipCreatePath(0, &path);
    GdipAddPathArc(path, x, y, 2 * rad, 2 * rad, 180, 90);
    GdipAddPathArc(path, x + w - 2 * rad, y, 2 * rad, 2 * rad, 270, 90);
    GdipAddPathArc(path, x + w - 2 * rad, y + h - 2 * rad, 2 * rad, 2 * rad, 0, 90);
    GdipAddPathArc(path, x, y + h - 2 * rad, 2 * rad, 2 * rad, 90, 90);
    GdipClosePathFigure(path);
    GdipCreateSolidFill(fill, &b);
    GdipFillPath(g, b, path);
    GdipDeleteBrush(b);
    if (line) {
        GdipCreatePen1(line, 1.5f * k, 2, &p);
        GdipDrawPath(g, p, path);
        GdipDeletePen(p);
    }
    GdipDeletePath(path);
}
static DWORD lighten(DWORD argb) {
    DWORD r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    r += (255 - r) / 2; g += (255 - g) / 2; b += (255 - b) / 2;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
static DWORD darken(DWORD argb) {
    DWORD r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return 0xFF000000u | ((r * 3 / 5) << 16) | ((g * 3 / 5) << 8) | (b * 3 / 5);
}

static void panel_text(HDC dc, const wchar_t *t, float x, float y, float w, float h, float k, COLORREF c, HFONT f, UINT fmt) {
    RECT r = {(LONG)(x * k), (LONG)(y * k), (LONG)((x + w) * k), (LONG)((y + h) * k)};
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

/* primeira tecla da lista, pro rótulo embaixo de cada botão */
static void first_key(const char *list, wchar_t *out, int cap) {
    char tok[32] = "";
    size_t n = 0;
    while (list[n] && list[n] != ',' && n < 31) { tok[n] = list[n]; n++; }
    tok[n] = 0;
    if (tok[0]) key_label(tok, out, cap); else swprintf(out, (size_t)cap, L"—");
}

static void paint_panel(const DRAWITEMSTRUCT *d) {
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC mem = CreateCompatibleDC(d->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ oldb = SelectObject(mem, bmp);
    FillRect(mem, &(RECT){0, 0, w, h}, GetSysColorBrush(COLOR_BTNFACE));
    const float k = (float)w / PANEL_W;
    const int p = g_ct_tab == CT_P2 ? 1 : 0;
    const unsigned on = g_ct_pressed;
    void *g = NULL;
    GdipCreateFromHDC(mem, &g);
    GdipSetSmoothingMode(g, 4); /* antialias */
    gp_round_rect(g, 0xFF14161A, 0xFF2A2D33, 1, 1, PANEL_W - 2, PANEL_H - 2, 14, k);
    /* moeda e start */
    for (int i = 0; i < 2; i++) {
        int bit = i ? Z3_START : Z3_COIN;
        bool lit = (on >> bit) & 1;
        const RECT *r = &k_pill[i];
        gp_round_rect(g, lit ? 0xFFFFE600 : 0xFF3A3D44, g_ct_capture_row == bit ? 0xFFFFE600 : 0,
                      (float)r->left, (float)r->top, (float)(r->right - r->left), (float)(r->bottom - r->top), 11, k);
    }
    /* alavanca: base, setas de direção, bola deslocada pro lado apertado */
    gp_fill_circle(g, 0xFF23262C, STICK_X, STICK_Y, STICK_R, k);
    gp_ring(g, 0xFF3A3D44, STICK_X, STICK_Y, STICK_R, 2, k, false);
    static const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (int i = 0; i < 4; i++) {
        bool lit = (on >> i) & 1;
        float dx = dirs[i][0], dy = dirs[i][1], cx = STICK_X + dx * (STICK_R - 9), cy = STICK_Y + dy * (STICK_R - 9);
        gp_point tri[3] = {{(cx + dx * 6) * k, (cy + dy * 6) * k},
                           {(cx - dx * 3 + dy * 6) * k, (cy - dy * 3 + dx * 6) * k},
                           {(cx - dx * 3 - dy * 6) * k, (cy - dy * 3 - dx * 6) * k}};
        void *b;
        GdipCreateSolidFill(lit ? 0xFF00C8FF : (g_ct_capture_row == i ? 0xFFFFE600 : 0xFF5A5D64), &b);
        GdipFillPolygon(g, b, tri, 3, 0);
        GdipDeleteBrush(b);
    }
    float bx = STICK_X, by = STICK_Y;
    if (on & 1) by -= 15;
    if (on & 2) by += 15;
    if (on & 4) bx -= 15;
    if (on & 8) bx += 15;
    {
        void *pen;
        GdipCreatePen1(0xFF9A9CA1, 7 * k, 2, &pen);
        GdipSetPenStartCap(pen, 2);
        GdipSetPenEndCap(pen, 2);
        GdipDrawLine(g, pen, STICK_X * k, STICK_Y * k, bx * k, by * k);
        GdipDeletePen(pen);
    }
    gp_fill_circle(g, 0x60000000, bx + 2, by + 3, 20, k);
    gp_fill_circle(g, (on & 15) ? 0xFFFF4D5E : 0xFFD7263D, bx, by, 20, k);
    gp_fill_circle(g, 0x66FFFFFF, bx - 6, by - 7, 6, k);
    /* A B C D */
    for (int i = 0; i < 4; i++) {
        bool lit = (on >> (Z3_A + i)) & 1;
        float cx = k_panel_buttons[i].x, cy = k_panel_buttons[i].y + (lit ? 2.0f : 0.0f);
        if (lit) gp_fill_circle(g, 0x5500C8FF, cx, cy, BUTTON_R + 6, k);
        gp_fill_circle(g, darken(k_panel_buttons[i].color), cx, k_panel_buttons[i].y + 3, BUTTON_R, k);
        gp_fill_circle(g, lit ? lighten(k_panel_buttons[i].color) : k_panel_buttons[i].color, cx, cy, BUTTON_R, k);
        if (lit) gp_ring(g, 0xFF00C8FF, cx, cy, BUTTON_R + 2, 2, k, false);
        if (g_ct_capture_row == Z3_A + i) gp_ring(g, 0xFFFFE600, cx, cy, BUTTON_R + 4, 2, k, true);
    }
    if (g_ct_capture_row >= 0 && g_ct_capture_row < 4) gp_ring(g, 0xFFFFE600, STICK_X, STICK_Y, STICK_R + 3, 2, k, true);
    GdipDeleteGraphics(g);

    SetBkMode(mem, TRANSPARENT);
    wchar_t t[128];
    swprintf(t, 128, L"JOGADOR %d", p + 1);
    panel_text(mem, t, 16, 16, 130, 22, k, RGB(0xFF, 0xE6, 0x00), g_font_bold, DT_LEFT);
    for (int i = 0; i < 2; i++) {
        int bit = i ? Z3_START : Z3_COIN;
        const RECT *r = &k_pill[i];
        panel_text(mem, i ? L"START" : L"MOEDA", (float)r->left, (float)r->top, (float)(r->right - r->left),
                   (float)(r->bottom - r->top), k, ((on >> bit) & 1) ? RGB(0x14, 0x16, 0x1A) : RGB(0xF4, 0xF5, 0xF0),
                   g_font_small, DT_CENTER);
        first_key(g_ct_kbd[p][bit], t, 64);
        panel_text(mem, t, (float)r->left - 4, (float)r->bottom + 2, (float)(r->right - r->left) + 8, 16, k,
                   RGB(0x9A, 0x9C, 0xA1), g_font_small, DT_CENTER);
    }
    for (int i = 0; i < 4; i++) {
        float cx = k_panel_buttons[i].x, cy = k_panel_buttons[i].y + (((on >> (Z3_A + i)) & 1) ? 2.0f : 0.0f);
        panel_text(mem, k_panel_buttons[i].letter, cx - BUTTON_R, cy - BUTTON_R, 2 * BUTTON_R, 2 * BUTTON_R, k,
                   RGB(0xFF, 0xFF, 0xFF), g_font_bold, DT_CENTER);
        first_key(g_ct_kbd[p][Z3_A + i], t, 64);
        panel_text(mem, t, cx - 22, k_panel_buttons[i].y + BUTTON_R + 5, 44, 16, k, RGB(0x9A, 0x9C, 0xA1), g_font_small, DT_CENTER);
    }
    panel_text(mem, L"ALAVANCA", STICK_X - 50, STICK_Y + STICK_R + 4, 100, 16, k, RGB(0x9A, 0x9C, 0xA1), g_font_small, DT_CENTER);
    int pad = pad_of_player(p, g_ct_guid);
    if (!sdl.ready) swprintf(t, 128, L"SDL2.dll não encontrado: só teclado");
    else if (pad >= 0) swprintf(t, 128, L"Controle: %ls", g_pad_name[pad]);
    else swprintf(t, 128, L"Sem controle — o teclado funciona sempre");
    panel_text(mem, t, 14, PANEL_H - 30, PANEL_W - 28, 20, k, pad >= 0 ? RGB(0x00, 0xC8, 0xFF) : RGB(0x9A, 0x9C, 0xA1),
               g_font_small, DT_LEFT);

    BitBlt(d->hDC, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* clique no painel -> qual entrada (ou -1) */
static int panel_hit(int px, int py) {
    RECT rc;
    GetClientRect(g_ct_panel, &rc);
    float k = (float)rc.right / PANEL_W, x = px / k, y = py / k;
    for (int i = 0; i < 4; i++) {
        float dx = x - k_panel_buttons[i].x, dy = y - k_panel_buttons[i].y;
        if (dx * dx + dy * dy <= (BUTTON_R + 4) * (BUTTON_R + 4)) return Z3_A + i;
    }
    for (int i = 0; i < 2; i++)
        if (x >= k_pill[i].left && x <= k_pill[i].right && y >= k_pill[i].top && y <= k_pill[i].bottom + 18)
            return i ? Z3_START : Z3_COIN;
    float dx = x - STICK_X, dy = y - STICK_Y;
    if (dx * dx + dy * dy <= STICK_R * STICK_R) {
        if (fabsf(dx) > fabsf(dy)) return dx < 0 ? Z3_LEFT : Z3_RIGHT;
        return dy < 0 ? Z3_UP : Z3_DOWN;
    }
    return -1;
}

/* o que está apertado agora (teclado só com a janela em foco) */
static unsigned ct_pressed_now(void) {
    if (g_ct_tab == CT_HOTKEYS) return 0;
    const int p = g_ct_tab;
    unsigned m = 0;
    HWND fg = GetForegroundWindow();
    bool focus = fg == g_ct;
    int pad = pad_of_player(p, g_ct_guid);
    for (int i = 0; i < Z3_PLAYER_INPUTS; i++) {
        bool down = false;
        const char *s = g_ct_kbd[p][i];
        char tok[32];
        while (focus && *s && !down) {
            while (*s == ',' || *s == ' ') s++;
            size_t n = 0;
            while (s[n] && s[n] != ',' && n < 31) { tok[n] = s[n]; n++; }
            tok[n] = 0;
            s += n;
            while (*s && *s != ',') s++;
            int ki = z3_key_find(tok, (int)n);
            if (ki < 0) continue;
            int ps2 = z3_keys[ki].ps2;
            UINT vk = MapVirtualKeyW((UINT)((ps2 & 0xFF) | ((ps2 & 0x100) ? 0xE000 : 0)), MAPVK_VSC_TO_VK_EX);
            if (vk && (GetAsyncKeyState((int)vk) & 0x8000)) down = true;
        }
        s = g_ct_pad[p][i];
        while (pad >= 0 && *s && !down) {
            while (*s == ',' || *s == ' ') s++;
            size_t n = 0;
            while (s[n] && s[n] != ',' && n < 31) { tok[n] = s[n]; n++; }
            tok[n] = 0;
            s += n;
            while (*s && *s != ',') s++;
            if (n && pad_token_down(pad, tok)) down = true;
        }
        if (!down && pad >= 0 && g_ct_analog && i <= Z3_RIGHT) {
            static const char *const a[4] = {"lefty-", "lefty+", "leftx-", "leftx+"};
            down = pad_token_down(pad, a[i]);
        }
        if (down) m |= 1u << i;
    }
    return m;
}

/* ---- Captura: uma tecla ou um botão/eixo do controle ---- */
static int g_cap_kind;      /* 0 cancelou, 1 tecla, 2 controle */
static char g_cap_tok[32];
static bool g_cap_buttons_only;
static Uint8 g_cap_btn[MAX_PADS][SDL_CONTROLLER_BUTTON_MAX];
static bool g_cap_axis_armed[MAX_PADS][SDL_CONTROLLER_AXIS_MAX];

static void cap_baseline(void) {
    for (int i = 0; i < g_pad_count; i++) {
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) g_cap_btn[i][b] = sdl.GetButton(g_pads[i], (SDL_GameControllerButton)b);
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
            int v = sdl.GetAxis(g_pads[i], (SDL_GameControllerAxis)a);
            g_cap_axis_armed[i][a] = v > -8000 && v < 8000;
        }
    }
}

static bool cap_poll_pad(void) {
    if (!sdl.ready) return false;
    if (pads_poll()) { cap_baseline(); return false; }
    for (int i = 0; i < g_pad_count; i++) {
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            Uint8 now = sdl.GetButton(g_pads[i], (SDL_GameControllerButton)b);
            if (now && !g_cap_btn[i][b]) {
                const char *nm = sdl.ButtonName((SDL_GameControllerButton)b);
                if (nm) { snprintf(g_cap_tok, sizeof(g_cap_tok), "%s", nm); return true; }
            }
            g_cap_btn[i][b] = now;
        }
        if (g_cap_buttons_only) continue;
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
            int v = sdl.GetAxis(g_pads[i], (SDL_GameControllerAxis)a);
            if (v > -8000 && v < 8000) { g_cap_axis_armed[i][a] = true; continue; }
            if (!g_cap_axis_armed[i][a] || (v > -24000 && v < 24000)) continue;
            const char *nm = sdl.AxisName((SDL_GameControllerAxis)a);
            if (!nm) continue;
            bool trigger = a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            snprintf(g_cap_tok, sizeof(g_cap_tok), "%s%s", nm, trigger ? "" : (v < 0 ? "-" : "+"));
            return true;
        }
    }
    return false;
}

static LRESULT CALLBACK capture_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (wp == VK_ESCAPE) { g_cap_kind = 0; DestroyWindow(w); return 0; }
        int ps2 = (int)((lp >> 16) & 0xFF) | (((lp >> 24) & 1) ? 0x100 : 0);
        if (ps2 == 0x1D) { /* AltGr manda um Ctrl esquerdo falso antes do Alt direito */
            MSG next;
            if (PeekMessageW(&next, w, WM_KEYDOWN, WM_SYSKEYDOWN, PM_NOREMOVE) && next.time == (DWORD)GetMessageTime() &&
                (((next.lParam >> 16) & 0xFF) == 0x38) && ((next.lParam >> 24) & 1))
                return 0;
        }
        for (int i = 0; i < Z3_KEY_COUNT; i++)
            if (z3_keys[i].ps2 == ps2) {
                snprintf(g_cap_tok, sizeof(g_cap_tok), "%s", z3_keys[i].name);
                g_cap_kind = 1;
                DestroyWindow(w);
                return 0;
            }
        return 0; /* tecla sem nome conhecido (Pause, Num Lock...): espera outra */
    }
    case WM_SYSCHAR:
    case WM_SYSKEYUP: return 0; /* Alt sozinho não abre menu */
    case WM_TIMER:
        if (wp == ID_CAP_TIMER && cap_poll_pad()) { g_cap_kind = 2; DestroyWindow(w); }
        return 0;
    case WM_CLOSE: g_cap_kind = 0; DestroyWindow(w); return 0;
    case WM_DESTROY: KillTimer(w, ID_CAP_TIMER); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        HBRUSH bg = CreateSolidBrush(RGB(0x14, 0x16, 0x1A));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        wchar_t target[200];
        GetWindowTextW(w, target, 200);
        RECT a = {r.left + S(16), r.top + S(18), r.right - S(16), r.top + S(44)};
        SelectObject(dc, g_font_bold);
        SetTextColor(dc, RGB(0xFF, 0xE6, 0x00));
        DrawTextW(dc, target, -1, &a, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT b = {r.left + S(16), r.top + S(52), r.right - S(16), r.bottom - S(10)};
        SelectObject(dc, g_font);
        SetTextColor(dc, RGB(0xF4, 0xF5, 0xF0));
        DrawTextW(dc, sdl.ready ? L"Aperte uma tecla ou um botão do controle.\n\nEsc cancela."
                                : L"Aperte uma tecla.\n\nEsc cancela.",
                  -1, &b, DT_CENTER | DT_WORDBREAK);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static int capture_input(HWND owner, const wchar_t *target, bool buttons_only) {
    g_cap_kind = 0;
    g_cap_tok[0] = 0;
    g_cap_buttons_only = buttons_only;
    if (sdl.ready) { pads_poll(); cap_baseline(); }
    HWND w = create_dialog(L"Zera330Capture", target, 340, 140, owner);
    SetTimer(w, ID_CAP_TIMER, 15, NULL);
    run_modal(w, owner, false);
    return g_cap_kind;
}

/* ---- Lista e ações da tela de controles ---- */
static void ct_refresh_list(void) {
    int sel = ListView_GetNextItem(g_ct_list, -1, LVNI_SELECTED);
    SendMessageW(g_ct_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_ct_list);
    for (int i = 0; i < ct_rows(g_ct_tab); i++) {
        wchar_t k[200], p[200];
        binds_text(g_ct_kbd[g_ct_tab][i], false, k, 200);
        binds_text(g_ct_pad[g_ct_tab][i], true, p, 200);
        LVITEMW it = {0};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (wchar_t *)ct_label(g_ct_tab, i);
        ListView_InsertItem(g_ct_list, &it);
        ListView_SetItemText(g_ct_list, i, 1, k);
        ListView_SetItemText(g_ct_list, i, 2, p);
    }
    if (sel < 0) sel = 0;
    if (sel >= ct_rows(g_ct_tab)) sel = ct_rows(g_ct_tab) - 1;
    ListView_SetItemState(g_ct_list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SendMessageW(g_ct_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_ct_list, NULL, TRUE);
    InvalidateRect(g_ct_panel, NULL, FALSE);
}

/* Combo "controle do jogador": Automático, os conectados, e o salvo que não está aqui. */
static void ct_refresh_pads(void) {
    if (g_ct_tab == CT_HOTKEYS) return;
    const int p = g_ct_tab;
    SendMessageW(g_ct_pad_combo, CB_RESETCONTENT, 0, 0);
    int n = 0, sel = 0;
    wchar_t t[200];
    swprintf(t, 200, L"Automático (o %d.º controle conectado)", p + 1);
    SendMessageW(g_ct_pad_combo, CB_ADDSTRING, 0, (LPARAM)t);
    g_ct_combo_guid[n++][0] = 0;
    bool found = false;
    for (int i = 0; i < g_pad_count && n < MAX_PADS + 1; i++) {
        bool dup = false; /* dois controles iguais têm o mesmo GUID: aparece uma vez */
        for (int j = 1; j < n; j++) if (!strcmp(g_ct_combo_guid[j], g_pad_guid[i])) dup = true;
        if (dup) continue;
        SendMessageW(g_ct_pad_combo, CB_ADDSTRING, 0, (LPARAM)g_pad_name[i]);
        snprintf(g_ct_combo_guid[n], 40, "%s", g_pad_guid[i]);
        if (g_ct_guid[p][0] && !strcmp(g_ct_guid[p], g_pad_guid[i])) { sel = n; found = true; }
        n++;
    }
    if (g_ct_guid[p][0] && !found) {
        SendMessageW(g_ct_pad_combo, CB_ADDSTRING, 0, (LPARAM)L"Controle escolhido antes (desconectado)");
        snprintf(g_ct_combo_guid[n], 40, "%s", g_ct_guid[p]);
        sel = n++;
    }
    SendMessageW(g_ct_pad_combo, CB_SETCURSEL, (WPARAM)sel, 0);

    wchar_t s[400] = L"";
    if (!sdl.ready) swprintf(s, 400, L"Não deu pra abrir o SDL2.dll da pasta do zera330: só o teclado pode ser configurado aqui.");
    else if (!g_pad_count) swprintf(s, 400, L"Nenhum controle conectado. Ligue um controle (USB ou Bluetooth) e ele aparece aqui na hora.");
    else {
        swprintf(s, 400, L"%d controle(s) conectado(s): ", g_pad_count);
        for (int i = 0; i < g_pad_count; i++) {
            if (i) wcscat(s, L", ");
            if (wcslen(s) + wcslen(g_pad_name[i]) < 380) wcscat(s, g_pad_name[i]);
        }
    }
    SetWindowTextW(g_ct_pads_text, s);
}

static void ct_show_tab(void) {
    bool player = g_ct_tab != CT_HOTKEYS;
    HWND player_only[] = {g_ct_panel, g_ct_pad_label, g_ct_pad_combo, g_ct_analog_box, g_ct_hint};
    for (size_t i = 0; i < ARRAY_COUNT(player_only); i++) ShowWindow(player_only[i], player ? SW_SHOW : SW_HIDE);
    ShowWindow(g_ct_hotkey_text, player ? SW_HIDE : SW_SHOW);
    if (player) {
        wchar_t t[64];
        swprintf(t, 64, L"Controle do jogador %d:", g_ct_tab + 1);
        SetWindowTextW(g_ct_pad_label, t);
    }
    SetWindowTextW(g_ct_warn, L"");
    g_ct_pressed = 0;
    ct_refresh_pads();
    ct_refresh_list();
}

/* Avisa se a mesma tecla/botão já está em outra entrada (não apaga: o
 * usuário decide). Teclado vale pra tudo; controle, dentro do mesmo jogador
 * e contra os atalhos. */
static void ct_check_conflict(int row, bool pad, const char *tok) {
    wchar_t msg[300] = L"";
    for (int t = 0; t < CT_TABS && !msg[0]; t++) {
        if (pad && t != g_ct_tab && t != CT_HOTKEYS && g_ct_tab != CT_HOTKEYS) continue;
        for (int i = 0; i < ct_rows(t) && !msg[0]; i++) {
            if (t == g_ct_tab && i == row) continue;
            if (!list_has(pad ? g_ct_pad[t][i] : g_ct_kbd[t][i], tok)) continue;
            wchar_t name[64];
            if (pad) pad_label(tok, name, 64); else key_label(tok, name, 64);
            swprintf(msg, 300, L"Atenção: %ls também está em %ls — %ls.", name, k_ct_tabs[t], ct_label(t, i));
        }
    }
    SetWindowTextW(g_ct_warn, msg);
}

static void ct_edit(int id, int row) {
    if (row < 0) row = ListView_GetNextItem(g_ct_list, -1, LVNI_SELECTED);
    if (id == ID_CT_DEFAULTS) {
        if (MessageBoxW(g_ct, L"Voltar todas as teclas e botões desta aba para o padrão?", APP_NAME,
                        MB_ICONQUESTION | MB_YESNO) != IDYES)
            return;
        for (int i = 0; i < ct_rows(g_ct_tab); i++) {
            snprintf(g_ct_kbd[g_ct_tab][i], 96, "%s", ct_default(g_ct_tab, i, false));
            snprintf(g_ct_pad[g_ct_tab][i], 96, "%s", ct_default(g_ct_tab, i, true));
        }
        SetWindowTextW(g_ct_warn, L"");
        ct_refresh_list();
        return;
    }
    if (row < 0) { MessageBoxW(g_ct, L"Selecione uma entrada na lista.", APP_NAME, MB_ICONINFORMATION); return; }
    ListView_SetItemState(g_ct_list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    if (id == ID_CT_CLEAR || id == ID_CT_CLEAR_KEY) g_ct_kbd[g_ct_tab][row][0] = 0;
    if (id == ID_CT_CLEAR || id == ID_CT_CLEAR_PAD) g_ct_pad[g_ct_tab][row][0] = 0;
    if (id == ID_CT_SET || id == ID_CT_ADD) {
        wchar_t target[160];
        swprintf(target, 160, L"%ls — %ls", k_ct_tabs[g_ct_tab], ct_label(g_ct_tab, row));
        g_ct_capture_row = g_ct_tab == CT_HOTKEYS ? -1 : row;
        InvalidateRect(g_ct_panel, NULL, FALSE);
        /* nos atalhos o jogo só reconhece botões do controle (não eixos) */
        int kind = capture_input(g_ct, target, g_ct_tab == CT_HOTKEYS);
        g_ct_capture_row = -1;
        InvalidateRect(g_ct_panel, NULL, FALSE);
        if (!kind) return;
        char *list = kind == 2 ? g_ct_pad[g_ct_tab][row] : g_ct_kbd[g_ct_tab][row];
        if (id == ID_CT_SET || !list[0]) snprintf(list, 96, "%s", g_cap_tok);
        else if (!list_has(list, g_cap_tok)) {
            int count = 1;
            for (const char *c = list; *c; c++) count += *c == ',';
            if (count < 4 && strlen(list) + strlen(g_cap_tok) + 2 < 96) { strcat(list, ","); strcat(list, g_cap_tok); }
            else MessageBoxW(g_ct, L"Cada entrada aceita até 4 teclas e 4 botões.", APP_NAME, MB_ICONINFORMATION);
        }
        ct_refresh_list();
        ct_check_conflict(row, kind == 2, g_cap_tok);
        return;
    }
    SetWindowTextW(g_ct_warn, L"");
    ct_refresh_list();
}

static void ct_menu(void) {
    int row = ListView_GetNextItem(g_ct_list, -1, LVNI_SELECTED);
    if (row < 0) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_CT_SET, L"Trocar...");
    AppendMenuW(m, MF_STRING, ID_CT_ADD, L"Adicionar outra...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_CT_CLEAR_KEY, L"Limpar teclado");
    AppendMenuW(m, MF_STRING, ID_CT_CLEAR_PAD, L"Limpar controle");
    POINT pt;
    GetCursorPos(&pt);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_ct, NULL);
    DestroyMenu(m);
    if (id) ct_edit(id, row);
}

static LRESULT CALLBACK controls_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lp)->hwndItem == g_ct_panel) { paint_panel((DRAWITEMSTRUCT *)lp); return TRUE; }
        break;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == g_ct_warn) {
            SetTextColor((HDC)wp, RGB(0xB3, 0x5C, 0x00));
            SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_TIMER:
        if (wp == ID_CT_TIMER) {
            if (pads_poll()) { ct_refresh_pads(); InvalidateRect(g_ct_panel, NULL, FALSE); }
            unsigned now = ct_pressed_now();
            if (now != g_ct_pressed) { g_ct_pressed = now; InvalidateRect(g_ct_panel, NULL, FALSE); }
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->hwndFrom == g_ct_tabctl && n->code == TCN_SELCHANGE) {
            g_ct_tab = TabCtrl_GetCurSel(g_ct_tabctl);
            ct_show_tab();
        } else if (n->hwndFrom == g_ct_list && n->code == NM_DBLCLK) {
            ct_edit(ID_CT_SET, ((NMITEMACTIVATE *)lp)->iItem);
        } else if (n->hwndFrom == g_ct_list && n->code == NM_RCLICK) {
            ct_menu();
        } else if (n->hwndFrom == g_ct_list && n->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)lp)->wVKey == VK_DELETE) {
            ct_edit(ID_CT_CLEAR, -1);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_CT_PANEL && HIWORD(wp) == STN_CLICKED) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(g_ct_panel, &pt);
            int row = panel_hit(pt.x, pt.y);
            if (row >= 0) ct_edit(ID_CT_SET, row);
        } else if (id == ID_CT_PAD && HIWORD(wp) == CBN_SELCHANGE && g_ct_tab != CT_HOTKEYS) {
            int sel = (int)SendMessageW(g_ct_pad_combo, CB_GETCURSEL, 0, 0);
            if (sel >= 0) snprintf(g_ct_guid[g_ct_tab], 40, "%s", g_ct_combo_guid[sel]);
            InvalidateRect(g_ct_panel, NULL, FALSE);
        } else if (id == ID_CT_ANALOG) {
            g_ct_analog = SendMessageW(g_ct_analog_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
        } else if (id >= ID_CT_SET && id <= ID_CT_DEFAULTS) {
            ct_edit(id, -1);
        } else if (id == IDOK) {
            ct_save();
            DestroyWindow(w);
        } else if (id == IDCANCEL) {
            DestroyWindow(w);
        }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: KillTimer(w, ID_CT_TIMER); g_ct = NULL; return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static void configure_controls(HWND owner) {
    ct_load();
    pads_start();
    pads_poll();
    g_ct_tab = CT_P1;
    g_ct_capture_row = -1;
    g_ct = create_dialog(L"Zera330Controls", L"Controles e teclado", 780, 566, owner);
    HWND w = g_ct;
    /* só a faixa das abas: o conteúdo fica no fundo da janela, igual ao resto */
    g_ct_tabctl = add_control(w, WC_TABCONTROLW, L"", WS_TABSTOP, 12, 10, 756, 28, ID_CT_TAB);
    for (int i = 0; i < CT_TABS; i++) {
        TCITEMW ti = {0};
        ti.mask = TCIF_TEXT;
        ti.pszText = (wchar_t *)k_ct_tabs[i];
        TabCtrl_InsertItem(g_ct_tabctl, i, &ti);
    }
    g_ct_panel = add_control(w, L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, 28, 48, PANEL_W, PANEL_H, ID_CT_PANEL);
    g_ct_pad_label = add_control(w, L"STATIC", L"", 0, 28, 310, 300, 20, 0);
    g_ct_pad_combo = add_control(w, WC_COMBOBOXW, NULL, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 28, 332, 300, 300, ID_CT_PAD);
    g_ct_analog_box = add_control(w, L"BUTTON", L"Analógico esquerdo também move a alavanca",
                                  WS_TABSTOP | BS_AUTOCHECKBOX, 28, 366, 300, 22, ID_CT_ANALOG);
    SendMessageW(g_ct_analog_box, BM_SETCHECK, g_ct_analog ? BST_CHECKED : BST_UNCHECKED, 0);
    g_ct_hint = add_control(w, L"STATIC",
                            L"Teste aqui: aperte as teclas ou os botões e o painel acende. "
                            L"Clique numa peça do painel para trocá-la.",
                            0, 28, 396, 300, 70, 0);
    g_ct_hotkey_text = add_control(w, L"STATIC",
                                   L"Atalhos valem durante o jogo.\n\n"
                                   L"Alt+Enter também alterna a tela cheia.\n\n"
                                   L"No controle, os atalhos aceitam botões (por exemplo Guia, L3 ou R3) — "
                                   L"evite usar um botão que o jogo também usa.\n\n"
                                   L"O botão de serviço abre o menu de testes da placa, como no fliperama.",
                                   0, 28, 52, 296, 300, 0);
    g_ct_list = add_control(w, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                            344, 48, 410, 330, ID_CT_LIST);
    ListView_SetExtendedListViewStyle(g_ct_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    static const wchar_t *const cols[3] = {L"Entrada", L"Teclado", L"Controle"};
    static const int widths[3] = {150, 130, 122};
    for (int i = 0; i < 3; i++) {
        LVCOLUMNW c = {0};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (wchar_t *)cols[i];
        c.cx = S(widths[i]);
        ListView_InsertColumn(g_ct_list, i, &c);
    }
    add_control(w, L"BUTTON", L"Trocar...", WS_TABSTOP, 344, 388, 96, 30, ID_CT_SET);
    add_control(w, L"BUTTON", L"Adicionar...", WS_TABSTOP, 446, 388, 96, 30, ID_CT_ADD);
    add_control(w, L"BUTTON", L"Limpar", WS_TABSTOP, 548, 388, 96, 30, ID_CT_CLEAR);
    add_control(w, L"BUTTON", L"Padrão", WS_TABSTOP, 650, 388, 104, 30, ID_CT_DEFAULTS);
    g_ct_warn = add_control(w, L"STATIC", L"", 0, 344, 426, 410, 40, 0);
    g_ct_pads_text = add_control(w, L"STATIC", L"", SS_ENDELLIPSIS, 12, 490, 540, 20, 0);
    add_control(w, L"STATIC", L"Botões do controle no padrão Xbox: A embaixo, B à direita, X à esquerda, Y em cima.",
                SS_ENDELLIPSIS, 12, 512, 540, 20, 0);
    add_control(w, L"BUTTON", L"Salvar", WS_TABSTOP | BS_DEFPUSHBUTTON, 568, 524, 96, 32, IDOK);
    add_control(w, L"BUTTON", L"Cancelar", WS_TABSTOP, 672, 524, 96, 32, IDCANCEL);
    ct_show_tab();
    SetTimer(w, ID_CT_TIMER, 30, NULL);
    run_modal(w, owner, true);
    pads_stop(); /* o jogo abre o controle sozinho */
}

/* ---- Configurações ---- */
static HWND g_set_games, g_set_bios, g_set_intro, g_settings;
static HWND g_set_scale, g_set_aspect, g_set_filter, g_set_scan, g_set_full, g_set_fps, g_set_volume, g_set_volume_text, g_set_mute;
static bool g_settings_saved;
enum { ID_SET_CONTROLS = 420, ID_SET_VOLUME, ID_SET_MUTE };

static int CALLBACK browse_ready(HWND dialog, UINT message, LPARAM value, LPARAM start) {
    (void)value;
    if (message == BFFM_INITIALIZED && start) SendMessageW(dialog, BFFM_SETSELECTIONW, TRUE, start);
    return 0;
}
static void browse_into(HWND owner, HWND edit, const wchar_t *title) {
    wchar_t start[MAX_PATH], out[MAX_PATH];
    GetWindowTextW(edit, start, MAX_PATH);
    BROWSEINFOW bi = {0};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browse_ready;
    bi.lParam = (LPARAM)start;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    if (SHGetPathFromIDListW(id, out)) SetWindowTextW(edit, out);
    CoTaskMemFree(id);
}

static HWND add_control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void volume_text(void) {
    wchar_t t[16];
    swprintf(t, 16, L"%d%%", (int)SendMessageW(g_set_volume, TBM_GETPOS, 0, 0));
    SetWindowTextW(g_set_volume_text, t);
}

static void settings_save(HWND w) {
    GetWindowTextW(g_set_games, g_games_dir, MAX_PATH);
    GetWindowTextW(g_set_bios, g_bios_dir, MAX_PATH);
    g_intro = SendMessageW(g_set_intro, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!save_settings()) MessageBoxW(w, L"Falha ao salvar as configurações.", APP_NAME, MB_ICONERROR);
    ini_put_int(L"video", L"escala", (int)SendMessageW(g_set_scale, CB_GETCURSEL, 0, 0) + 2);
    ini_put_int(L"video", L"proporcao", (int)SendMessageW(g_set_aspect, CB_GETCURSEL, 0, 0));
    ini_put_int(L"video", L"filtro", (int)SendMessageW(g_set_filter, CB_GETCURSEL, 0, 0));
    ini_put_int(L"video", L"scanlines", (int)SendMessageW(g_set_scan, CB_GETCURSEL, 0, 0));
    ini_put_int(L"video", L"tela_cheia", SendMessageW(g_set_full, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ini_put_int(L"video", L"mostrar_fps", SendMessageW(g_set_fps, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ini_put_int(L"audio", L"volume", (int)SendMessageW(g_set_volume, TBM_GETPOS, 0, 0));
    ini_put_int(L"audio", L"mudo", SendMessageW(g_set_mute, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_settings_saved = true;
}

static LRESULT CALLBACK settings_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HSCROLL:
        if ((HWND)lp == g_set_volume) volume_text();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SET_GAMES_BROWSE: browse_into(w, g_set_games, L"Pasta dos jogos"); return 0;
        case ID_SET_BIOS_BROWSE: browse_into(w, g_set_bios, L"Pasta da BIOS"); return 0;
        case ID_SET_CONTROLS: configure_controls(w); return 0;
        case ID_SET_MUTE:
            EnableWindow(g_set_volume, SendMessageW(g_set_mute, BM_GETCHECK, 0, 0) != BST_CHECKED);
            return 0;
        case IDOK:
        case ID_SET_OK:
            settings_save(w);
            DestroyWindow(w);
            return 0;
        case IDCANCEL:
        case ID_SET_CANCEL: DestroyWindow(w); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(w); return 0;
    case WM_DESTROY: g_settings = NULL; return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static void configure_general(void) {
    load_settings();
    g_settings_saved = false;
    HWND w = g_settings = create_dialog(L"Zera330Settings", L"Configurações", 540, 540, g_main);

    add_control(w, L"BUTTON", L"Pastas", BS_GROUPBOX, 12, 8, 516, 132, 0);
    add_control(w, L"STATIC", L"Jogos (.zip):", 0, 24, 30, 300, 20, 0);
    g_set_games = add_control(w, L"EDIT", g_games_dir, WS_TABSTOP | ES_AUTOHSCROLL, 24, 50, 392, 24, ID_SET_GAMES);
    add_control(w, L"BUTTON", L"Procurar...", WS_TABSTOP, 424, 49, 92, 26, ID_SET_GAMES_BROWSE);
    add_control(w, L"STATIC", L"BIOS do Neo Geo (neogeo.zip):", 0, 24, 82, 300, 20, 0);
    g_set_bios = add_control(w, L"EDIT", g_bios_dir, WS_TABSTOP | ES_AUTOHSCROLL, 24, 102, 392, 24, ID_SET_BIOS);
    add_control(w, L"BUTTON", L"Procurar...", WS_TABSTOP, 424, 101, 92, 26, ID_SET_BIOS_BROWSE);

    add_control(w, L"BUTTON", L"Vídeo", BS_GROUPBOX, 12, 148, 516, 196, 0);
    static const wchar_t *const scales[] = {L"2×", L"3× (padrão)", L"4×", L"5×"};
    static const wchar_t *const aspects[] = {L"4:3, como no monitor do fliperama (padrão)", L"Pixels quadrados (320 × 224)",
                                             L"Esticar para a janela"};
    static const wchar_t *const filters[] = {L"Nítido: pixels bem definidos (padrão)", L"Suave"};
    static const wchar_t *const scans[] = {L"Desligadas", L"Leves", L"Fortes"};
    add_control(w, L"STATIC", L"Tamanho da janela:", 0, 24, 172, 160, 20, 0);
    g_set_scale = add_combo(w, 190, 168, 326, scales, 4, ini_int(L"video", L"escala", 3, 2, 5) - 2);
    add_control(w, L"STATIC", L"Proporção da imagem:", 0, 24, 204, 160, 20, 0);
    g_set_aspect = add_combo(w, 190, 200, 326, aspects, 3, ini_int(L"video", L"proporcao", 0, 0, 2));
    add_control(w, L"STATIC", L"Filtro:", 0, 24, 236, 160, 20, 0);
    g_set_filter = add_combo(w, 190, 232, 326, filters, 2, ini_int(L"video", L"filtro", 0, 0, 1));
    add_control(w, L"STATIC", L"Scanlines (linhas de CRT):", 0, 24, 268, 160, 20, 0);
    g_set_scan = add_combo(w, 190, 264, 326, scans, 3, ini_int(L"video", L"scanlines", 0, 0, 2));
    g_set_full = add_control(w, L"BUTTON", L"Começar em tela cheia (F11 ou Alt+Enter alternam no jogo)",
                             WS_TABSTOP | BS_AUTOCHECKBOX, 24, 296, 490, 20, 0);
    SendMessageW(g_set_full, BM_SETCHECK, ini_int(L"video", L"tela_cheia", 0, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    g_set_fps = add_control(w, L"BUTTON", L"Mostrar fps e velocidade no título da janela",
                            WS_TABSTOP | BS_AUTOCHECKBOX, 24, 318, 490, 20, 0);
    SendMessageW(g_set_fps, BM_SETCHECK, ini_int(L"video", L"mostrar_fps", 1, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);

    add_control(w, L"BUTTON", L"Som", BS_GROUPBOX, 12, 352, 516, 84, 0);
    add_control(w, L"STATIC", L"Volume:", 0, 24, 378, 70, 20, 0);
    g_set_volume = add_control(w, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 96, 372, 360, 30, ID_SET_VOLUME);
    SendMessageW(g_set_volume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(g_set_volume, TBM_SETTICFREQ, 10, 0);
    SendMessageW(g_set_volume, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_set_volume, TBM_SETPOS, TRUE, ini_int(L"audio", L"volume", 100, 0, 100));
    g_set_volume_text = add_control(w, L"STATIC", L"", SS_RIGHT, 460, 378, 56, 20, 0);
    volume_text();
    g_set_mute = add_control(w, L"BUTTON", L"Sem som", WS_TABSTOP | BS_AUTOCHECKBOX, 24, 408, 300, 20, ID_SET_MUTE);
    bool mute = ini_int(L"audio", L"mudo", 0, 0, 1) != 0;
    SendMessageW(g_set_mute, BM_SETCHECK, mute ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g_set_volume, !mute);

    g_set_intro = add_control(w, L"BUTTON", L"Tocar a abertura do zera330 antes de cada jogo",
                              WS_TABSTOP | BS_AUTOCHECKBOX, 24, 448, 490, 20, ID_SET_INTRO);
    SendMessageW(g_set_intro, BM_SETCHECK, g_intro ? BST_CHECKED : BST_UNCHECKED, 0);

    add_control(w, L"BUTTON", L"Controles e teclado...", WS_TABSTOP, 12, 494, 220, 34, ID_SET_CONTROLS);
    add_control(w, L"BUTTON", L"Salvar", WS_TABSTOP | BS_DEFPUSHBUTTON, 330, 494, 96, 34, ID_SET_OK);
    add_control(w, L"BUTTON", L"Cancelar", WS_TABSTOP, 432, 494, 96, 34, ID_SET_CANCEL);
    run_modal(w, g_main, true);
    if (g_settings_saved) fill_list();
}

/* ---- Janela principal ---- */
static void load_logo(void) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\assets\\zera330-logo.png", g_base);
    void *bmp = NULL;
    if (GdipCreateBitmapFromFile(p, &bmp) != 0 || !bmp) return;
    UINT w = 0, h = 0;
    GdipGetImageWidth(bmp, &w);
    GdipGetImageHeight(bmp, &h);
    if (GdipCreateHBITMAPFromBitmap(bmp, &g_logo, 0xFF14161A) == 0) { g_logo_w = (int)w; g_logo_h = (int)h; }
    GdipDisposeImage(bmp);
}

/* Faixa de título no grafite da marca: logo à esquerda, contagem à direita. */
static void paint_banner(const DRAWITEMSTRUCT *d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH bg = CreateSolidBrush(RGB(0x14, 0x16, 0x1A));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    int x = S(4);
    if (g_logo) {
        int h = r.bottom - r.top, w = g_logo_w * h / g_logo_h;
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, g_logo);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, r.left, r.top, w, h, mem, 0, 0, g_logo_w, g_logo_h, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        x = w;
    }
    wchar_t text[200];
    if (!dir_exists(g_games_dir)) swprintf(text, ARRAY_COUNT(text), L"Escolha a pasta dos jogos em Configurações.");
    else swprintf(text, ARRAY_COUNT(text), L"%d jogo(s)    ·    Neo Geo MVS", g_count);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0x9A, 0x9C, 0xA1));
    HGDIOBJ oldf = SelectObject(dc, g_font_banner);
    RECT tr = {r.left + x, r.top, r.right - S(16), r.bottom};
    DrawTextW(dc, text, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, oldf);
}

static void layout(void) {
    RECT r;
    GetClientRect(g_main, &r);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g_status, &sr);
    int status_h = sr.bottom - sr.top, pad = S(8), bh = S(38), banner_h = S(64);
    int columns = r.right < S(900) ? 3 : 6, rows = 6 / columns;
    int bottom = r.bottom - status_h - pad - rows * bh - (rows - 1) * pad;
    MoveWindow(g_banner, 0, 0, r.right, banner_h, TRUE);
    MoveWindow(g_list, pad, banner_h + pad, r.right - 2 * pad, bottom - banner_h - 2 * pad, TRUE);
    int bw = (r.right - (columns + 1) * pad) / columns;
    for (int i = 0; i < 6; i++)
        MoveWindow(g_buttons[i], pad + (i % columns) * (bw + pad), bottom + (i / columns) * (bh + pad), bw, bh, TRUE);
    int parts[2] = {r.right - S(260) > S(100) ? r.right - S(260) : S(100), -1};
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
}

static void on_command(int id) {
    if (id == ID_SETTINGS) { configure_general(); return; }
    if (id == ID_REFRESH) { load_settings(); fill_list(); status(L"Lista atualizada."); return; }
    if (id == ID_SAVES) { open_saves(); return; }
    int gi = selected_game();
    if (gi < 0) { MessageBoxW(g_main, L"Selecione um jogo na lista.", APP_NAME, MB_ICONINFORMATION); return; }
    if (id == ID_PLAY) run_game(gi);
    else if (id == ID_LOG) view_log(&g_games[gi]);
    else if (id == ID_SHORTCUT) create_shortcut(&g_games[gi]);
}

static void list_menu(void) {
    if (selected_game() < 0) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_PLAY, L"Jogar");
    AppendMenuW(m, MF_STRING, ID_LOG, L"Ver log");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_SHORTCUT, L"Criar atalho na área de trabalho");
    POINT pt;
    GetCursorPos(&pt);
    int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
    DestroyMenu(m);
    if (id) on_command(id);
}

static LRESULT CALLBACK main_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_main = w;
        g_banner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 0, w, NULL, NULL, NULL);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                 0, 0, 0, 0, w, (HMENU)ID_LIST, NULL, NULL);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        for (int i = 0; i < (int)ARRAY_COUNT(k_columns); i++) {
            LVCOLUMNW c = {0};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (wchar_t *)k_columns[i];
            c.cx = S(k_widths[i]);
            ListView_InsertColumn(g_list, i, &c);
        }
        for (int i = 0; i < 6; i++)
            g_buttons[i] = CreateWindowExW(0, L"BUTTON", k_button_text[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i ? 0 : BS_DEFPUSHBUTTON),
                                           0, 0, 0, 0, w, (HMENU)(INT_PTR)(ID_PLAY + i), NULL, NULL);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, w, NULL, NULL, NULL);
        HWND all[] = {g_list, g_status, g_buttons[0], g_buttons[1], g_buttons[2], g_buttons[3], g_buttons[4], g_buttons[5]};
        for (size_t i = 0; i < ARRAY_COUNT(all); i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)g_font, TRUE);
        layout(); /* as duas partes da barra existem antes do texto */
        wchar_t ver[128];
        swprintf(ver, ARRAY_COUNT(ver), L"Versão %hs (%hs)", ZERA330_VERSION, ZERA330_GIT_COMMIT);
        SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)ver);
        status(L"Pronto");
        fill_list();
        autoplay();
        SetTimer(w, ID_TIMER, 1000, NULL);
        return 0;
    }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(640);
        mm->ptMinTrackSize.y = S(400);
        return 0;
    }
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lp)->hwndItem == g_banner) { paint_banner((DRAWITEMSTRUCT *)lp); return TRUE; }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) >= ID_PLAY && LOWORD(wp) <= ID_SETTINGS) on_command(LOWORD(wp));
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->idFrom == ID_LIST && n->code == NM_RCLICK) list_menu();
        if (n->idFrom == ID_LIST && (n->code == NM_DBLCLK || n->code == LVN_KEYDOWN)) {
            if (n->code == NM_DBLCLK || ((NMLVKEYDOWN *)lp)->wVKey == VK_RETURN) on_command(ID_PLAY);
            else if (((NMLVKEYDOWN *)lp)->wVKey == VK_F5) on_command(ID_REFRESH);
        }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) SetFocus(g_list);
        return 0;
    case WM_TIMER: poll_game(); return 0;
    case WM_DESTROY:
        if (game_running() && g_proc_game >= 0)  /* o jogo continua; o tempo até aqui fica salvo */
            playtime_add(g_games[g_proc_game].set, (unsigned)((GetTickCount64() - g_play_started) / 1000u));
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)prev;
    (void)cmdline;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i + 1 < argc; i++)
        if (!wcscmp(argv[i], L"--jogar")) swprintf(g_autoplay, ARRAY_COUNT(g_autoplay), L"%ls", argv[i + 1]);
    if (argv) LocalFree(argv);
    GetModuleFileNameW(NULL, g_base, MAX_PATH);
    wchar_t *slash = wcsrchr(g_base, L'\\');
    if (slash) *slash = 0;
    SetCurrentDirectoryW(g_base);
    static const wchar_t *const dirs[] = {L"user", L"user\\config", L"user\\logs", L"user\\nvram", L"user\\states"};
    for (size_t i = 0; i < ARRAY_COUNT(dirs); i++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\%ls", g_base, dirs[i]);
        CreateDirectoryW(p, NULL);
    }
    swprintf(g_ini, MAX_PATH, L"%ls\\user\\config\\zera330.ini", g_base);
    swprintf(g_time_ini, MAX_PATH, L"%ls\\user\\config\\tempo.ini", g_base);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    load_settings();

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);
    ULONG_PTR gdip;
    gdip_input gi = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&gdip, &gi, NULL);
    load_logo();

    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    NONCLIENTMETRICSW ncm = {0};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -S(15);
    g_font_banner = CreateFontIndirectW(&lf);
    lf = ncm.lfMessageFont;
    lf.lfWeight = FW_BOLD;
    g_font_bold = CreateFontIndirectW(&lf);
    lf = ncm.lfMessageFont;
    lf.lfHeight = -S(11);
    lf.lfWeight = FW_SEMIBOLD;
    g_font_small = CreateFontIndirectW(&lf);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"Zera330Main";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = settings_proc;
    wc.lpszClassName = L"Zera330Settings";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = controls_proc;
    wc.lpszClassName = L"Zera330Controls";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = capture_proc;
    wc.lpszClassName = L"Zera330Capture";
    RegisterClassExW(&wc);

    wchar_t title[128];
    swprintf(title, ARRAY_COUNT(title), L"%ls %hs - Neo Geo MVS", APP_NAME, ZERA330_VERSION);
    HWND w = CreateWindowExW(0, L"Zera330Main", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             S(1000), S(640), NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
