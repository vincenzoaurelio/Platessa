// platessa.c — Markdown viewer in C with SDL2.
//
// Build:  make
// Run:    ./platessa [file.md]

#define _POSIX_C_SOURCE 200809L

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include "md4c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define WINDOW_W           900
#define WINDOW_H           700
#define ZOOM_STEP          1.15f
#define ZOOM_MIN           0.4f
#define ZOOM_MAX           5.0f
#define BASE_FONT_SIZE     15.0f
#define LINE_HEIGHT_MUL    1.6f
#define CONTENT_MAX_WIDTH  860.0f
#define PAGE_PADDING       24.0f
#define BLOCK_MARGIN       12.0f
#define STATUS_BAR_H       24
#define FONT_MIN_PX        6
#define FONT_MAX_PX        400
#define BASE_WIN_W         900
#define BASE_WIN_H         700
#define MATH_DPI           200

static const SDL_Color COL_BG             = {173, 173, 201, 255};
static const SDL_Color COL_PAGE           = {255, 255, 255, 255};
static const SDL_Color COL_TEXT           = { 36,  41,  46, 255};
static const SDL_Color COL_HEADING_BORDER = {234, 236, 239, 255};
static const SDL_Color COL_CODE_BG        = {246, 248, 250, 255};
static const SDL_Color COL_LINK           = {  3, 102, 214, 255};
static const SDL_Color COL_HR             = {225, 228, 232, 255};
static const SDL_Color COL_STATUS_BG      = {245, 245, 250, 255};
static const SDL_Color COL_TABLE_BORDER   = {200, 200, 200, 255};
static const SDL_Color COL_TABLE_HEADER   = {246, 248, 250, 255};

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------
typedef enum {
    BLK_PARA,
    BLK_HEADING,
    BLK_CODE,
    BLK_IMAGE,
    BLK_HR,
    BLK_LIST,
    BLK_MATH,
    BLK_TABLE
} BlockKind;

typedef enum { ST_NORMAL, ST_BOLD, ST_ITALIC, ST_BOLD_ITALIC, ST_CODE, ST_INLINE_MATH } TextStyle;

typedef struct {
    char     *text;
    TextStyle style;
    char     *link;
} Run;

typedef struct {
    int          run_idx;
    int          start, len;
    float        x, y;
    SDL_Texture *tex;
    int          tex_w, tex_h;
    bool         is_math;  
    SDL_Texture *math_tex; 
    int          math_draw_w; 
    int          math_draw_h; 
} PlacedWord;

typedef struct {
    SDL_Texture *tex;
    int          w, h;
} CodeLine;

typedef struct {
    Run         *runs;
    int          run_count, run_cap;
    bool         is_header;
    float        w, h; 
    PlacedWord  *words;
    int          word_count, word_cap;
} TableCell;

typedef struct {
    TableCell *cells;
    int        cell_count, cell_cap;
} TableRow;

typedef struct {
    BlockKind kind;

    int  level;
    int  heading_level;
    bool ordered;
    int  item_no;

    Run  *runs;
    int   run_count, run_cap;
    char *literal;

    char        *img_src;
    char        *img_path;
    SDL_Texture *image;
    bool         image_loaded;
    float        draw_w, draw_h;

    float y, height;
    float content_x, content_w;
    float text_w;

    PlacedWord *words;
    int         word_count, word_cap;

    CodeLine *code_lines;
    int       code_line_count;

    SDL_Texture *marker_tex;
    int          marker_w, marker_h;

    // Math (BLK_MATH)
    char        *math_source;   
    bool         math_display;  
    SDL_Texture *math_tex;      
    int          math_w, math_h;

    // Table (BLK_TABLE)
    TableRow *rows;
    int       row_count, row_cap;
    int       col_count;
    float    *col_widths;
} Block;

typedef struct {
    Block *blocks;
    int    count, cap;
    float  total_height;
    char  *base_dir;
    char  *file_path;
} Document;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static TTF_Font     *F_REG, *F_BOLD, *F_ITAL, *F_MONO;
static SDL_Renderer *g_rend;
static bool          g_quit = false;
static SDL_Texture  *g_splash_logo   = NULL;
static int           g_splash_logo_w = 0;
static int           g_splash_logo_h = 0;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool has_md_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    dot++;
    char ext[16] = {0};
    size_t n = strlen(dot);
    if (n >= sizeof(ext)) return false;
    for (size_t i = 0; i < n; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
    return !strcmp(ext, "md") || !strcmp(ext, "markdown");
}

static char *str_ndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *r = (char*)malloc(len + 1);
    memcpy(r, s, len);
    r[len] = 0;
    return r;
}

static int clamp_px(int px) {
    if (px < FONT_MIN_PX) return FONT_MIN_PX;
    if (px > FONT_MAX_PX) return FONT_MAX_PX;
    return px;
}

static TTF_Font *font_for_style(TextStyle s) {
    switch (s) {
        case ST_CODE:        return F_MONO;
        case ST_BOLD:        return F_BOLD;
        case ST_BOLD_ITALIC: return F_BOLD;
        case ST_ITALIC:      return F_ITAL;
        default:             return F_REG;
    }
}

static float block_font_size(const Block *b, float zoom) {
    float base = BASE_FONT_SIZE * zoom;
    if (b->kind == BLK_HEADING) {
        switch (b->heading_level) {
            case 1: return base * 2.00f;
            case 2: return base * 1.50f;
            case 3: return base * 1.25f;
            case 4: return base * 1.00f;
            case 5: return base * 0.875f;
            case 6: return base * 0.850f;
        }
    }
    if (b->kind == BLK_CODE) return base * 0.9f;
    return base;
}

static int block_font_px(const Block *b, float zoom) {
    return clamp_px((int)block_font_size(b, zoom));
}

static void resize_for_zoom(SDL_Window *win, float zoom) {
    Uint32 flags = SDL_GetWindowFlags(win);
    if (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN |
                 SDL_WINDOW_FULLSCREEN_DESKTOP))
        return;

    int max_w = 1920, max_h = 1080;
    int disp = SDL_GetWindowDisplayIndex(win);
    SDL_DisplayMode dm;
    if (disp >= 0 && SDL_GetCurrentDisplayMode(disp, &dm) == 0) {
        max_w = (int)(dm.w * 0.98f);
        max_h = (int)(dm.h * 0.92f);
    }

    int target_w = (int)(BASE_WIN_W * zoom);
    int target_h = (int)(BASE_WIN_H * zoom);
    if (target_w < 480) target_w = 480;
    if (target_h < 360) target_h = 360;
    if (target_w > max_w) target_w = max_w;
    if (target_h > max_h) target_h = max_h;

    SDL_SetWindowSize(win, target_w, target_h);
}

// ---------------------------------------------------------------------------
// LaTeX → PNG (with disk cache)
// ---------------------------------------------------------------------------
static void check_latex_deps(void) {
    if (system("latex --version > /dev/null 2>&1") != 0) {
        fprintf(stderr, "[math] WARNING: 'latex' not found in PATH. Install TeX Live.\n");
    } else if (system("dvipng --version > /dev/null 2>&1") != 0) {
        fprintf(stderr, "[math] WARNING: 'dvipng' not found in PATH. Install dvipng.\n");
    }
}

static const char *math_cache_dir(void) {
    static char *dir = NULL;
    if (dir) return dir;

    const char *home = getenv("HOME");
    const char *xdg  = getenv("XDG_CACHE_HOME");
    char buf[1024];
    if (xdg && *xdg)         snprintf(buf, sizeof(buf), "%s/platessa", xdg);
    else if (home && *home)  snprintf(buf, sizeof(buf), "%s/.cache/platessa", home);
    else                     snprintf(buf, sizeof(buf), "/tmp/platessa_cache");

    dir = strdup(buf);

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
    return dir;
}

static uint64_t fnv1a(const char *s, uint64_t seed) {
    uint64_t h = seed;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static char *math_cache_path(const char *latex, bool display) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a(display ? "D:" : "I:", h);
    h = fnv1a(latex, h);

    char path[1200];
    snprintf(path, sizeof(path), "%s/%016llx.png",
             math_cache_dir(), (unsigned long long)h);
    return strdup(path);
}

