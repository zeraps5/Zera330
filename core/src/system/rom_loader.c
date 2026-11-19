/* Carregador de romset Neo-Geo (Fase 3, revisado). Romsets reais na prática
 * usam nomes de arquivo bem variados por chip — ex: o cartucho "Double
 * Dragon" (MVS) vem como "082-p1.rom", "082-s1.rom", "082-c1.rom" etc (um
 * prefixo do jogo + o nome do chip), não "p1.p1"/"p1.bin" como a Fase 3
 * original assumia. Por isso, em vez de uma lista fixa de nomes completos,
 * escaneamos o diretório e casamos por "token": o nome do arquivo (sem
 * extensão), dividido em segmentos por '-'/'_'/'.', precisa ter um segmento
 * EXATAMENTE igual ao token do chip ("p1", "s1", "c3", ...). Isso aceita
 * "082-p1.rom", "p1.bin", "p1.p1" etc., mas não confunde "sp1.sp1" (BIOS)
 * com o token "p1" (o segmento é "sp1", não "p1").
 *
 * A BIOS não tem um token comum entre versões (sp-s2, neo-epo, vs-bios,
 * uni-bios...), então continua usando uma lista de nomes conhecidos, por
 * prioridade — mas agora escaneada de um diretório PRÓPRIO (`bios_dir`),
 * já que na prática a BIOS é compartilhada entre jogos e vive separada do
 * cartucho.
 *
 * Achado #71: ROMS DIRETO DE DENTRO DO .ZIP. Antes disso, cada jogo tinha
 * que ser extraído numa pasta própria antes de jogar — bagunçado quando
 * você tem um romset inteiro (aqui: 170 jogos MVS/AES em `roms/zips/`,
 * baixados já compactados). Em vez de extrair tudo em disco, `cart_dir` e
 * `bios_dir` agora podem apontar tanto para uma PASTA (comportamento
 * antigo, intacto) quanto para um ARQUIVO .zip — detectado automaticamente
 * (é pasta? trata como pasta; senão, tenta abrir como zip). Toda a lógica
 * de busca por token acima continua igual: só a camada de baixo nível (`
 * listar entradas` / `ler uma entrada inteira pra memória`) foi trocada por
 * uma pequena abstração (`rom_source_t`) que sabe fazer isso tanto num
 * diretório real quanto dentro de um .zip (via miniz, vendorizado em
 * third_party/miniz — os romsets MAME usam DEFLATE, não dá pra só copiar
 * bytes crus do arquivo). */

#include "neocore/neogeo.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_DIR_FILES 256
#define MAX_NAME_LEN 260

typedef struct {
    char names[MAX_DIR_FILES][MAX_NAME_LEN];
    u32  zip_index[MAX_DIR_FILES]; /* só usado quando a listagem veio de um .zip */
    int count;
} dir_listing_t;

static void set_err(char *out_err, u32 out_err_size, const char *msg) {
    if (out_err && out_err_size > 0) snprintf(out_err, out_err_size, "%s", msg);
}

static long file_size(FILE *f) {
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, cur, SEEK_SET);
    return sz;
}

/* ---- Detecção pasta-vs-arquivo (multiplataforma) -------------------------- */

#if defined(_WIN32)
static bool path_is_dir(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}
static bool path_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
}
#else
static bool path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}
#endif

/* ---- rom_source_t: pasta OU .zip, mesma interface ------------------------- */

typedef struct {
    bool is_zip;
    char base[1024];      /* modo pasta: caminho base pra montar "base/nome" */
    mz_zip_archive zip;   /* modo zip: arquivo já aberto (central directory lida) */
} rom_source_t;

/* Lista os arquivos (não-diretórios) de `dir_path`. Retorna false só em
 * caso de o diretório nem existir/abrir — lista vazia não é erro fatal
 * aqui (quem chama decide se falta algo obrigatório). */
static bool list_dir(const char *dir_path, dir_listing_t *out) {
    out->count = 0;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (out->count >= MAX_DIR_FILES) break;
        snprintf(out->names[out->count], MAX_NAME_LEN, "%s", fd.cFileName);
        out->count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
#else
    DIR *d = opendir(dir_path);
    if (!d) return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue; /* pula "." ".." e arquivos ocultos */
        if (out->count >= MAX_DIR_FILES) break;
        snprintf(out->names[out->count], MAX_NAME_LEN, "%s", ent->d_name);
        out->count++;
    }
    closedir(d);
    return true;
#endif
}

/* Abre `path` como fonte de ROMs: pasta de verdade (comportamento clássico)
 * ou, se não for pasta mas existir como arquivo, tenta como .zip (miniz lê
 * o central directory na hora — barato, não descompacta nada ainda). */
static bool source_open(rom_source_t *src, const char *path, char *out_err, u32 out_err_size) {
    memset(src, 0, sizeof(*src));
    if (path_is_dir(path)) {
        src->is_zip = false;
        snprintf(src->base, sizeof(src->base), "%s", path);
        return true;
    }
    if (path_exists(path)) {
        if (mz_zip_reader_init_file(&src->zip, path, 0)) {
            src->is_zip = true;
            return true;
        }
        char msg[512];
        snprintf(msg, sizeof(msg), "'%s' nao e uma pasta nem um .zip valido", path);
        set_err(out_err, out_err_size, msg);
        return false;
    }
    char msg[512];
    snprintf(msg, sizeof(msg), "caminho nao encontrado: '%s'", path);
    set_err(out_err, out_err_size, msg);
    return false;
}

static void source_close(rom_source_t *src) {
    if (src->is_zip) mz_zip_reader_end(&src->zip);
}

static bool source_list(rom_source_t *src, dir_listing_t *out) {
    if (!src->is_zip) return list_dir(src->base, out);

    out->count = 0;
    mz_uint n = mz_zip_reader_get_num_files(&src->zip);
    for (mz_uint i = 0; i < n && out->count < MAX_DIR_FILES; i++) {
        if (mz_zip_reader_is_file_a_directory(&src->zip, i)) continue;
        char name[MAX_NAME_LEN];
        mz_zip_reader_get_filename(&src->zip, i, name, sizeof(name));
        /* Os romsets MAME são planos (sem subpastas), mas por via das
         * dúvidas: se vier "alguma/pasta/arquivo.bin", usa só o nome final —
         * é isso que os tokens comparam de qualquer forma. */
        const char *base = name;
        for (const char *p = name; *p != '\0'; p++) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        snprintf(out->names[out->count], MAX_NAME_LEN, "%s", base);
        out->zip_index[out->count] = i;
        out->count++;
    }
    return true;
}

/* Lê a entrada de índice `idx` de `listing` (que deve ter vindo de
 * `source_list(src, ...)`) inteira pra um buffer alocado com malloc — quem
 * chama tem que dar free(). */
static bool source_read_entry(rom_source_t *src, const dir_listing_t *listing, int idx,
                               u8 **out_buf, u32 *out_size) {
    if (!src->is_zip) {
        char path[1280];
        snprintf(path, sizeof(path), "%s/%s", src->base, listing->names[idx]);
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        long sz = file_size(f);
        if (sz < 0) { fclose(f); return false; }
        u8 *buf = (u8 *)malloc((size_t)sz > 0 ? (size_t)sz : 1);
        if (!buf) { fclose(f); return false; }
        if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            fclose(f); free(buf); return false;
        }
        fclose(f);
        *out_buf = buf;
        *out_size = (u32)sz;
        return true;
    }

    size_t sz = 0;
    void *p = mz_zip_reader_extract_to_heap(&src->zip, listing->zip_index[idx], &sz, 0);
    if (!p) return false;
    *out_buf = (u8 *)p;
    *out_size = (u32)sz;
    return true;
}

/* Acha, em `listing`, o índice do primeiro nome que bate (case-insensitive)
 * com `want`. -1 se não achar. */