static SDL_Texture *render_latex_to_texture(const char *latex_source, bool display) {
    if (!latex_source || !*latex_source) return NULL;

    char *cache_path = math_cache_path(latex_source, display);

    if (file_exists(cache_path)) {
        SDL_Texture *tex = IMG_LoadTexture(g_rend, cache_path);
        if (tex) {
            free(cache_path);
            return tex;
        }
    }

    char base[256];
    snprintf(base, sizeof(base), "/tmp/platessa_math_%ld_%lu",
             (long)getpid(), (unsigned long)SDL_GetTicks());

    char tex_path[300], dvi_path[300], aux_path[300], log_path[300];
    snprintf(tex_path, sizeof(tex_path), "%s.tex", base);
    snprintf(dvi_path, sizeof(dvi_path), "%s.dvi", base);
    snprintf(aux_path, sizeof(aux_path), "%s.aux", base);
    snprintf(log_path, sizeof(log_path), "%s.log", base);

    FILE *f = fopen(tex_path, "w");
    if (!f) { free(cache_path); return NULL; }

    fprintf(f, "\\documentclass[12pt]{article}\n");
    fprintf(f, "\\usepackage{amsmath,amssymb}\n");
    fprintf(f, "\\pagestyle{empty}\n");
    fprintf(f, "\\begin{document}\n");
    if (display) fprintf(f, "\\[%s\\]\n", latex_source);
    else         fprintf(f, "\\text{$%s$}\n",  latex_source);
    fprintf(f, "\\end{document}\n");
    fclose(f);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "latex -interaction=batchmode -halt-on-error "
             "-output-directory=/tmp %s > /dev/null 2>&1",
             tex_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "[math] ERROR: 'latex' failed.\n");
        FILE *lf = fopen(log_path, "r");
        if (lf) {
            char line[256];
            fprintf(stderr, "[math] --- latex log ---\n");
            while (fgets(line, sizeof(line), lf)) fprintf(stderr, "%s", line);
            fprintf(stderr, "[math] ------------------\n");
            fclose(lf);
        }
        goto cleanup;
    }

    snprintf(cmd, sizeof(cmd),
             "dvipng -D %d -T tight -bg Transparent -o %s %s > /dev/null 2>&1",
             MATH_DPI, cache_path, dvi_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "[math] ERROR: 'dvipng' failed.\n");
        goto cleanup;
    }

    if (!file_exists(cache_path)) {
        fprintf(stderr, "[math] ERROR: dvipng output not found.\n");
        goto cleanup;
    }

    {
        SDL_Texture *tex = IMG_LoadTexture(g_rend, cache_path);
        remove(tex_path); remove(dvi_path); remove(aux_path); remove(log_path);
        free(cache_path);
        return tex;
    }

cleanup:
    remove(tex_path); remove(dvi_path); remove(aux_path); remove(log_path);
    free(cache_path);
    return NULL;
}

// ---------------------------------------------------------------------------
// Parser state + callbacks
// ---------------------------------------------------------------------------
typedef struct {
    Document *doc;
    Block    *cur;
    TableCell *cur_cell;
    
    bool      in_paragraph;
    bool      in_code;
    bool      in_image;

    TextStyle style_stack[16];
    int       style_top;

    int  indent;
    bool list_ordered;
    int  list_item_no;

    bool  in_math_span;
    bool  in_display_math;
    char *math_buf;
    int   math_buf_len;

    char *pending_link;
} ParseState;

static TextStyle current_style(ParseState *st) {
    bool b = false, i = false, c = false;
    for (int k = 0; k < st->style_top; k++) {
        if (st->style_stack[k] == ST_BOLD)   b = true;
        if (st->style_stack[k] == ST_ITALIC) i = true;
        if (st->style_stack[k] == ST_CODE)   c = true;
    }
    if (c) return ST_CODE;
    if (b && i) return ST_BOLD_ITALIC;
    if (b) return ST_BOLD;
    if (i) return ST_ITALIC;
    return ST_NORMAL;
}

static Block *doc_new_block(Document *d, BlockKind k) {
    if (d->count >= d->cap) {
        d->cap = d->cap ? d->cap * 2 : 16;
        d->blocks = (Block*)realloc(d->blocks, (size_t)d->cap * sizeof(Block));
    }
    Block *b = &d->blocks[d->count++];
    memset(b, 0, sizeof(*b));
    b->kind = k;
    return b;
}

static void block_add_run(Block *b, const char *text, size_t size,
                          TextStyle style, const char *link) {
    if (size == 0) return;

    if (b->run_count > 0) {
        Run *last = &b->runs[b->run_count - 1];
        bool same_link = (!link && !last->link) ||
                         (link && last->link && !strcmp(link, last->link));
        if (last->style == style && same_link) {
            size_t old = strlen(last->text);
            last->text = (char*)realloc(last->text, old + size + 1);
            memcpy(last->text + old, text, size);
            last->text[old + size] = 0;
            return;
        }
    }

    if (b->run_count >= b->run_cap) {
        b->run_cap = b->run_cap ? b->run_cap * 2 : 8;
        b->runs = (Run*)realloc(b->runs, (size_t)b->run_cap * sizeof(Run));
    }
    Run *r = &b->runs[b->run_count++];
    r->text = str_ndup(text, size);
    r->style = style;
    r->link  = link ? strdup(link) : NULL;
}

static void cell_add_run(TableCell *cell, const char *text, size_t size, TextStyle style, const char *link) {
    if (size == 0) return;
    if (cell->run_count > 0) {
        Run *last = &cell->runs[cell->run_count - 1];
        bool same_link = (!link && !last->link) || (link && last->link && !strcmp(link, last->link));
        if (last->style == style && same_link) {
            size_t old = strlen(last->text);
            last->text = (char*)realloc(last->text, old + size + 1);
            memcpy(last->text + old, text, size);
            last->text[old + size] = 0;
            return;
        }
    }
    if (cell->run_count >= cell->run_cap) {
        cell->run_cap = cell->run_cap ? cell->run_cap * 2 : 4;
        cell->runs = (Run*)realloc(cell->runs, (size_t)cell->run_cap * sizeof(Run));
    }
    Run *r = &cell->runs[cell->run_count++];
    r->text = str_ndup(text, size);
    r->style = style;
    r->link = link ? strdup(link) : NULL;
}

static int cb_enter_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    ParseState *st = (ParseState*)ud;
    switch (type) {
        case MD_BLOCK_TABLE:
            st->cur = doc_new_block(st->doc, BLK_TABLE);
            st->cur->level = st->indent;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            // Ignore these, keep st->cur as the table
            break;
        case MD_BLOCK_TR:
            if (st->cur && st->cur->kind == BLK_TABLE) {
                if (st->cur->row_count >= st->cur->row_cap) {
                    st->cur->row_cap = st->cur->row_cap ? st->cur->row_cap * 2 : 8;
                    st->cur->rows = realloc(st->cur->rows, st->cur->row_cap * sizeof(TableRow));
                }
                TableRow *row = &st->cur->rows[st->cur->row_count++];
                memset(row, 0, sizeof(TableRow));
            }
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            if (st->cur && st->cur->kind == BLK_TABLE && st->cur->row_count > 0) {
                TableRow *row = &st->cur->rows[st->cur->row_count - 1];
                if (row->cell_count >= row->cell_cap) {
                    row->cell_cap = row->cell_cap ? row->cell_cap * 2 : 4;
                    row->cells = realloc(row->cells, row->cell_cap * sizeof(TableCell));
                }
                TableCell *cell = &row->cells[row->cell_count++];
                memset(cell, 0, sizeof(TableCell));
                cell->is_header = (type == MD_BLOCK_TH);
                st->cur_cell = cell;
            }
            break;

        case MD_BLOCK_H:
            st->cur = doc_new_block(st->doc, BLK_HEADING);
            st->cur->level = st->indent;
            st->cur->heading_level = ((MD_BLOCK_H_DETAIL*)detail)->level;
            break;

        case MD_BLOCK_P:
            if (st->cur_cell) {
                break;
            }
            if (st->cur && st->cur->kind == BLK_LIST && st->cur->run_count == 0) {
                st->in_paragraph = true;
            } else {
                st->cur = doc_new_block(st->doc, BLK_PARA);
                st->cur->level = st->indent;
                st->in_paragraph = true;
            }
            break;

        case MD_BLOCK_CODE:
            st->cur = doc_new_block(st->doc, BLK_CODE);
            st->cur->level = st->indent;
            st->in_code = true;
            break;

        case MD_BLOCK_QUOTE:
            st->indent++;
            break;

        case MD_BLOCK_UL:
            st->indent++;
            st->list_ordered = false;
            st->list_item_no = 0;
            break;

        case MD_BLOCK_OL:
            st->indent++;
            st->list_ordered = true;
            st->list_item_no = ((MD_BLOCK_OL_DETAIL*)detail)->start - 1;
            break;

        case MD_BLOCK_LI:
            st->list_item_no++;
            st->cur = doc_new_block(st->doc, BLK_LIST);
            st->cur->level   = st->indent;
            st->cur->ordered = st->list_ordered;
            st->cur->item_no = st->list_item_no;
            break;

        case MD_BLOCK_HR:
            st->cur = doc_new_block(st->doc, BLK_HR);
            break;

        default: break;
    }
    return 0;
}