static int find_exact_name(const dir_listing_t *listing, const char *want) {
    char want_lower[MAX_NAME_LEN];
    for (int i = 0; i < listing->count; i++) {
        char lower[MAX_NAME_LEN];
        size_t j = 0;
        for (; listing->names[i][j] != '\0' && j + 1 < sizeof(lower); j++)
            lower[j] = (char)tolower((unsigned char)listing->names[i][j]);
        lower[j] = '\0';
        size_t k = 0;
        for (; want[k] != '\0' && k + 1 < sizeof(want_lower); k++)
            want_lower[k] = (char)tolower((unsigned char)want[k]);
        want_lower[k] = '\0';
        if (strcmp(lower, want_lower) == 0) return i;
    }
    return -1;
}

/* Tenta cada nome de `names` (em ordem de prioridade), devolve o índice do
 * primeiro que casar em `listing`, ou -1. */
static int find_first_of(const dir_listing_t *listing, const char *const *names) {
    for (int n = 0; names[n] != NULL; n++) {
        int idx = find_exact_name(listing, names[n]);
        if (idx >= 0) return idx;
    }
    return -1;
}

static void to_lower_copy(const char *src, char *dst, size_t dst_size) {
    size_t i = 0;
    for (; src[i] != '\0' && i + 1 < dst_size; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Verdadeiro se `filename` (sem caminho) tem, entre os segmentos separados
 * por '-', '_' ou '.', algum EXATAMENTE igual a `token` (case-insensitive). */
static bool filename_has_token(const char *filename, const char *token) {
    char lower[MAX_NAME_LEN];
    to_lower_copy(filename, lower, sizeof(lower));
    char tok_lower[32];
    to_lower_copy(token, tok_lower, sizeof(tok_lower));

    char *seg = lower;
    for (char *p = lower; ; p++) {
        if (*p == '-' || *p == '_' || *p == '.' || *p == '\0') {
            bool end = (*p == '\0');
            char saved = *p;
            *p = '\0';
            bool match = (strcmp(seg, tok_lower) == 0);
            *p = saved;
            if (match) return true;
            if (end) break;
            seg = p + 1;
        }
    }
    return false;
}

/* Concatena, na ordem dos tokens dados, os arquivos encontrados por token
 * (ex: "p1","p2" ou "c1","c3","c5","c7"). Tokens sem arquivo correspondente
 * são pulados (partes opcionais, ex: v3/v4 em jogos menores). */
/* Quantos ARQUIVOS a última chamada de load_concat_by_tokens concatenou.
 * Usado pelo Achado #67 pra distinguir "um arquivo de 2MB carregado com
 * ROM_CONTINUE" (metades trocadas) de "dois arquivos de 1MB em sequência"
 * (sem troca). */
static int g_last_parts = 0;

static bool load_concat_by_tokens(rom_source_t *src, const char *src_label, const dir_listing_t *listing,
                                   const char *const *tokens, int token_count,
                                   u8 **out_buf, u32 *out_size, u32 max_size,
                                   char *out_err, u32 out_err_size, const char *what,
                                   bool required) {
    u8 *buf = (u8 *)calloc(1, max_size);
    if (!buf) { set_err(out_err, out_err_size, "falha de alocacao de memoria"); return false; }
    u32 offset = 0;
    int found_any = 0;
    int n_parts = 0;   /* quantos ARQUIVOS entraram (ver Achado #67) */
    /* Marca arquivo já consumido por um token anterior NESTE grupo (ex:
     * "250-p2.ep1" bate tanto com "p2" quanto com um alias "ep1") pra não
     * concatenar o MESMO arquivo duas vezes. */
    bool used[MAX_DIR_FILES] = {0};

    for (int t = 0; t < token_count; t++) {
        for (int i = 0; i < listing->count; i++) {
            if (used[i]) continue;
            if (!filename_has_token(listing->names[i], tokens[t])) continue;

            u8 *entry = NULL;
            u32 entry_size = 0;
            if (!source_read_entry(src, listing, i, &entry, &entry_size)) continue;

            if (entry_size == 0 || offset + entry_size > max_size) {
                free(entry); free(buf);
                char msg[300];
                snprintf(msg, sizeof(msg), "%s: '%s' excede o limite de %u bytes", what, listing->names[i], max_size);
                set_err(out_err, out_err_size, msg);
                return false;
            }
            memcpy(buf + offset, entry, entry_size);
            free(entry);
            offset += entry_size;
            found_any = 1;
            n_parts++;
            used[i] = true;
            break; /* um arquivo por token */
        }
    }

    if (!found_any) {
        free(buf);
        if (required) {
            char msg[300];
            snprintf(msg, sizeof(msg), "nenhuma parte de %s encontrada em %s", what, src_label);
            set_err(out_err, out_err_size, msg);
            return false;
        }
        *out_buf = NULL; *out_size = 0;
        return true;
    }
    *out_buf = buf;
    *out_size = offset;
    g_last_parts = n_parts;
    return true;
}

void neogeo_romset_free(neogeo_romset_t *rs) {
    if (!rs) return;
    free(rs->bios); rs->bios = NULL; rs->bios_size = 0;
    free(rs->audiobios); rs->audiobios = NULL; rs->audiobios_size = 0;
    free(rs->sfix); rs->sfix = NULL; rs->sfix_size = 0;
    free(rs->zoomy); rs->zoomy = NULL; rs->zoomy_size = 0;
    free(rs->p);    rs->p = NULL;    rs->p_size = 0;
    free(rs->s);    rs->s = NULL;    rs->s_size = 0;
    free(rs->m);    rs->m = NULL;    rs->m_size = 0;
    free(rs->v);    rs->v = NULL;    rs->v_size = 0;
    free(rs->vb);   rs->vb = NULL;   rs->vb_size = 0;
    free(rs->c_odd);  rs->c_odd = NULL;  rs->c_odd_size = 0;
    free(rs->c_even); rs->c_even = NULL; rs->c_even_size = 0;
}

bool neogeo_romset_load_dir_ex(neogeo_romset_t *rs, const char *cart_dir,
                                const char *bios_dir,
                                char *out_err, u32 out_err_size) {
    if (!rs || !cart_dir) {
        set_err(out_err, out_err_size, "parametros invalidos");
        return false;
    }
    memset(rs, 0, sizeof(*rs));
    if (!bios_dir) bios_dir = cart_dir; /* compatibilidade: tudo numa pasta só */

    rom_source_t bios_src, cart_src;
    bool same_source = (strcmp(cart_dir, bios_dir) == 0);

    if (!source_open(&bios_src, bios_dir, out_err, out_err_size)) {
        return false;
    }
    /* Só abre a fonte do cartucho de novo se for um caminho DIFERENTE da
     * BIOS — reabrir o mesmo .zip duas vezes é desperdício e, em teoria,
     * dois mz_zip_archive apontando pro mesmo arquivo em modo leitura não
     * tem problema, mas não há motivo pra pagar esse custo. */
    if (same_source) {
        cart_src = bios_src;
    } else if (!source_open(&cart_src, cart_dir, out_err, out_err_size)) {
        source_close(&bios_src);
        return false;
    }

    /* BIOS: nomes conhecidos, por prioridade (nao ha token comum entre
     * versoes). "sm1.sm1" de propósito NAO entra aqui: é a sound ROM (M1)
     * do sistema, não a BIOS do 68000 — apesar do nome parecido. */
    static const char *bios_names[] = {
        "neogeo.rom", "sp-s2.sp1", "sp-s.sp1", "sp-s3.sp1", "sp-u2.sp1",
        "sp-e.sp1", "sp-j2.sp1", "neo-epo.bin", "bios.bin", NULL
    };
    dir_listing_t bios_listing;
    if (!source_list(&bios_src, &bios_listing)) {
        char msg[300];
        snprintf(msg, sizeof(msg), "nao foi possivel listar a BIOS '%s'", bios_dir);
        set_err(out_err, out_err_size, msg);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }

    {
        /* Achado #76: alguns cartuchos trazem a PRÓPRIA BIOS dentro do zip e
         * ela SUBSTITUI a do sistema — é uma placa diferente, não um extra.
         * Aqui só o `irrmaze` ("The Irritating Maze", 236-bios.sp1), que usa
         * trackball e tem BIOS própria pra isso. Sem tratar esse caso ele
         * rodava com a BIOS euro e era o ÚNICO jogo da varredura que divergia
         * cedo do MAME (62349 de 71680 pixels no vblank 200). */
        rom_source_t *bsrc = &bios_src;
        dir_listing_t *blist = &bios_listing;
        dir_listing_t cart_bios_listing;
        int idx = -1;
        if (!same_source && source_list(&cart_src, &cart_bios_listing)) {
            for (int i = 0; i < cart_bios_listing.count; i++) {
                char lower[MAX_NAME_LEN];
                to_lower_copy(cart_bios_listing.names[i], lower, sizeof(lower));
                size_t n = strlen(lower);
                if (n > 4 && strcmp(lower + n - 4, ".sp1") == 0) {
                    bsrc = &cart_src; blist = &cart_bios_listing; idx = i;
                    break;
                }
            }
        }
        if (idx < 0) { bsrc = &bios_src; blist = &bios_listing; idx = find_first_of(&bios_listing, bios_names); }
        if (idx >= 0 && getenv("NEOGEO_TRACE_BIOS")) {
            printf("[bios] usando '%s' (%s)\n", blist->names[idx],
                   (bsrc == &cart_src) ? "do cartucho" : "do sistema");
            fflush(stdout);
        }
        u8 *entry = NULL; u32 entry_size = 0;
        if (idx < 0 || !source_read_entry(bsrc, blist, idx, &entry, &entry_size)) {
            char msg[300];
            snprintf(msg, sizeof(msg), "arquivo de BIOS nao encontrado em %s", bios_dir);
            set_err(out_err, out_err_size, msg);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }
        if (entry_size == 0 || entry_size > NEOGEO_BIOS_SIZE) {
            char msg[300];
            snprintf(msg, sizeof(msg), "BIOS ('%s'): tamanho invalido (%u bytes)", blist->names[idx], entry_size);
            set_err(out_err, out_err_size, msg);
            free(entry);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }
        rs->bios = (u8 *)calloc(1, NEOGEO_BIOS_SIZE);
        if (!rs->bios) {
            free(entry);
            set_err(out_err, out_err_size, "falha de alocacao de memoria");
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }
        memcpy(rs->bios, entry, entry_size);
        free(entry);
        rs->bios_size = entry_size;

        /* Algumas distribuições da BIOS (esta incluída) vêm com os bytes
         * trocados em pares dentro de cada word de 16 bits ("byte-swapped").
         * Detectamos isso pelo vetor de PC (bytes 4-7): sem troca, um PC
         * válido no formato $00C0xxxx apareceria como "00 C0 xx xx"; com
         * troca, aparece como "C0 00 xx xx" (byte alto e baixo invertidos
         * dentro da word alta). Confirmado contra a NeoGeo Development Wiki
         * (Boot process: PC inicial real é $00C00402) — ver
         * docs/ARCHITECTURE.md. Se detectado, desfaz a troca em toda a
         * imagem antes de usar. */
        if (rs->bios_size >= 8 && rs->bios[4] == 0xC0 && rs->bios[5] == 0x00) {
            for (u32 i = 0; i + 1 < rs->bios_size; i += 2) {
                u8 t = rs->bios[i];
                rs->bios[i] = rs->bios[i + 1];
                rs->bios[i + 1] = t;
            }
        }
    }

    /* Audio BIOS (sm1.sm1) — driver de som compartilhado do Z80, mesmo
     * diretório da BIOS do 68k. Não precisa de byte-swap (é um programa
     * Z80, não 68k). Opcional (não falha se ausente — jogos muito antigos
     * ou romsets incompletos podem não ter), mas sem ela o Z80 não roda
     * (ver comentário em core/src/system/neogeo.c). */
    {
        static const char *audiobios_names[] = { "sm1.sm1", "sm1.bin", NULL };
        int idx = find_first_of(&bios_listing, audiobios_names);
        if (idx >= 0) {
            u8 *entry = NULL; u32 entry_size = 0;
            if (source_read_entry(&bios_src, &bios_listing, idx, &entry, &entry_size) &&
                entry_size > 0 && entry_size <= NEOGEO_BIOS_SIZE) {
                rs->audiobios = (u8 *)calloc(1, NEOGEO_BIOS_SIZE);
                if (rs->audiobios) {
                    memcpy(rs->audiobios, entry, entry_size);
                    rs->audiobios_size = entry_size;
                }
                free(entry);
            } else {
                free(entry);
            }
        }
    }

    /* SFIX (fix/texto PRÓPRIO da BIOS — Achado #27, ver docs/ROADMAP.md):
     * mesmo diretório da BIOS, mesmo padrão do audiobios acima (opcional,
     * mas sem ele a fix layer usa a S ROM do cartucho pra TUDO, inclusive
     * telas desenhadas pela própria BIOS — dando glifos corrompidos toda
     * vez que a BIOS usa códigos de tile fora da faixa ASCII básica). */
    {
        static const char *sfix_names[] = { "sfix.sfix", "sfix.bin", NULL };
        int idx = find_first_of(&bios_listing, sfix_names);
        if (idx >= 0) {
            u8 *entry = NULL; u32 entry_size = 0;
            if (source_read_entry(&bios_src, &bios_listing, idx, &entry, &entry_size) && entry_size > 0) {
                rs->sfix = entry; /* já é um malloc próprio, adota direto */
                rs->sfix_size = entry_size;
            } else {
                free(entry);
            }
        }
    }

    /* Achado #50: "000-lo.lo" — ROM de lookup do zoom vertical de sprite
     * (região "spritegen:zoomy" no MAME, carregada por `ROM_Y_ZOOM`). Sem
     * ela o shrink vertical fica numa aproximação linear em vez da curva
     * real do chip. Procurada no diretório/zip da BIOS E no do cartucho,
     * porque os romsets que temos trazem uma cópia em cada um. */
    {
        int idx = find_exact_name(&bios_listing, "000-lo.lo");
        rom_source_t *found_src = &bios_src;
        dir_listing_t cart_listing_probe;
        bool have_cart_probe = false;
        if (idx < 0 && !same_source) {
            if (source_list(&cart_src, &cart_listing_probe)) {
                have_cart_probe = true;
                idx = find_exact_name(&cart_listing_probe, "000-lo.lo");
                found_src = &cart_src;
            }
        }
        if (idx >= 0) {
            u8 *entry = NULL; u32 entry_size = 0;
            const dir_listing_t *lst = (found_src == &bios_src) ? &bios_listing : &cart_listing_probe;
            if (source_read_entry(found_src, lst, idx, &entry, &entry_size) && entry_size > 0) {
                rs->zoomy = entry;
                rs->zoomy_size = entry_size;
            } else {
                free(entry);
            }
        }
        (void)have_cart_probe;
    }

    /* Cartucho: escaneia cart_dir/cart.zip uma vez, casa tudo por token. */
    dir_listing_t cart_listing;
    if (!source_list(&cart_src, &cart_listing)) {
        char msg[300];
        snprintf(msg, sizeof(msg), "nao foi possivel listar o cartucho '%s'", cart_dir);
        set_err(out_err, out_err_size, msg);
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }

    /* Achado #72: detecta o King of Fighters '99 (chip SMA+CMC42) ANTES de
     * tocar na P ROM — o cabeçalho "NEO-GEO"/NGH que a gente normalmente lê
     * pra identificar cartucho especial (Metal Slug X, V-Liner) só existe
     * DEPOIS da descriptografia aqui, então a detecção tem que ser por
     * NOME DE ARQUIVO: todo dump de kof99 tem o prefixo "251" (segmento
     * exato, tipo "251-p1.p1") E um arquivo do chip customizado
     * ("*.neo-sma", ex: "ka.neo-sma") — a combinação das duas é específica
     * o bastante pra não confundir com outro cartucho. */
    /* Achado #79: generalizado pra todos os cartuchos SMA. Mesma ideia — o
     * número do conjunto no nome ("251", "253"...) + a presença do arquivo do
     * chip ("*.neo-sma") — e o layout ABSOLUTO de cada P ROM vem do
     * `ROM_START(...)` de cada jogo no neogeo.cpp do MAME. */
    typedef struct { const char *token; bool is_substr; u32 offset; u32 max_len; } sma_part_t;
    static const sma_part_t parts_4m[] = {   /* kof99, mslug3, kof2000: 2 x 4MB */
        { "neo-sma", true,  0x0C0000u, 0x040000u },
        { "p1",      false, 0x100000u, 0x400000u },
        { "p2",      false, 0x500000u, 0x400000u },
    };
    static const sma_part_t parts_garou[] = { /* garou: 4 x 2MB */
        { "neo-sma", true,  0x0C0000u, 0x040000u },
        { "p1",      false, 0x100000u, 0x200000u },
        { "p2",      false, 0x300000u, 0x200000u },
        { "p3",      false, 0x500000u, 0x200000u },
        { "p4",      false, 0x700000u, 0x200000u },
    };
    static const struct {
        const char *set; u8 type; u8 fix_bank; const sma_part_t *parts; int n_parts;
    } sma_carts[] = {
        { "251", NEOGEO_SMA_KOF99,   0, parts_4m,    3 },
        { "253", NEOGEO_SMA_GAROU,   1, parts_garou, 5 },
        { "256", NEOGEO_SMA_MSLUG3,  1, parts_4m,    3 },
        { "257", NEOGEO_SMA_KOF2000, 2, parts_4m,    3 },
    };
    rs->is_kof99 = false;
    rs->sma_type = NEOGEO_SMA_NONE;
    rs->fix_bank_type = 0;
    const sma_part_t *sma_parts = NULL;
    int sma_n_parts = 0;
    {
        bool has_neosma = false;
        for (int i = 0; i < cart_listing.count; i++) {
            char lower[MAX_NAME_LEN];
            to_lower_copy(cart_listing.names[i], lower, sizeof(lower));
            if (strstr(lower, "neo-sma") != NULL) has_neosma = true;
        }
        for (size_t k = 0; has_neosma && k < sizeof(sma_carts) / sizeof(sma_carts[0]); k++) {
            for (int i = 0; i < cart_listing.count; i++) {
                if (filename_has_token(cart_listing.names[i], sma_carts[k].set)) {
                    rs->sma_type = sma_carts[k].type;
                    rs->fix_bank_type = sma_carts[k].fix_bank;
                    sma_parts = sma_carts[k].parts;
                    sma_n_parts = sma_carts[k].n_parts;
                    break;
                }
            }
            if (rs->sma_type != NEOGEO_SMA_NONE) break;
        }
        rs->is_kof99 = (rs->sma_type == NEOGEO_SMA_KOF99);
    }

    /* Achado #83: família CMC50 / PCM2 / PVC. Tudo vem do `decrypt_all` de
     * cada cartucho (cmc.cpp, pcm2.cpp, pvc.cpp, kof2k2.cpp) e do
     * `ROM_START(...)` correspondente no neogeo.cpp do MAME:
     *   p68k   — embaralhamento da P ROM
     *   load32 — P1/P2 intercalados palavra a palavra (ROM_LOAD32_WORD_SWAP)
     *   key    — chave do CMC50 no gráfico (prot_cmc.h)
     *   pcm    — V ROMs: 'd' = pcm2 decrypt(valor), 's' = pcm2 swap(valor)
     *   fixed  — tamanho declarado da região "fixed" (0x80000 = em banco)
     * Identificação: primeiro pelo número do conjunto no arquivo da P1 (o
     * kof2001 usa uma M1 chamada "265-262-m1", que casaria com o kof2002),
     * depois em qualquer arquivo (o pnyaa chama a P1 de "pn202.p1"). */
    enum { P68K_NONE, P68K_KOF2002, P68K_SAMSHO5, P68K_SAMSH5SP, P68K_MSLUG5, P68K_SVC, P68K_KOF2003 };
    typedef struct {
        const char *set; u8 p68k; u8 load32; u8 key; char pcm; u8 pcm_val;
        u32 fixed; u8 fix_bank; bool pvc; bool cart_ram; bool no_half_swap; u32 p_fill_to;
    } cmc50_cart_t;
    static const cmc50_cart_t cmc50_carts[] = {
        /* set    p68k           l32  key   pcm  val  fixed     fb  pvc    ram    nohalf  fill */
        { "262", P68K_NONE,     0, 0x1E, 0,   0,  0x20000u, 0, false, false, false, 0 },         /* kof2001  */
        { "263", P68K_NONE,     0, 0x31, 'd', 8,  0x80000u, 1, false, false, false, 0 },         /* mslug4   */
        { "264", P68K_NONE,     0, 0x3F, 'd', 16, 0x20000u, 1, false, false, true,  0 },         /* rotd     */
        { "265", P68K_KOF2002,  0, 0xEC, 's', 0,  0x20000u, 0, false, false, false, 0 },         /* kof2002  */
        { "267", P68K_NONE,     0, 0x2E, 'd', 4,  0x20000u, 1, false, false, false, 0 },         /* pnyaa    */
        { "268", P68K_MSLUG5,   1, 0x19, 's', 2,  0x20000u, 1, true,  false, false, 0 },         /* mslug5   */
        { "269", P68K_SVC,      1, 0x57, 's', 3,  0x80000u, 2, true,  false, false, 0 },         /* svc      */
        { "270", P68K_SAMSHO5,  0, 0x0F, 's', 4,  0x20000u, 1, false, false, false, 0 },         /* samsho5  */
        { "271", P68K_KOF2003,  1, 0x9D, 's', 5,  0x80000u, 2, true,  false, false, 0 },         /* kof2003  */
        { "272", P68K_SAMSH5SP, 0, 0x0D, 's', 6,  0x20000u, 1, false, false, false, 0 },         /* samsh5sp */
        { "008", P68K_NONE,     0, 0xAC, 0,   0,  0x20000u, 1, false, true,  false, 0x200000u }, /* jockeygp */
    };
    const cmc50_cart_t *c50 = NULL;
    if (rs->sma_type == NEOGEO_SMA_NONE) {
        for (int pass = 0; pass < 2 && !c50; pass++) {
            for (size_t k = 0; k < sizeof(cmc50_carts) / sizeof(cmc50_carts[0]) && !c50; k++) {
                for (int i = 0; i < cart_listing.count; i++) {
                    if (pass == 0 && !filename_has_token(cart_listing.names[i], "p1")) continue;
                    if (filename_has_token(cart_listing.names[i], cmc50_carts[k].set)) { c50 = &cmc50_carts[k]; break; }
                }
            }
        }
        if (c50) {
            rs->fix_bank_type = (c50->fixed > 0x20000u) ? c50->fix_bank : 0;
            rs->is_pvc = c50->pvc;
            rs->has_cart_ram = c50->cart_ram;
        }
    }

    if (c50 && c50->load32) {
        /* ROM_LOAD32_WORD_SWAP(p1, 0) + ROM_LOAD32_WORD_SWAP(p2, 2): cada
         * palavra da P1 vai em [4k,4k+1] e cada palavra da P2 em [4k+2,4k+3],
         * já com o byte-swap. O kof2003 ainda tem uma P3 de 1MB em $800000. */
        const bool has_p3 = (c50->p68k == P68K_KOF2003);
        rs->p_size = has_p3 ? 0x900000u : 0x800000u;
        rs->p = (u8 *)calloc(1, rs->p_size);
        static const char *const parts[3] = { "p1", "p2", "p3" };
        bool ok = (rs->p != NULL);
        for (int part = 0; ok && part < (has_p3 ? 3 : 2); part++) {
            int idx = -1;
            for (int i = 0; i < cart_listing.count; i++)
                if (filename_has_token(cart_listing.names[i], parts[part])) { idx = i; break; }
            u8 *entry = NULL; u32 entry_size = 0;
            if (idx < 0 || !source_read_entry(&cart_src, &cart_listing, idx, &entry, &entry_size)) {
                char msg[300];
                snprintf(msg, sizeof(msg), "PVC: parte '%s' da P ROM nao encontrada", parts[part]);
                set_err(out_err, out_err_size, msg);
                ok = false; break;
            }
            if (part < 2) {
                u32 off = (part == 0) ? 0u : 2u;
                for (u32 w = 0; w + 1u < entry_size && 2u * w + off + 1u < 0x800000u; w += 2u) {
                    rs->p[2u * w + off]      = entry[w + 1u];
                    rs->p[2u * w + off + 1u] = entry[w];
                }
            } else {
                for (u32 w = 0; w + 1u < entry_size && w + 1u < 0x100000u; w += 2u) {
                    rs->p[0x800000u + w]      = entry[w + 1u];
                    rs->p[0x800000u + w + 1u] = entry[w];
                }
            }
            free(entry);
        }
        if (!ok) {
            neogeo_romset_free(rs);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }
    } else if (rs->sma_type != NEOGEO_SMA_NONE) {
        /* P ROM SMA: buffer de 9MB (1MB fixo + 8MB bancados), com os arquivos
         * em offsets ABSOLUTOS fixos — não é concatenação simples como o resto
         * dos cartuchos. Ver prot_sma_kof99.c (Achados #72 e #79). */
        rs->p_size = 0x900000u;
        rs->p = (u8 *)calloc(1, rs->p_size);
        if (!rs->p) {
            set_err(out_err, out_err_size, "falha de alocacao de memoria (P ROM SMA)");
            neogeo_romset_free(rs);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }

        const sma_part_t *kof99_parts = sma_parts;
        bool ok = true;
        for (int part = 0; part < sma_n_parts && ok; part++) {
            int idx = -1;
            for (int i = 0; i < cart_listing.count; i++) {
                char lower[MAX_NAME_LEN];
                to_lower_copy(cart_listing.names[i], lower, sizeof(lower));
                bool match = kof99_parts[part].is_substr
                    ? (strstr(lower, kof99_parts[part].token) != NULL)
                    : filename_has_token(cart_listing.names[i], kof99_parts[part].token);
                if (match) { idx = i; break; }
            }
            if (idx < 0) {
                char msg[300];
                snprintf(msg, sizeof(msg), "SMA: parte '%s' da P ROM nao encontrada", kof99_parts[part].token);
                set_err(out_err, out_err_size, msg);
                ok = false;
                break;
            }
            u8 *entry = NULL; u32 entry_size = 0;
            if (!source_read_entry(&cart_src, &cart_listing, idx, &entry, &entry_size)) {
                char msg[300];
                snprintf(msg, sizeof(msg), "SMA: erro lendo '%s'", cart_listing.names[idx]);
                set_err(out_err, out_err_size, msg);
                ok = false;
                break;
            }
            /* mesmo byte-swap por word de sempre (ROM_LOAD16_WORD_SWAP),
             * aplicado só a este pedaço antes de colocá-lo no lugar certo. */
            for (u32 i = 0; i + 1u < entry_size; i += 2u) {
                u8 t = entry[i]; entry[i] = entry[i + 1u]; entry[i + 1u] = t;
            }
            u32 n = entry_size < kof99_parts[part].max_len ? entry_size : kof99_parts[part].max_len;
            memcpy(rs->p + kof99_parts[part].offset, entry, n);
            free(entry);
        }
        if (!ok) {
            neogeo_romset_free(rs);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }

        if (rs->sma_type == NEOGEO_SMA_KOF99 && getenv("NEOGEO_SMA_SELFTEST")) {
            /* Confere que o caminho genérico dá EXATAMENTE o mesmo resultado
             * que o do KOF99 (validado contra o MAME no Achado #72). */
            u8 *copy = (u8 *)malloc(rs->p_size);
            if (copy) {
                memcpy(copy, rs->p, rs->p_size);
                neogeo_kof99_decrypt_68k(copy);
                neogeo_sma_decrypt_68k(NEOGEO_SMA_KOF99, rs->p);
                u32 diffs = 0;
                for (u32 i = 0; i < rs->p_size; i++) if (copy[i] != rs->p[i]) diffs++;
                printf("[sma-selftest] generico vs kof99: %u bytes diferentes\n", diffs);
                for (u32 s = 0; s < 0x10000u; s++)
                    if (neogeo_sma_bank_base(NEOGEO_SMA_KOF99, (u16)s) != neogeo_kof99_bank_base((u16)s))
                        { printf("[sma-selftest] bank_base difere em sel=%04x\n", s); break; }
                fflush(stdout);
                free(copy);
            }
        } else {
            neogeo_sma_decrypt_68k(rs->sma_type, rs->p);
        }
    } else {
        /* "ep1"/"ep2" (sem "p1"/"p2" como token próprio): convenção alternativa
         * de nomenclatura vista em alguns dumps (ex: Puzzle Bobble, "d96-07.ep1")
         * pro mesmo chip de P ROM. Só entram em jogo quando nao ha p1/p2 "de
         * verdade" no diretorio (cada token so casa arquivos que o tenham como
         * segmento exato do nome). */
        /* Achado #66: a SEGUNDA metade da P ROM nem sempre se chama "p2".
         * Vários cartuchos usam `sp2` (Samurai Shodown: `045-p1.p1` +
         * `045-pg2.sp2`), outros `ep1`/`ep2` (Metal Slug X). Sem o token `sp2`,
         * o Samurai Shodown carregava só 1 dos 2 MB de programa: o banco 1
         * ($200000-$2FFFFF) vinha vazio, uma tabela de ponteiros devolvia lixo,
         * o 68000 tomava ERRO DE ENDEREÇO (vetor 3, `move.w (A0)+` com A0 ímpar)
         * e a máquina REINICIAVA no meio do attract — o sintoma era a tela verde
         * com texto embaralhado (que é a BIOS refazendo o teste de RAM).
         * ATENÇÃO: NÃO adicionar `sp1` aqui — é o nome das ROMs de BIOS
         * (`sp-s2.sp1`, `sp-45.sp1`...), que costumam estar no mesmo diretório. */
        /* Achado #106: o Samurai Shodown III MVS vem em 5 chips —
         * `087-epr.ep1..ep4` (512KB cada) + `087-p5.p5` (1MB) = 3MB
         * (`ROM_START(samsho3)` do MAME: ep1@$000000 ... ep4@$180000,
         * p5@$200000). Sem ep3/ep4/p5 só entrava 1MB, a janela de banco vinha
         * vazia e a BIOS parava em "WARNING / BANK ERROR". (O garou também tem
         * `ep3`/`ep4`, mas vai pelo ramo SMA acima.) */
        static const char *p_tokens[] = { "p1", "p2", "sp2", "ep1", "ep2", "ep3", "ep4", "p5" };
        if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, p_tokens, 8, &rs->p, &rs->p_size,
                                    NEOGEO_CART_P_MAX, out_err, out_err_size, "P ROM", true)) {
            neogeo_romset_free(rs);
            source_close(&bios_src);
            if (!same_source) source_close(&cart_src);
            return false;
        }

        /* Achado #39 (ver docs/ROADMAP.md): a P ROM precisa de DUAS transformações
         * que não estávamos fazendo — ambas confirmadas comparando byte a byte
         * contra o que o MAME real tem na memória (dump pelo debugger).
         *
         * 1) BYTE-SWAP por word. O MAME carrega essas ROMs com
         *    `ROM_LOAD16_WORD_SWAP`, ou seja, o arquivo está com os dois bytes
         *    de cada word de 16 bits trocados em relação à ordem que o 68000
         *    (big-endian) espera. Sem isso, TODO o código do cartucho é lixo.
         *
         * 2) TROCA DAS METADES quando a P ROM passa de 1MB. O idioma padrão do
         *    MAME pra esses cartuchos é
         *        ROM_LOAD16_WORD_SWAP( "p1", 0x100000, 0x100000 )
         *        ROM_CONTINUE(          0x000000, 0x100000 )
         *    isto é: o PRIMEIRO mega do arquivo vai pra $100000 da região, e o
         *    resto vai pro começo. Efeito prático num cartucho de 2MB como o
         *    Double Dragon: a SEGUNDA metade do arquivo é a que aparece em
         *    $000000-$0FFFFF pro 68000 (onde fica o cabeçalho "NEO-GEO" + o
         *    número NGH do jogo, lido pela BIOS em `$000108`).
         *    Comprovação: `"NEO-GEO"` está no offset 0x100100 do arquivo (depois
         *    do swap) e o MAME o mostra em $000100. */
        if (rs->p && rs->p_size >= 2u) {
            for (u32 i = 0; i + 1u < rs->p_size; i += 2u) {
                u8 t = rs->p[i]; rs->p[i] = rs->p[i + 1u]; rs->p[i + 1u] = t;
            }
        }
        /* Achado #67: a troca de metades acima NÃO vale pra todo cartucho grande
         * — vale só pro idioma de UM ARQUIVO carregado com `ROM_CONTINUE`.
         * Comparando as definições reais do MAME (`hash/neogeo.xml`):
         *   doubledr: 082-p1.p1 -> 0x100000, ROM_CONTINUE -> 0x000000  (TROCA)
         *   samsho:   045-p1.p1 -> 0x000000, 045-pg2.sp2 -> 0x100000   (não)
         *   mslugx:   250-p1.p1 -> 0x000000, 250-p2.ep1  -> 0x100000   (não)
         * Ou seja: com DOIS OU MAIS arquivos, eles entram em ordem e ponto.
         * Aplicar a troca nesses casos embaralhava o programa: no Samurai
         * Shodown o 68000 acabava tomando erro de endereço e a máquina
         * reiniciava (ver Achado #66). */
        /* Achado #83: exceção — o rotd tem UM arquivo de 8MB carregado direto
         * em $000000 (`ROM_LOAD16_WORD_SWAP("264-p1.p1", 0x000000, 0x800000)`),
         * sem `ROM_CONTINUE`. A regra "arquivo único grande = troca" não vale. */
        if (rs->p && rs->p_size > 0x100000u && g_last_parts == 1 && !(c50 && c50->no_half_swap)) {
            u32 first = 0x100000u;
            u32 rest = rs->p_size - first;
            u8 *tmp = (u8 *)malloc(rs->p_size);
            if (tmp) {
                memcpy(tmp, rs->p + first, rest);        /* resto -> início */
                memcpy(tmp + rest, rs->p, first);        /* 1º MB -> depois  */
                memcpy(rs->p, tmp, rs->p_size);
                free(tmp);
            }
        }

        /* Achado #82: KOF98 (MVS) — criptografia própria, bem mais antiga que
         * a SMA. Conjunto "242" com `p1` (2MB) + `sp2` (4MB), exatamente o
         * `ROM_START(kof98)` do MAME: p1@$000000, sp2@$200000 = 6MB. A versão
         * AES (`kof98h`) usa `242-pn1.p1` — token "pn1", não cai aqui. */
        /* Achado #83: jockeygp declara região de 2MB com o 2º MB preenchido
         * com 0xFF (`ROM_FILL(0x100000, 0x100000, 0xff)`). */
        if (c50 && c50->p_fill_to > rs->p_size && rs->p) {
            u8 *np = (u8 *)realloc(rs->p, c50->p_fill_to);
            if (np) {
                memset(np + rs->p_size, 0xFF, c50->p_fill_to - rs->p_size);
                rs->p = np;
                rs->p_size = c50->p_fill_to;
            }
        }

        rs->is_kof98 = false;
        if (rs->p && rs->p_size == 0x600000u) {
            bool has_242 = false, has_p1 = false;
            for (int i = 0; i < cart_listing.count; i++) {
                if (filename_has_token(cart_listing.names[i], "242")) has_242 = true;
                if (filename_has_token(cart_listing.names[i], "p1")) has_p1 = true;
            }
            if (has_242 && has_p1) {
                neogeo_kof98_decrypt_68k(rs->p);
                rs->is_kof98 = true;
            }
        }
    }

    static const char *s_tokens[] = { "s1" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, s_tokens, 1, &rs->s, &rs->s_size,
                                256u * 1024u, out_err, out_err_size, "S ROM", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }

    static const char *m_tokens[] = { "m1" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, m_tokens, 1, &rs->m, &rs->m_size,
                                NEOGEO_CART_M_MAX, out_err, out_err_size, "M1 (sound)", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }
    /* Achado #83: P ROM da família CMC50/PVC. Feito aqui (depois de todos os
     * caminhos de carga da P) pra valer tanto pro layout intercalado quanto
     * pro concatenado. */
    if (c50 && rs->p) {
        switch (c50->p68k) {
        case P68K_KOF2002:  if (rs->p_size >= 0x500000u) neogeo_kof2002_decrypt_68k(rs->p); break;
        case P68K_SAMSHO5:  if (rs->p_size >= 0x800000u) neogeo_samsho5_decrypt_68k(rs->p); break;
        case P68K_SAMSH5SP: if (rs->p_size >= 0x800000u) neogeo_samsh5sp_decrypt_68k(rs->p); break;
        case P68K_MSLUG5:   if (rs->p_size >= 0x800000u) neogeo_mslug5_decrypt_68k(rs->p); break;
        case P68K_SVC:      if (rs->p_size >= 0x800000u) neogeo_svc_decrypt_68k(rs->p); break;
        case P68K_KOF2003:  if (rs->p_size >= 0x900000u) neogeo_kof2003_decrypt_68k(rs->p); break;
        default: break;
        }
    }

    /* Achado #81: M1 cifrada pelo CMC50 (NEO_BIOS_AUDIO_ENCRYPTED_* no MAME). */
    if ((rs->sma_type == NEOGEO_SMA_KOF2000 || c50) && rs->m && rs->m_size > 0) {
        u8 *dec = (u8 *)malloc(0x80000u);
        if (dec) {
            neogeo_cmc50_m1_decrypt(rs->m, rs->m_size, dec);
            free(rs->m);
            rs->m = dec;
            rs->m_size = 0x80000u;
        }
    }

    /* O YM2610 tem DOIS barramentos de amostra separados: ADPCM-A (vozes e
     * percussão, 6 canais) e ADPCM-B (um canal só, com taxa variável). No
     * MAME são as regiões `ymsnd:adpcma` e `ymsnd:adpcmb`. A convenção de
     * nomes dos cartuchos: quando o jogo usa os dois, as ROMs de A viram
     * `v11/v12/...` e as de B `v21/v22/...`; quando só usa A, ficam
     * `v1/v2/...`. A maioria dos jogos (todos os que temos aqui) só usa A. */
    static const char *v_tokens[] = { "v1", "v2", "v3", "v4", "v11", "v12", "v13", "v14" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, v_tokens, 8, &rs->v, &rs->v_size,
                                NEOGEO_CART_V_MAX, out_err, out_err_size, "V ROMs (ADPCM-A)", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }
    /* Achado #83: V ROMs cifradas pelo NEO-PCM2. O `swap` opera sobre 16MB
     * fixos no MAME (região adpcma de 0x1000000) — só aplicamos se o
     * conjunto carregado tem exatamente esse tamanho. */
    if (c50 && rs->v && rs->v_size > 0) {
        if (c50->pcm == 'd') neogeo_pcm2_decrypt(rs->v, rs->v_size, c50->pcm_val);
        else if (c50->pcm == 's' && rs->v_size == 0x1000000u) neogeo_pcm2_swap(rs->v, c50->pcm_val);
    }

    static const char *vb_tokens[] = { "v21", "v22", "v23", "v24" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, vb_tokens, 4, &rs->vb, &rs->vb_size,
                                NEOGEO_CART_V_MAX, out_err, out_err_size, "V ROMs (ADPCM-B)", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }

    /* C ROMs: impares (c1,c3,c5,c7) = bitplanes 0-1; pares (c2,c4,c6,c8) =
     * bitplanes 2-3 do MESMO tile — ver core/src/video/lspc_sprites.c. */
    static const char *c_odd_tokens[] = { "c1", "c3", "c5", "c7" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, c_odd_tokens, 4, &rs->c_odd, &rs->c_odd_size,
                                32u * 1024u * 1024u, out_err, out_err_size, "C ROMs impares (bitplanes 0-1)", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }
    int c_odd_parts = g_last_parts;
    static const char *c_even_tokens[] = { "c2", "c4", "c6", "c8" };
    if (!load_concat_by_tokens(&cart_src, cart_dir, &cart_listing, c_even_tokens, 4, &rs->c_even, &rs->c_even_size,
                                32u * 1024u * 1024u, out_err, out_err_size, "C ROMs pares (bitplanes 2-3)", false)) {
        neogeo_romset_free(rs);
        source_close(&bios_src);
        if (!same_source) source_close(&cart_src);
        return false;
    }
    int c_even_parts = g_last_parts;

    /* Achado #97: C ROMs de 2MB carregadas em DUAS metades (`ROM_CONTINUE`).
     * Em 8 jogos do acervo o MAME faz, por exemplo no aof:
     *     ROM_LOAD16_BYTE( "044-c1.c1", 0x000000, 0x100000 )
     *     ROM_CONTINUE(                 0x400000, 0x100000 )
     *     ROM_LOAD16_BYTE( "044-c3.c3", 0x200000, 0x100000 )
     *     ROM_CONTINUE(                 0x600000, 0x100000 )
     * ou seja, a 2a metade de cada chip vai 4MB adiante na região — e nós
     * concatenávamos os arquivos inteiros, trocando o meio da área de
     * gráficos. Sintoma: personagens grandes do AOF embaralhados, o estádio do
     * Super Sidekicks em pedaços. Tabela GERADA do `ROM_START` de cada jogo
     * no neogeo.cpp (tamanho da região + cada pedaço: token, offset na
     * região, offset no arquivo, tamanho). Em coordenadas do nosso buffer de
     * plano (ímpar = c1/c3..., par = c2/c4...), offset na região >> 1.
     * Achado #109: também vale pra C ROM com BURACO na região (kof95, whp,
     * kizuna: `c7/c8` em $1800000 com o par anterior terminando antes) —
     * concatenar deslocava os tiles do último par e eles viravam transparentes. */
    {
        typedef struct { const char *tok; u32 region_off, file_off, len; } csplit_chunk_t;
        typedef struct { const char *set; u32 region; int n; csplit_chunk_t ch[8]; } csplit_t;
        static const csplit_t csplit[] = {
    { "039", 0x0600000u, 6, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x080000u}, {"c4", 0x200001u, 0x000000u, 0x080000u} } }, /* kotm2 */
    { "040", 0x0600000u, 6, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x080000u}, {"c4", 0x200001u, 0x000000u, 0x080000u} } }, /* sengoku2 */
    { "043", 0x0800000u, 8, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x100000u}, {"c3", 0x600000u, 0x100000u, 0x100000u}, {"c4", 0x200001u, 0x000000u, 0x100000u}, {"c4", 0x600001u, 0x100000u, 0x100000u} } }, /* 3countb */
    { "044", 0x0800000u, 8, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x100000u}, {"c3", 0x600000u, 0x100000u, 0x100000u}, {"c4", 0x200001u, 0x000000u, 0x100000u}, {"c4", 0x600001u, 0x100000u, 0x100000u} } }, /* aof */
    { "047", 0x0800000u, 8, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x100000u}, {"c3", 0x600000u, 0x100000u, 0x100000u}, {"c4", 0x200001u, 0x000000u, 0x100000u}, {"c4", 0x600001u, 0x100000u, 0x100000u} } }, /* fatfury2 */
    { "051", 0x0600000u, 4, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u} } }, /* viewpoin */
    { "052", 0x0600000u, 4, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u} } }, /* ssideki */
    { "053", 0x0600000u, 6, { {"c1", 0x000000u, 0x000000u, 0x100000u}, {"c1", 0x400000u, 0x100000u, 0x100000u}, {"c2", 0x000001u, 0x000000u, 0x100000u}, {"c2", 0x400001u, 0x100000u, 0x100000u}, {"c3", 0x200000u, 0x000000u, 0x100000u}, {"c4", 0x200001u, 0x000000u, 0x100000u} } }, /* wh1 */
    { "084", 0x1a00000u, 8, { {"c1", 0x0000000u, 0x000000u, 0x400000u}, {"c2", 0x0000001u, 0x000000u, 0x400000u}, {"c3", 0x0800000u, 0x000000u, 0x400000u}, {"c4", 0x0800001u, 0x000000u, 0x400000u}, {"c5", 0x1000000u, 0x000000u, 0x200000u}, {"c6", 0x1000001u, 0x000000u, 0x200000u}, {"c7", 0x1800000u, 0x000000u, 0x100000u}, {"c8", 0x1800001u, 0x000000u, 0x100000u} } }, /* kof95: C ROM com buraco na regiao (Achado #109) */
    { "090", 0x1c00000u, 8, { {"c1", 0x0000000u, 0x000000u, 0x400000u}, {"c2", 0x0000001u, 0x000000u, 0x400000u}, {"c3", 0x0800000u, 0x000000u, 0x200000u}, {"c4", 0x0800001u, 0x000000u, 0x200000u}, {"c5", 0x1000000u, 0x000000u, 0x200000u}, {"c6", 0x1000001u, 0x000000u, 0x200000u}, {"c7", 0x1800000u, 0x000000u, 0x200000u}, {"c8", 0x1800001u, 0x000000u, 0x200000u} } }, /* whp: C ROM com buraco na regiao (Achado #109) */
    { "216", 0x1c00000u, 8, { {"c1", 0x0000000u, 0x000000u, 0x200000u}, {"c2", 0x0000001u, 0x000000u, 0x200000u}, {"c3", 0x0800000u, 0x000000u, 0x400000u}, {"c4", 0x0800001u, 0x000000u, 0x400000u}, {"c5", 0x1000000u, 0x000000u, 0x200000u}, {"c6", 0x1000001u, 0x000000u, 0x200000u}, {"c7", 0x1800000u, 0x000000u, 0x200000u}, {"c8", 0x1800001u, 0x000000u, 0x200000u} } }, /* kizuna: C ROM com buraco na regiao (Achado #109); identificado por 216-c3 (059-c1 e do savagere) */
        };
        const csplit_t *cs = NULL;
        for (size_t k = 0; k < sizeof(csplit) / sizeof(csplit[0]) && !cs; k++)
            for (int i = 0; i < cart_listing.count; i++)
                if (filename_has_token(cart_listing.names[i], csplit[k].set) &&
                    filename_has_token(cart_listing.names[i],
                        /* kizuna usa c1/c2 do savagere (059); o chip próprio é 216-c3 */
                        strcmp(csplit[k].set, "216") == 0 ? "c3" : "c1")) { cs = &csplit[k]; break; }
        if (cs) {
            u32 plane = cs->region / 2u;
            u8 *odd = (u8 *)calloc(1, plane), *even = (u8 *)calloc(1, plane);
            bool ok = odd && even;
            for (int c = 0; ok && c < cs->n; c++) {
                const csplit_chunk_t *ch = &cs->ch[c];
                int idx = -1;
                for (int i = 0; i < cart_listing.count; i++)
                    if (filename_has_token(cart_listing.names[i], ch->tok)) { idx = i; break; }
                u8 *entry = NULL; u32 entry_size = 0;
                if (idx < 0 || !source_read_entry(&cart_src, &cart_listing, idx, &entry, &entry_size)) { ok = false; break; }
                u8 *dst = (ch->region_off & 1u) ? even : odd;
                u32 poff = ch->region_off >> 1;
                if (ch->file_off + ch->len <= entry_size && poff + ch->len <= plane)
                    memcpy(dst + poff, entry + ch->file_off, ch->len);
                else ok = false;
                free(entry);
            }
            if (ok) {
                free(rs->c_odd); free(rs->c_even);
                rs->c_odd = odd; rs->c_even = even;
                rs->c_odd_size = rs->c_even_size = plane;
            } else { free(odd); free(even); }
        }
    }

    /* Achado #72: descriptografia CMC42 das C ROMs do KOF99. O algoritmo do
     * MAME opera sobre a região COMBINADA (odd+even intercalados byte a
     * byte, exatamente como o hardware físico entrelaça os dois chips do
     * par — `ROM_LOAD16_BYTE("c1",0,...)` / `ROM_LOAD16_BYTE("c2",1,...)`),
     * com os 4 pares (c1/c2, c3/c4, c5/c6, c7/c8) emendados em sequência.
     * Montamos esse layout temporariamente, descriptografamos, e desfazemos
     * a intercalação de volta pra c_odd/c_even — assim o resto do código
     * (`lspc_sprites.c`) nem precisa saber que isso aconteceu. Os dados da
     * fix layer (sem s1.s1 separado nesses cartuchos) vêm do FINAL dessa
     * mesma região, depois de descriptografada. */
    /* Achado #75: a mesma CMC42 vale pra mais 7 cartuchos aqui — só muda a
     * CHAVE (o `extra_xor`) e o número de pares de C ROM. As chaves vêm de
     * `src/devices/bus/neogeo/prot_cmc.h` do MAME; a identificação é pelo
     * NÚMERO DO CONJUNTO no nome do arquivo ("252-c1.c1" -> 252), porque o NGH
     * do cabeçalho não serve: ganryu diz 0002 e bangbead/sengoku3 dizem os
     * dois 0020. Esses aqui têm a P ROM em claro (sem SMA), só o gráfico é
     * cifrado. */
    int cmc_key = -1;
    switch (rs->sma_type) { /* Achado #79: chaves de prot_cmc.h */
    case NEOGEO_SMA_KOF99:  cmc_key = 0x00; break; /* KOF99_GFX_KEY  */
    case NEOGEO_SMA_GAROU:  cmc_key = 0x06; break; /* GAROU_GFX_KEY  */
    case NEOGEO_SMA_MSLUG3: cmc_key = 0xAD; break; /* MSLUG3_GFX_KEY */
    case NEOGEO_SMA_KOF2000: cmc_key = 0x00; break; /* KOF2000_GFX_KEY (CMC50) */
    default: break;
    }
    const bool use_cmc50 = (rs->sma_type == NEOGEO_SMA_KOF2000) || (c50 != NULL);
    if (c50) cmc_key = c50->key; /* Achado #83 */
    if (cmc_key < 0 && rs->sma_type == NEOGEO_SMA_NONE) {
        static const struct { const char *set; int key; } cmc42_sets[] = {
            { "070", 0xBD }, /* zupapa   ZUPAPA_GFX_KEY   */
            { "252", 0x07 }, /* ganryu   GANRYU_GFX_KEY   */
            { "254", 0x05 }, /* s1945p   S1945P_GFX_KEY   */
            { "255", 0x9F }, /* preisle2 PREISLE2_GFX_KEY */
            { "259", 0xF8 }, /* bangbead BANGBEAD_GFX_KEY */
            { "260", 0xFF }, /* nitd     NITD_GFX_KEY     */
            { "261", 0xFE }, /* sengoku3 SENGOKU3_GFX_KEY */
        };
        for (size_t k = 0; k < sizeof(cmc42_sets) / sizeof(cmc42_sets[0]) && cmc_key < 0; k++) {
            for (int i = 0; i < cart_listing.count; i++) {
                if (filename_has_token(cart_listing.names[i], cmc42_sets[k].set)) {
                    cmc_key = cmc42_sets[k].key;
                    break;
                }
            }
        }
    }
    u32 c_pairs = (c_odd_parts > 0 && c_odd_parts == c_even_parts) ? (u32)c_odd_parts : 0u;
    if (cmc_key >= 0 && c_pairs > 0 && rs->c_odd_size > 0 && rs->c_odd_size == rs->c_even_size &&
        (rs->c_odd_size % c_pairs) == 0u) {
        u32 chunk = rs->c_odd_size / c_pairs; /* tamanho de 1 C ROM original */
        u32 combined_size = rs->c_odd_size + rs->c_even_size;
        u8 *combined = (u8 *)malloc(combined_size);
        if (combined) {
            for (u32 pair = 0; pair < c_pairs; pair++) {
                u8 *dst = combined + pair * chunk * 2u;
                const u8 *odd = rs->c_odd + pair * chunk;
                const u8 *even = rs->c_even + pair * chunk;
                for (u32 k = 0; k < chunk; k++) {
                    dst[2u * k + 0u] = odd[k];
                    dst[2u * k + 1u] = even[k];
                }
            }

            if (use_cmc50) neogeo_cmc50_gfx_decrypt(combined, combined_size, cmc_key);
            else                                    neogeo_cmc42_gfx_decrypt(combined, combined_size, cmc_key);

            for (u32 pair = 0; pair < c_pairs; pair++) {
                const u8 *src = combined + pair * chunk * 2u;
                u8 *odd = rs->c_odd + pair * chunk;
                u8 *even = rs->c_even + pair * chunk;
                for (u32 k = 0; k < chunk; k++) {
                    odd[k] = src[2u * k + 0u];
                    even[k] = src[2u * k + 1u];
                }
            }

            free(rs->s); /* cartucho com CMC não tem s1.s1 — a fix layer sai daqui */
            /* Achado #79: garou/mslug3/kof2000 declaram `ROM_REGION(0x80000,
             * "fixed")` — "larger char set", 512KB em banco — e o
             * `sfix_decrypt` usa esse tamanho pra achar onde começa. */
            rs->s_size = c50 ? c50->fixed : ((rs->fix_bank_type != 0) ? 0x80000u : 0x20000u);
            rs->s = (u8 *)calloc(1, rs->s_size);
            if (rs->s) neogeo_cmc_sfix_decrypt(combined, combined_size, rs->s, rs->s_size);
            else rs->s_size = 0;

            free(combined);
        }
    }

    source_close(&bios_src);
    if (!same_source) source_close(&cart_src);
    return true;
}

bool neogeo_romset_load_dir(neogeo_romset_t *rs, const char *dir_path,
                             char *out_err, u32 out_err_size) {
    return neogeo_romset_load_dir_ex(rs, dir_path, NULL, out_err, out_err_size);
}