static int cb_leave_block(MD_BLOCKTYPE type, void *detail, void *ud) {
    ParseState *st = (ParseState*)ud;
    (void)detail;
    switch (type) {
        case MD_BLOCK_TABLE:
            st->cur = NULL;
            st->cur_cell = NULL;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            // Ignore, do NOT null out st->cur
            break;
        case MD_BLOCK_TR:
            st->cur_cell = NULL;
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            st->cur_cell = NULL;
            break;

        case MD_BLOCK_QUOTE:
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (st->indent > 0) st->indent--;
            break;

        case MD_BLOCK_LI:
            if (st->cur &&
                st->cur->kind == BLK_LIST &&
                st->cur->run_count == 0 &&
                st->doc->count > 0 &&
                st->cur == &st->doc->blocks[st->doc->count - 1]) {
                st->doc->count--;
            }
            st->cur = NULL;
            break;

        case MD_BLOCK_P:
            if (st->cur_cell) {
                break;
            }
            st->in_paragraph = false;
            st->cur = NULL;
            break;

        case MD_BLOCK_CODE:
            st->in_code = false;
            st->cur = NULL;
            break;

        default:
            st->cur = NULL;
            break;
    }
    return 0;
}

static int cb_enter_span(MD_SPANTYPE type, void *detail, void *ud) {
    ParseState *st = (ParseState*)ud;
    switch (type) {
        case MD_SPAN_LATEXMATH:
            st->in_math_span = true;
            st->in_display_math = false;
            free(st->math_buf);
            st->math_buf = NULL;
            st->math_buf_len = 0;
            break;

        case MD_SPAN_LATEXMATH_DISPLAY:
            st->in_math_span = true;
            st->in_display_math = true;
            free(st->math_buf);
            st->math_buf = NULL;
            st->math_buf_len = 0;
            break;

        case MD_SPAN_EM:
            if (st->style_top < 16) st->style_stack[st->style_top++] = ST_ITALIC;
            break;
        case MD_SPAN_STRONG:
            if (st->style_top < 16) st->style_stack[st->style_top++] = ST_BOLD;
            break;
        case MD_SPAN_CODE:
            if (st->style_top < 16) st->style_stack[st->style_top++] = ST_CODE;
            break;
        case MD_SPAN_A: {
            MD_SPAN_A_DETAIL *d = (MD_SPAN_A_DETAIL*)detail;
            st->pending_link = str_ndup(d->href.text, d->href.size);
            break;
        }
        case MD_SPAN_IMG: {
            MD_SPAN_IMG_DETAIL *d = (MD_SPAN_IMG_DETAIL*)detail;
            if (st->cur && st->cur->kind == BLK_PARA && st->cur->run_count == 0
                && st->doc->count > 0
                && st->cur == &st->doc->blocks[st->doc->count - 1]) {
                st->doc->count--;
            }
            st->cur = doc_new_block(st->doc, BLK_IMAGE);
            st->cur->level   = st->indent;
            st->cur->img_src = str_ndup(d->src.text, d->src.size);
            st->in_image = true;
            break;
        }
        default: break;
    }
    return 0;
}

static int cb_leave_span(MD_SPANTYPE type, void *detail, void *ud) {
    ParseState *st = (ParseState*)ud;
    (void)detail;
    switch (type) {
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: {
            bool display = st->in_display_math;
            st->in_math_span = false;

            if (st->math_buf && st->math_buf_len > 0) {
                while (st->math_buf_len > 0 && isspace((unsigned char)st->math_buf[0])) {
                    memmove(st->math_buf, st->math_buf + 1, st->math_buf_len);
                    st->math_buf_len--;
                }
                while (st->math_buf_len > 0 && isspace((unsigned char)st->math_buf[st->math_buf_len - 1])) {
                    st->math_buf[--st->math_buf_len] = '\0';
                }

                if (st->math_buf_len > 0) {
                    if (display) {
                        if (st->cur && st->cur->kind == BLK_PARA
                            && st->cur->run_count == 0
                            && st->doc->count > 0
                            && st->cur == &st->doc->blocks[st->doc->count - 1]) {
                            st->doc->count--;
                        }

                        Block *b = doc_new_block(st->doc, BLK_MATH);
                        b->level        = st->indent;
                        b->math_display = true;
                        b->math_source  = strdup(st->math_buf);
                        b->math_tex     = render_latex_to_texture(st->math_buf, true);
                        if (b->math_tex) SDL_QueryTexture(b->math_tex, NULL, NULL, &b->math_w, &b->math_h);

                        block_add_run(b, st->math_buf, (size_t)st->math_buf_len, ST_CODE, NULL);
                        st->cur = NULL;
                    } else {
                        if (st->cur_cell) {
                            cell_add_run(st->cur_cell, st->math_buf, (size_t)st->math_buf_len, ST_INLINE_MATH, NULL);
                        } else {
                            if (!st->cur) {
                                st->cur = doc_new_block(st->doc, BLK_PARA);
                                st->cur->level = st->indent;
                            }
                            block_add_run(st->cur, st->math_buf, (size_t)st->math_buf_len, ST_INLINE_MATH, NULL);
                        }
                    } 
                }
            }

            free(st->math_buf);
            st->math_buf = NULL;
            st->math_buf_len = 0;
            break;
        }
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_CODE:
            if (st->style_top > 0) st->style_top--;
            break;
        case MD_SPAN_A:
            free(st->pending_link);
            st->pending_link = NULL;
            break;
        case MD_SPAN_IMG:
            st->in_image = false;
            st->cur = NULL;
            break;
        default: break;
    }
    return 0;
}

static int cb_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud) {
    ParseState *st = (ParseState*)ud;
    if (size == 0) return 0;
    if (st->in_image) return 0;
    if (type == MD_TEXT_NULLCHAR) return 0;
    if (type == MD_TEXT_HTML) return 0;

    if (type == MD_TEXT_LATEXMATH) {
        char *nb = realloc(st->math_buf, (size_t)st->math_buf_len + size + 1);
        if (nb) {
            st->math_buf = nb;
            memcpy(st->math_buf + st->math_buf_len, text, size);
            st->math_buf_len += (int)size;
            st->math_buf[st->math_buf_len] = '\0';
        }
        return 0;
    }

    if (st->cur_cell) {
        cell_add_run(st->cur_cell, text, size, current_style(st), st->pending_link);
        return 0;
    }

    if (!st->cur && st->in_paragraph) {
        st->cur = doc_new_block(st->doc, BLK_PARA);
        st->cur->level = st->indent;
    }
    if (!st->cur) return 0;

    if (st->in_code) {
        Block *b = st->cur;
        size_t old = b->literal ? strlen(b->literal) : 0;
        b->literal = (char*)realloc(b->literal, old + size + 1);
        memcpy(b->literal + old, text, size);
        b->literal[old + size] = 0;
        return 0;
    }

    if (type == MD_TEXT_CODE) {
        block_add_run(st->cur, text, size, ST_CODE, st->pending_link);
        return 0;
    }
    if (type == MD_TEXT_BR) {
        block_add_run(st->cur, "\n", 1, current_style(st), NULL);
        return 0;
    }
    block_add_run(st->cur, text, size, current_style(st), st->pending_link);
    return 0;
}

// ---------------------------------------------------------------------------
// Image / math / document loading
// ---------------------------------------------------------------------------
static char *get_base_dir(const char *path) {
    const char *last = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs && (!last || bs > last)) last = bs;
#endif
    if (!last) return strdup(".");
    return str_ndup(path, (size_t)(last - path));
}

static void block_load_image(Block *b, const char *base_dir) {
    if (!b->img_src) return;
    if (!strncmp(b->img_src, "http://", 7) || !strncmp(b->img_src, "https://", 8)) return;

    char path[2048];
    if (b->img_src[0] == '/' || (strlen(b->img_src) > 2 && b->img_src[1] == ':')) {
        snprintf(path, sizeof(path), "%s", b->img_src);
    } else {
        snprintf(path, sizeof(path), "%s/%s", base_dir, b->img_src);
    }
    b->img_path = strdup(path);
    if (!file_exists(path)) return;
    b->image = IMG_LoadTexture(g_rend, path);
    if (b->image) {
        SDL_SetTextureBlendMode(b->image, SDL_BLENDMODE_BLEND);
        b->image_loaded = true;
    }
}

static void block_free(Block *b) {
    if (b->math_tex) SDL_DestroyTexture(b->math_tex);
    free(b->math_source);

    if (b->rows) {
        for (int r = 0; r < b->row_count; r++) {
            for (int c = 0; c < b->rows[r].cell_count; c++) {
                TableCell *cell = &b->rows[r].cells[c];
                for (int ri = 0; ri < cell->run_count; ri++) {
                    free(cell->runs[ri].text);
                    free(cell->runs[ri].link);
                }
                free(cell->runs);
                for (int w = 0; w < cell->word_count; w++) {
                    if (cell->words[w].tex) SDL_DestroyTexture(cell->words[w].tex);
                }
                free(cell->words);
            }
            free(b->rows[r].cells);
        }
        free(b->rows);
    }
    free(b->col_widths);

    for (int j = 0; j < b->run_count; j++) {
        free(b->runs[j].text);
        free(b->runs[j].link);
    }
    free(b->runs);

    for (int j = 0; j < b->word_count; j++)
        if (b->words[j].tex) SDL_DestroyTexture(b->words[j].tex);
    free(b->words);

    if (b->code_lines) {
        for (int j = 0; j < b->code_line_count; j++)
            if (b->code_lines[j].tex) SDL_DestroyTexture(b->code_lines[j].tex);
        free(b->code_lines);
    }

    if (b->marker_tex) SDL_DestroyTexture(b->marker_tex);
    if (b->image_loaded) SDL_DestroyTexture(b->image);

    free(b->literal);
    free(b->img_src);
    free(b->img_path);
    memset(b, 0, sizeof(*b));
}

static void document_free(Document *d) {
    for (int i = 0; i < d->count; i++) block_free(&d->blocks[i]);
    free(d->blocks);
    free(d->base_dir);
    free(d->file_path);
    memset(d, 0, sizeof(*d));
}

static bool document_load(Document *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    char *text = (char*)malloc((size_t)size + 1);
    size_t got = fread(text, 1, (size_t)size, f);
    text[got] = 0;
    fclose(f);

    document_free(d);

    d->file_path = strdup(path);
    d->base_dir  = get_base_dir(path);

    ParseState st;
    memset(&st, 0, sizeof(st));
    st.doc = d;

    MD_PARSER parser;
    memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0;
    parser.flags       = MD_FLAG_LATEXMATHSPANS | MD_FLAG_FOOTNOTES | MD_FLAG_TABLES;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    md_parse(text, (MD_SIZE)got, &parser, &st);
    free(text);
    free(st.math_buf);
    free(st.pending_link);

    for (int i = 0; i < d->count; i++) {
        if (d->blocks[i].kind == BLK_IMAGE)
            block_load_image(&d->blocks[i], d->base_dir);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static void layout_text_block(Block *b, float max_width, int font_px) {
    b->word_count = 0;

    float line_h = font_px * LINE_HEIGHT_MUL;
    float x = 0, y = 0;
    float max_x = 0;

    TTF_SetFontSize(F_REG, font_px);
    int space_w = 0, dummy = 0;
    TTF_SizeUTF8(F_REG, " ", &space_w, &dummy);

    for (int ri = 0; ri < b->run_count; ri++) {
        Run *r = &b->runs[ri];

        if (r->style == ST_INLINE_MATH) {
            SDL_Texture *m_tex = render_latex_to_texture(r->text, false);
            if (m_tex) {
                int mw = 0, mh = 0;
                SDL_QueryTexture(m_tex, NULL, NULL, &mw, &mh);
                
                float target_h = font_px * 1.2f;
                float scale = 1.0f;
                if (mh > 0) scale = target_h / (float)mh;
                
                int dw = (int)(mw * scale);
                int dh = (int)(mh * scale);

                if (x > 0 && x + dw > max_width) { x = 0; y += line_h; }

                if (b->word_count >= b->word_cap) {
                    b->word_cap = b->word_cap ? b->word_cap * 2 : 32;
                    b->words = (PlacedWord*)realloc(b->words, (size_t)b->word_cap * sizeof(PlacedWord));
                }
                PlacedWord *pw = &b->words[b->word_count++];
                pw->run_idx    = ri;
                pw->start      = 0;
                pw->len        = (int)strlen(r->text);
                pw->x          = x;
                pw->y          = y + (line_h - dh) * 0.5f; 
                pw->tex        = m_tex;
                pw->tex_w      = dw;
                pw->tex_h      = dh;
                pw->is_math    = true;
                pw->math_tex   = m_tex;
                pw->math_draw_w = dw;
                pw->math_draw_h = dh;

                x += dw;
                if (x > max_x) max_x = x;
            } else {
                int w = 0, h = 0;
                TTF_SizeUTF8(F_REG, r->text, &w, &h);
                if (x > 0 && x + w > max_width) { x = 0; y += line_h; }
                if (b->word_count >= b->word_cap) {
                    b->word_cap = b->word_cap ? b->word_cap * 2 : 32;
                    b->words = (PlacedWord*)realloc(b->words, (size_t)b->word_cap * sizeof(PlacedWord));
                }
                PlacedWord *pw = &b->words[b->word_count++];
                pw->run_idx = ri;
                pw->start = 0;
                pw->len = (int)strlen(r->text);
                pw->x = x;
                pw->y = y;
                pw->tex = NULL;
                pw->is_math = false;
                x += w;
                if (x > max_x) max_x = x;
            }
            continue;
        }

        TTF_Font *font = font_for_style(r->style);
        TTF_SetFontSize(font, font_px);

        int n = (int)strlen(r->text);
        int i = 0;
        while (i < n) {
            int start = i;
            bool is_space = isspace((unsigned char)r->text[i]) != 0;
            while (i < n && (!!isspace((unsigned char)r->text[i])) == is_space) i++;
            int len = i - start;

            if (is_space) {
                int nl = 0;
                for (int j = 0; j < len; j++)
                    if (r->text[start + j] == '\n') nl++;
                if (nl > 0) { x = 0; y += line_h * nl; }
                else if (x > 0) { x += (float)space_w; }
                continue;
            }

            char buf[1024];
            int cp = len < 1023 ? len : 1023;
            memcpy(buf, r->text + start, cp);
            buf[cp] = 0;

            int w = 0, h = 0;
            TTF_SizeUTF8(font, buf, &w, &h);

            if (x > 0 && x + w > max_width) { x = 0; y += line_h; }

            if (b->word_count >= b->word_cap) {
                b->word_cap = b->word_cap ? b->word_cap * 2 : 32;
                b->words = (PlacedWord*)realloc(b->words, (size_t)b->word_cap * sizeof(PlacedWord));
            }
            PlacedWord *pw = &b->words[b->word_count++];
            pw->run_idx = ri;
            pw->start   = start;
            pw->len     = len;
            pw->x       = x;
            pw->y       = y;
            pw->tex     = NULL;
            pw->tex_w   = pw->tex_h = 0;
            pw->is_math = false;
            x += (float)w;
            if (x > max_x) max_x = x;
        }
    }
    b->text_w = max_x;
    b->height = y + line_h;
}

static void layout_table_block(Block *b, float max_width, int font_px) {
    b->col_count = 0;
    for (int r = 0; r < b->row_count; r++) {
        if (b->rows[r].cell_count > b->col_count)
            b->col_count = b->rows[r].cell_count;
    }
    if (b->col_count == 0) return;

    b->col_widths = calloc(b->col_count, sizeof(float));
    float cell_pad = 8.0f * (font_px / BASE_FONT_SIZE);
    float line_h = font_px * LINE_HEIGHT_MUL;

    for (int c = 0; c < b->col_count; c++) {
        float max_w = 100.0f; // Minimum column width
        for (int r = 0; r < b->row_count; r++) {
            if (c >= b->rows[r].cell_count) continue;
            TableCell *cell = &b->rows[r].cells[c];
            float w = 0;
            for (int ri = 0; ri < cell->run_count; ri++) {
                Run *run = &cell->runs[ri];
                if (run->style == ST_INLINE_MATH) {
                    w += 60.0f;
                } else {
                    TTF_Font *font = font_for_style(run->style);
                    TTF_SetFontSize(font, font_px);
                    int tw = 0;
                    TTF_SizeUTF8(font, run->text, &tw, NULL);
                    w += tw;
                }
            }
            if (w > max_w) max_w = w;
        }
        b->col_widths[c] = max_w + cell_pad * 2;
    }

    float total_w = 0;
    for (int c = 0; c < b->col_count; c++) total_w += b->col_widths[c];
    if (total_w > max_width && total_w > 0) {
        float scale = max_width / total_w;
        for (int c = 0; c < b->col_count; c++) b->col_widths[c] *= scale;
    }

    float y = 0;
    for (int r = 0; r < b->row_count; r++) {
        float row_h = line_h;
        float x = 0;
        for (int c = 0; c < b->rows[r].cell_count; c++) {
            TableCell *cell = &b->rows[r].cells[c];
            float cell_w = b->col_widths[c] - cell_pad * 2;

            cell->word_count = 0;
            float cx = 0, cy = 0;
            for (int ri = 0; ri < cell->run_count; ri++) {
                Run *run = &cell->runs[ri];

                if (run->style == ST_INLINE_MATH) {
                    SDL_Texture *m_tex = render_latex_to_texture(run->text, false);
                    if (m_tex) {
                        int mw = 0, mh = 0;
                        SDL_QueryTexture(m_tex, NULL, NULL, &mw, &mh);
                        float target_h = font_px * 1.2f;
                        float scale = 1.0f;
                        if (mh > 0) scale = target_h / (float)mh;
                        
                        if (cell_w > 0 && mw * scale > cell_w) {
                            scale = cell_w / (float)mw;
                        }
                        
                        int dw = (int)(mw * scale);
                        int dh = (int)(mh * scale);

                        if (cx > 0 && cx + dw > cell_w) { cx = 0; cy += line_h; }

                        if (cell->word_count >= cell->word_cap) {
                            cell->word_cap = cell->word_cap ? cell->word_cap * 2 : 16;
                            cell->words = realloc(cell->words, cell->word_cap * sizeof(PlacedWord));
                        }
                        PlacedWord *pw = &cell->words[cell->word_count++];
                        pw->is_math = true;
                        pw->math_tex = m_tex;
                        pw->tex = m_tex;
                        pw->x = cx;
                        pw->y = cy + (line_h - dh) * 0.5f;
                        pw->tex_w = dw;
                        pw->tex_h = dh;
                        cx += dw;
                    }
                    continue;
                }

                TTF_Font *font = font_for_style(run->style);
                TTF_SetFontSize(font, font_px);
                int n = (int)strlen(run->text);
                int i = 0;
                while (i < n) {
                    int start = i;
                    bool is_space = isspace((unsigned char)run->text[i]) != 0;
                    while (i < n && (!!isspace((unsigned char)run->text[i])) == is_space) i++;
                    int len = i - start;

                    if (is_space) {
                        int nl = 0;
                        for (int j = 0; j < len; j++)
                            if (run->text[start + j] == '\n') nl++;
                        if (nl > 0) { cx = 0; cy += line_h * nl; }
                        else if (cx > 0) {
                            int sw = 0;
                            TTF_SizeUTF8(F_REG, " ", &sw, NULL);
                            cx += sw;
                        }
                        continue;
                    }

                    char buf[1024];
                    int cp = len < 1023 ? len : 1023;
                    memcpy(buf, run->text + start, cp);
                    buf[cp] = 0;

                    int w = 0;
                    TTF_SizeUTF8(font, buf, &w, NULL);

                    if (cx > 0 && cx + w > cell_w) { cx = 0; cy += line_h; }

                    if (cell->word_count >= cell->word_cap) {
                        cell->word_cap = cell->word_cap ? cell->word_cap * 2 : 16;
                        cell->words = realloc(cell->words, cell->word_cap * sizeof(PlacedWord));
                    }
                    PlacedWord *pw = &cell->words[cell->word_count++];
                    pw->run_idx = ri;
                    pw->start = start;
                    pw->len = len;
                    pw->x = cx;
                    pw->y = cy;
                    pw->tex = NULL;
                    pw->is_math = false;
                    cx += w;
                }
            }
            float ch = cy + line_h;
            if (ch > row_h) row_h = ch;
            cell->w = b->col_widths[c];
            cell->h = row_h;
            x += cell->w;
        }
        for (int c = 0; c < b->rows[r].cell_count; c++) {
            b->rows[r].cells[c].h = row_h;
        }
        y += row_h;
    }
    b->height = y;
}

static void layout_code_block(Block *b, float zoom) {
    float fs     = BASE_FONT_SIZE * zoom * 0.9f;
    float line_h = fs * 1.4f;
    float pad    = 16.0f * zoom;

    int n_lines = 0;
    if (b->literal) {
        n_lines = 1;
        for (const char *p = b->literal; *p; p++)
            if (*p == '\n') n_lines++;
    }
    b->code_line_count = n_lines;
    b->height = n_lines * line_h + pad * 2;
}

static void layout_image_block(Block *b, float zoom) {
    if (!b->image_loaded) {
        b->height = 40.0f * zoom;
        b->draw_w = b->content_w;
        b->draw_h = 40.0f * zoom;
        return;
    }
    int iw = 0, ih = 0;
    SDL_QueryTexture(b->image, NULL, NULL, &iw, &ih);
    float scale = 1.0f;
    if ((float)iw > b->content_w && iw > 0)
        scale = b->content_w / (float)iw;
    b->draw_w = (float)iw * scale;
    b->draw_h = (float)ih * scale;
    b->height = b->draw_h;
}

static void layout_math_block(Block *b, float zoom) {
    if (b->math_tex) {
        float scale = 1.0f;
        if ((float)b->math_w > b->content_w && b->math_w > 0)
            scale = b->content_w / (float)b->math_w;
        b->draw_w = (float)b->math_w * scale;
        b->draw_h = (float)b->math_h * scale;
        b->height = b->draw_h;
    } else {
        layout_text_block(b, b->content_w, block_font_px(b, zoom));
    }
}

static void document_layout(Document *d, float zoom) {
    int W = 0, H = 0;
    SDL_GetRendererOutputSize(g_rend, &W, &H);
    (void)H;

    float screen_w = (float)W;
    float page_w   = fminf(screen_w, CONTENT_MAX_WIDTH);
    float page_x   = (screen_w - page_w) / 2.0f;
    float content_width = page_w - PAGE_PADDING * 2;

    float y = PAGE_PADDING;

    for (int i = 0; i < d->count; i++) {
        Block *b = &d->blocks[i];

        for (int j = 0; j < b->word_count; j++) {
            if (b->words[j].tex && !b->words[j].is_math) {
                SDL_DestroyTexture(b->words[j].tex);
            }
            if (b->words[j].is_math && b->words[j].math_tex) {
                SDL_DestroyTexture(b->words[j].math_tex);
            }
            b->words[j].tex = NULL;
            b->words[j].math_tex = NULL;
        }

        if (b->rows) {
            for (int r = 0; r < b->row_count; r++) {
                for (int c = 0; c < b->rows[r].cell_count; c++) {
                    TableCell *cell = &b->rows[r].cells[c];
                    for (int w = 0; w < cell->word_count; w++) {
                        if (cell->words[w].tex) { SDL_DestroyTexture(cell->words[w].tex); cell->words[w].tex = NULL; }
                    }
                }
            }
        }
        free(b->col_widths);
        b->col_widths = NULL;

        if (b->code_lines) {
            for (int j = 0; j < b->code_line_count; j++)
                if (b->code_lines[j].tex) SDL_DestroyTexture(b->code_lines[j].tex);
            free(b->code_lines);
        }
        b->code_lines = NULL;
        b->code_line_count = 0;

        if (b->marker_tex) SDL_DestroyTexture(b->marker_tex);
        b->marker_tex = NULL;
        b->marker_w = b->marker_h = 0;

        b->content_x = page_x + PAGE_PADDING + b->level * 20.0f * zoom;
        b->content_w = content_width    - b->level * 20.0f * zoom;

        b->y = y;

        switch (b->kind) {
            case BLK_MATH:
                layout_math_block(b, zoom);
                break;
            case BLK_TABLE:
                layout_table_block(b, b->content_w, block_font_px(b, zoom));
                break;
            case BLK_PARA:
            case BLK_HEADING:
            case BLK_LIST:
                layout_text_block(b, b->content_w, block_font_px(b, zoom));
                break;
            case BLK_CODE:
                layout_code_block(b, zoom);
                break;
            case BLK_IMAGE:
                layout_image_block(b, zoom);
                break;
            case BLK_HR:
                b->height = 16.0f * zoom;
                break;
        }

        y += b->height + BLOCK_MARGIN * zoom;
    }

    d->total_height = y + PAGE_PADDING;
}

static void render_textures(Document *d, float zoom) {
    for (int bi = 0; bi < d->count; bi++) {
        Block *b = &d->blocks[bi];

        if (b->kind == BLK_TABLE) {
            int font_px = block_font_px(b, zoom);
            for (int r = 0; r < b->row_count; r++) {
                for (int c = 0; c < b->rows[r].cell_count; c++) {
                    TableCell *cell = &b->rows[r].cells[c];
                    for (int i = 0; i < cell->word_count; i++) {
                        PlacedWord *pw = &cell->words[i];
                        if (pw->is_math) continue;

                        Run *run = &cell->runs[pw->run_idx];
                        TTF_Font *font = font_for_style(run->style);
                        TTF_SetFontSize(font, font_px);
                        
                        char buf[1024];
                        int cp = pw->len < 1023 ? pw->len : 1023;
                        memcpy(buf, run->text + pw->start, cp);
                        buf[cp] = 0;
                        
                        SDL_Color col = run->link ? COL_LINK : COL_TEXT;
                        SDL_Surface *surf = TTF_RenderUTF8_Blended(font, buf, col);
                        if (surf) {
                            pw->tex = SDL_CreateTextureFromSurface(g_rend, surf);
                            pw->tex_w = surf->w;
                            pw->tex_h = surf->h;
                            SDL_FreeSurface(surf);
                        }
                    }
                }
            }
        }

        if (b->kind == BLK_PARA || b->kind == BLK_HEADING || b->kind == BLK_LIST ||
            (b->kind == BLK_MATH && !b->math_tex)) {
            int font_px = block_font_px(b, zoom);
            for (int i = 0; i < b->word_count; i++) {
                PlacedWord *pw = &b->words[i];
                if (pw->is_math) continue;

                Run *r = &b->runs[pw->run_idx];
                if (r->style == ST_INLINE_MATH) continue;

                TTF_Font *font = font_for_style(r->style);
                TTF_SetFontSize(font, font_px);

                char buf[1024];
                int cp = pw->len < 1023 ? pw->len : 1023;
                memcpy(buf, r->text + pw->start, cp);
                buf[cp] = 0;

                SDL_Color col = r->link ? COL_LINK : COL_TEXT;
                SDL_Surface *surf = TTF_RenderUTF8_Blended(font, buf, col);
                if (!surf) continue;
                pw->tex = SDL_CreateTextureFromSurface(g_rend, surf);
                pw->tex_w = surf->w;
                pw->tex_h = surf->h;
                SDL_FreeSurface(surf);
            }

            if (b->kind == BLK_LIST) {
                char mbuf[64];
                if (b->ordered) snprintf(mbuf, sizeof(mbuf), "%d.", b->item_no);
                else            snprintf(mbuf, sizeof(mbuf), "\xE2\x80\xA2");
                TTF_SetFontSize(F_REG, font_px);
                SDL_Surface *surf = TTF_RenderUTF8_Blended(F_REG, mbuf, COL_TEXT);
                if (surf) {
                    b->marker_tex = SDL_CreateTextureFromSurface(g_rend, surf);
                    b->marker_w = surf->w;
                    b->marker_h = surf->h;
                    SDL_FreeSurface(surf);
                }
            }
        }

        if (b->kind == BLK_CODE && b->literal) {
            float fs = BASE_FONT_SIZE * zoom * 0.9f;
            int px = clamp_px((int)fs);
            TTF_SetFontSize(F_MONO, px);

            b->code_lines = (CodeLine*)calloc((size_t)b->code_line_count, sizeof(CodeLine));
            int idx = 0;
            const char *p = b->literal;
            while (idx < b->code_line_count) {
                const char *nl = strchr(p, '\n');
                int len = nl ? (int)(nl - p) : (int)strlen(p);
                char *line = str_ndup(p, (size_t)len);
                SDL_Surface *surf = TTF_RenderUTF8_Blended(F_MONO, line, COL_TEXT);
                if (surf) {
                    b->code_lines[idx].tex = SDL_CreateTextureFromSurface(g_rend, surf);
                    b->code_lines[idx].w = surf->w;
                    b->code_lines[idx].h = surf->h;
                    SDL_FreeSurface(surf);
                }
                free(line);
                idx++;
                if (!nl) break;
                p = nl + 1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_words(Block *b, float sy, float zoom) {
    float base_fs = block_font_size(b, zoom);

    float offset_x = 0;
    if (b->kind == BLK_MATH && b->math_display && !b->math_tex) {
        offset_x = (b->content_w - b->text_w) * 0.5f;
    }
    if (offset_x < 0) offset_x = 0;

    if (b->kind == BLK_LIST && b->marker_tex) {
        int mx = (int)(b->content_x - 8.0f * zoom) - b->marker_w;
        SDL_Rect dst = { mx, (int)(sy + base_fs * 0.15f),
                         b->marker_w, b->marker_h };
        SDL_RenderCopy(g_rend, b->marker_tex, NULL, &dst);
    }

    for (int i = 0; i < b->word_count; i++) {
       PlacedWord *pw = &b->words[i];
       
       if (pw->is_math) {
           SDL_Rect dst = {
               (int)(b->content_x + offset_x + pw->x),
               (int)(sy + pw->y),
               pw->tex_w, pw->tex_h
           };
           SDL_RenderCopy(g_rend, pw->math_tex, NULL, &dst);
           continue;
       }

       if (!pw->tex) continue;
       SDL_Rect dst = {
           (int)(b->content_x + offset_x + pw->x),
           (int)(sy + pw->y),
           pw->tex_w, pw->tex_h
       };
       SDL_RenderCopy(g_rend, pw->tex, NULL, &dst);
    } 

    if (b->kind == BLK_HEADING) {
        SDL_SetRenderDrawColor(g_rend,
            COL_HEADING_BORDER.r, COL_HEADING_BORDER.g,
            COL_HEADING_BORDER.b, 255);
        SDL_Rect r = { (int)b->content_x,
                       (int)(sy + b->height - 2),
                       (int)b->content_w, 1 };
        SDL_RenderFillRect(g_rend, &r);
    }
}

static void draw_table(Block *b, float sy, float zoom) {
    float pad = 8.0f * (block_font_px(b, zoom) / BASE_FONT_SIZE);
    float y = sy;

    for (int r = 0; r < b->row_count; r++) {
        float x = b->content_x;
        float row_h = (b->rows[r].cell_count > 0) ? b->rows[r].cells[0].h : 0;
        
        for (int c = 0; c < b->rows[r].cell_count; c++) {
            TableCell *cell = &b->rows[r].cells[c];
            
            if (cell->is_header) {
                SDL_SetRenderDrawColor(g_rend, COL_TABLE_HEADER.r, COL_TABLE_HEADER.g, COL_TABLE_HEADER.b, 255);
                SDL_Rect bg = { (int)x, (int)y, (int)cell->w, (int)cell->h };
                SDL_RenderFillRect(g_rend, &bg);
            }

            SDL_SetRenderDrawColor(g_rend, COL_TABLE_BORDER.r, COL_TABLE_BORDER.g, COL_TABLE_BORDER.b, 255);
            SDL_Rect border = { (int)x, (int)y, (int)cell->w, (int)cell->h };
            SDL_RenderDrawRect(g_rend, &border);
            
            for (int i = 0; i < cell->word_count; i++) {
                PlacedWord *pw = &cell->words[i];
                if (!pw->tex) continue;
                SDL_Rect dst = {
                    (int)(x + pad + pw->x),
                    (int)(y + pad + pw->y),
                    pw->tex_w, pw->tex_h
                };
                SDL_RenderCopy(g_rend, pw->tex, NULL, &dst);
            }
            
            x += cell->w;
        }
        y += row_h;
    }
}

static void draw_block(Block *b, float scroll_y, float zoom) {
    float sy = b->y - scroll_y;

    switch (b->kind) {
        case BLK_TABLE:
            draw_table(b, sy, zoom);
            break;

        case BLK_MATH:
            if (b->math_tex) {
                float x = b->math_display
                    ? b->content_x + (b->content_w - b->draw_w) * 0.5f
                    : b->content_x;
                SDL_Rect dst = { (int)x, (int)sy,
                                 (int)b->draw_w, (int)b->draw_h };
                SDL_RenderCopy(g_rend, b->math_tex, NULL, &dst);
            } else {
                draw_words(b, sy, zoom);
            }
            break;

        case BLK_PARA:
        case BLK_HEADING:
        case BLK_LIST:
            draw_words(b, sy, zoom);
            break;

        case BLK_CODE: {
            SDL_SetRenderDrawColor(g_rend,
                COL_CODE_BG.r, COL_CODE_BG.g, COL_CODE_BG.b, 255);
            SDL_Rect bg = { (int)b->content_x, (int)sy,
                            (int)b->content_w, (int)b->height };
            SDL_RenderFillRect(g_rend, &bg);

            float fs     = BASE_FONT_SIZE * zoom * 0.9f;
            float line_h = fs * 1.4f;
            float pad    = 16.0f * zoom;
            for (int i = 0; i < b->code_line_count; i++) {
                if (!b->code_lines[i].tex) continue;
                SDL_Rect dst = {
                    (int)(b->content_x + pad),
                    (int)(sy + pad + i * line_h),
                    b->code_lines[i].w, b->code_lines[i].h
                };
                SDL_RenderCopy(g_rend, b->code_lines[i].tex, NULL, &dst);
            }
            break;
        }

        case BLK_IMAGE: {
            if (b->image_loaded) {
                SDL_Rect dst = { (int)b->content_x, (int)sy,
                                 (int)b->draw_w, (int)b->draw_h };
                SDL_RenderCopy(g_rend, b->image, NULL, &dst);
            } else {
                SDL_SetRenderDrawColor(g_rend, 200, 200, 200, 255);
                SDL_Rect outline = { (int)b->content_x, (int)sy,
                                     (int)b->content_w, (int)b->height };
                SDL_RenderDrawRect(g_rend, &outline);
                TTF_SetFontSize(F_REG, clamp_px((int)(12.0f * zoom)));
                SDL_Surface *surf = TTF_RenderUTF8_Blended(F_REG, "[image not found]",
                                                           (SDL_Color){128,128,128,255});
                if (surf) {
                    SDL_Texture *t = SDL_CreateTextureFromSurface(g_rend, surf);
                    if (t) {
                        SDL_Rect d = { (int)b->content_x + 8, (int)sy + 8,
                                       surf->w, surf->h };
                        SDL_RenderCopy(g_rend, t, NULL, &d);
                        SDL_DestroyTexture(t);
                    }
                    SDL_FreeSurface(surf);
                }
            }
            break;
        }

        case BLK_HR: {
            SDL_SetRenderDrawColor(g_rend, COL_HR.r, COL_HR.g, COL_HR.b, 255);
            SDL_Rect r = { (int)b->content_x,
                           (int)(sy + b->height * 0.5f),
                           (int)b->content_w, 2 };
            SDL_RenderFillRect(g_rend, &r);
            break;
        }
    }
}

static void draw_document(Document *d, float zoom, float scroll_y) {
    int W = 0, H = 0;
    SDL_GetRendererOutputSize(g_rend, &W, &H);
    (void)W;

    for (int i = 0; i < d->count; i++) {
        Block *b = &d->blocks[i];
        float sy = b->y - scroll_y;
        if (sy + b->height < -50.0f) continue;
        if (sy > (float)H + 50.0f)   continue;
        draw_block(b, scroll_y, zoom);
    }
}

static void draw_status_bar(const Document *d, int W, int H) {
    SDL_SetRenderDrawColor(g_rend,
        COL_STATUS_BG.r, COL_STATUS_BG.g, COL_STATUS_BG.b, 255);
    SDL_Rect bar = { 0, H - STATUS_BAR_H, W, STATUS_BAR_H };
    SDL_RenderFillRect(g_rend, &bar);

    TTF_SetFontSize(F_REG, 14);
    const char *text = d->file_path ? d->file_path : "No file open";
    SDL_Surface *surf = TTF_RenderUTF8_Blended(F_REG, text, COL_TEXT);
    if (surf) {
        SDL_Texture *t = SDL_CreateTextureFromSurface(g_rend, surf);
        if (t) {
            SDL_Rect dst = { 8, H - STATUS_BAR_H + 4, surf->w, surf->h };
            SDL_RenderCopy(g_rend, t, NULL, &dst);
            SDL_DestroyTexture(t);
        }
        SDL_FreeSurface(surf);
    }
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static TTF_Font *open_first(const char *const *paths, int n, const char *label) {
    for (int i = 0; i < n; i++) {
        if (file_exists(paths[i])) {
            TTF_Font *f = TTF_OpenFont(paths[i], (int)BASE_FONT_SIZE);
            if (f) {
                fprintf(stderr, "[font] %s <- %s\n", label, paths[i]);
                return f;
            }
        }
    }
    fprintf(stderr, "[font] FATAL: could not load %s font.\n", label);
    return NULL;
}

static bool load_fonts(void) {
    const char *regular[] = {
        "./fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    const char *bold[] = {
        "./fonts/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/segoeuib.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
    };
    const char *ital[] = {
        "./fonts/DejaVuSans-Oblique.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Oblique.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/segoeuii.ttf",
        "C:/Windows/Fonts/ariali.ttf",
    };
    const char *mono[] = {
        "./fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
    };
    F_REG  = open_first(regular, 7, "regular");
    F_BOLD = open_first(bold,    7, "bold");
    F_ITAL = open_first(ital,    7, "italic");
    F_MONO = open_first(mono,    7, "mono");
    return F_REG && F_BOLD && F_ITAL && F_MONO;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Window *win = SDL_CreateWindow(
        "Platessa",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(win, 480, 360);

    g_rend = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_rend) {
        g_rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!g_rend) {
            fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            return 1;
        }
    }

    if (!load_fonts()) {
        fprintf(stderr,
            "Place DejaVu fonts in ./fonts/ or install a system font.\n");
        return 1;
    }

    check_latex_deps();

    {
        const char *cwd_path = "logo.png";
        char *exe_path = NULL;
        char *base = SDL_GetBasePath();
        if (base) {
            size_t n = strlen(base) + 16;
            exe_path = (char*)malloc(n);
            snprintf(exe_path, n, "%slogo.png", base);
            SDL_free(base);
        }
        const char *chosen = NULL;
        if (file_exists(cwd_path))                        chosen = cwd_path;
        else if (exe_path && file_exists(exe_path))       chosen = exe_path;

        if (chosen) {
            g_splash_logo = IMG_LoadTexture(g_rend, chosen);
            if (g_splash_logo) {
                SDL_SetTextureBlendMode(g_splash_logo, SDL_BLENDMODE_BLEND);
                SDL_QueryTexture(g_splash_logo, NULL, NULL,
                                 &g_splash_logo_w, &g_splash_logo_h);
                fprintf(stderr, "[splash] logo <- %s\n", chosen);
            }
        } else {
            fprintf(stderr, "[splash] logo.png not found; showing hint only\n");
        }
        free(exe_path);
    }

    Document doc = {0};
    float    zoom       = 1.0f;
    float    scroll_y   = 0.0f;
    bool     drop_received = false;
    int      last_W     = 0, last_H = 0;
    bool     g_pending  = false;
    Uint32   g_pending_t = 0;
    bool     need_relayout = true;

    if (argc > 1) {
        if (!document_load(&doc, argv[1]))
            fprintf(stderr, "Cannot open %s\n", argv[1]);
    }

    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { g_quit = true; continue; }

            if (ev.type == SDL_DROPBEGIN) { drop_received = false; continue; }
            if (ev.type == SDL_DROPFILE) {
                if (!drop_received) {
                    drop_received = true;
                    char *path = ev.drop.file;
                    if (path && path[0] && has_md_ext(path)) {
                        if (document_load(&doc, path)) {
                            need_relayout = true;
                            scroll_y = 0;
                        }
                    }
                }
                SDL_free(ev.drop.file);
                continue;
            }
            if (ev.type == SDL_DROPCOMPLETE) { drop_received = false; continue; }

            if (ev.type == SDL_MOUSEWHEEL) {
                scroll_y -= ev.wheel.y * 60.0f;
                continue;
            }

            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode sym = ev.key.keysym.sym;
                Uint16 mod = ev.key.keysym.mod;
                bool ctrl  = (mod & KMOD_CTRL) != 0;
                bool shift = (mod & KMOD_SHIFT) != 0;

                if (sym != SDLK_g) g_pending = false;

                if (sym == SDLK_q && !ctrl) { g_quit = true; continue; }

                if (sym == SDLK_r && !ctrl && doc.file_path) {
                    char *tmp = strdup(doc.file_path);
                    document_load(&doc, tmp);
                    free(tmp);
                    need_relayout = true;
                    continue;
                }

                if ((sym == SDLK_EQUALS && ctrl) || sym == SDLK_KP_PLUS || 
                     sym == SDLK_PLUS) {
                    zoom = fminf(zoom * ZOOM_STEP, ZOOM_MAX);
                    resize_for_zoom(win, zoom);
                    need_relayout = true;
                    continue;
                }
                if ((sym == SDLK_MINUS && ctrl) || sym == SDLK_KP_MINUS || 
                     sym == SDLK_MINUS) {
                    zoom = fmaxf(zoom / ZOOM_STEP, ZOOM_MIN);
                    resize_for_zoom(win, zoom);
                    need_relayout = true;
                    continue;
                }
                if (sym == SDLK_0 && ctrl) {
                    zoom = 1.0f;
                    resize_for_zoom(win, zoom);
                    need_relayout = true;
                    continue;
                }

                if (sym == SDLK_j)         { scroll_y += 30; continue; }
                if (sym == SDLK_k)         { scroll_y -= 30; continue; }
                if (sym == SDLK_d && ctrl) { scroll_y += BASE_FONT_SIZE * zoom * 15; continue; }
                if (sym == SDLK_u && ctrl) { scroll_y -= BASE_FONT_SIZE * zoom * 15; continue; }
                if (sym == SDLK_f && ctrl) { scroll_y += (float)last_H * 0.9f; continue; }
                if (sym == SDLK_b && ctrl) { scroll_y -= (float)last_H * 0.9f; continue; }

                if (sym == SDLK_g) {
                    if (shift) {
                        scroll_y = doc.total_height;
                    } else {
                        Uint32 now = SDL_GetTicks();
                        if (g_pending && now - g_pending_t < 500) {
                            scroll_y = 0;
                            g_pending = false;
                        } else {
                            g_pending = true;
                            g_pending_t = now;
                        }
                    }
                    continue;
                }

                if (sym == SDLK_DOWN) { scroll_y += 30; continue; }
                if (sym == SDLK_UP)   { scroll_y -= 30; continue; }
                if (sym == SDLK_PAGEDOWN) { 
                    scroll_y += (float)last_H * 0.9f; 
                    continue; 
                }
                if (sym == SDLK_PAGEUP) { 
                    scroll_y -= (float)last_H * 0.9f; 
                    continue; 
                }
                if (sym == SDLK_HOME) { scroll_y = 0; continue; }
                if (sym == SDLK_END)  { scroll_y = doc.total_height; continue; }
            }
        }

        int W = 0, H = 0;
        SDL_GetRendererOutputSize(g_rend, &W, &H);
        if (W != last_W || H != last_H) {
            last_W = W;
            last_H = H;
            need_relayout = true;
        }

        if (need_relayout) {
            document_layout(&doc, zoom);
            render_textures(&doc, zoom);
            need_relayout = false;
        }

        float max_scroll = fmaxf(0.0f, doc.total_height - (float)H);
        if (scroll_y < 0) scroll_y = 0;
        if (scroll_y > max_scroll) scroll_y = max_scroll;

        SDL_SetRenderDrawColor(g_rend, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(g_rend);

        if (doc.count > 0) {
            float page_w = fminf((float)W, CONTENT_MAX_WIDTH);
            float page_x = ((float)W - page_w) * 0.5f;
            SDL_SetRenderDrawColor(g_rend, COL_PAGE.r, COL_PAGE.g, COL_PAGE.b, 
                                   255);
            SDL_Rect page_rect = { (int)page_x, 0, (int)page_w, H };
            SDL_RenderFillRect(g_rend, &page_rect);
        }

        if (doc.count == 0) {
            int cx = W / 2;
            int cy = H / 2;

            int logo_draw_w = 0;
            int logo_draw_h = 0;
            int logo_y      = cy - 40;

            if (g_splash_logo) {
                float max_w = (float)W * 0.60f;
                float max_h = (float)H * 0.60f;
                float sw    = (float)g_splash_logo_w;
                float sh    = (float)g_splash_logo_h;
                float scale = 1.0f;
                if (sw > 0 && sw * scale > max_w) scale = max_w / sw;
                if (sh > 0 && sh * scale > max_h) scale = max_h / sh;

                logo_draw_w = (int)(sw * scale);
                logo_draw_h = (int)(sh * scale);
                logo_y      = cy - logo_draw_h / 2 - 24;

                SDL_Rect dst = { cx - logo_draw_w / 2, logo_y,
                                 logo_draw_w, logo_draw_h };
                SDL_RenderCopy(g_rend, g_splash_logo, NULL, &dst);
            }

            TTF_SetFontSize(F_REG, 18);
            const char *hint = "Drag and drop a Markdown file here to open it";
            SDL_Color hint_col = { 60, 60, 90, 255 };
            SDL_Surface *surf = TTF_RenderUTF8_Blended(F_REG, hint, hint_col);
            if (surf) {
                SDL_Texture *t = SDL_CreateTextureFromSurface(g_rend, surf);
                if (t) {
                    int hx = (W - surf->w) / 2;
                    int hy = logo_y + logo_draw_h + 32;
                    SDL_Rect d = { hx, hy, surf->w, surf->h };
                    SDL_RenderCopy(g_rend, t, NULL, &d);
                    SDL_DestroyTexture(t);
                }
                SDL_FreeSurface(surf);
            }
        } else {
            draw_document(&doc, zoom, scroll_y);
        }

        draw_status_bar(&doc, W, H);
        SDL_RenderPresent(g_rend);
    }

    document_free(&doc);
    TTF_CloseFont(F_REG);
    TTF_CloseFont(F_BOLD);
    TTF_CloseFont(F_ITAL);
    TTF_CloseFont(F_MONO);
    if (g_splash_logo) SDL_DestroyTexture(g_splash_logo);
    SDL_DestroyRenderer(g_rend);
    SDL_DestroyWindow(win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
