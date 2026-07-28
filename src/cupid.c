#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#include <utf8proc.h>

#include "cupid.h"
#include "config.h"
#include "xwin.h"
#include "xentry.h"
#include "pty.h"
#include "arg.h"

#define VERSION "0.1"

/* PTY session singleton: always present (master_fd=-1 until spawned, so
   ttywrite is a no-op in unit-test builds where no PTY is opened). */
static PtySession g_pty_session = {
    .master_fd    = -1,
    .child_pid    = -1,
    .child_exited = 0,
    .child_status = 0,
};
static int printer_fd = STDOUT_FILENO;
static int printer_fd_owned = 0;

static void printer_write(const void *buf, size_t len) {
    const unsigned char *p = buf;

    while (printer_fd >= 0 && len > 0) {
        ssize_t n = write(printer_fd, p, len);
        if (n > 0) {
            p += n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            perror("cupidterminal: printer write");
            tprinter_close();
        }
    }
}

static void printer_write_range(const uint8_t *bytes, size_t start, size_t end,
                                size_t already_written) {
    if (end <= already_written)
        return;
    if (start < already_written)
        start = already_written;
    if (end > start)
        printer_write(bytes + start, end - start);
}

static void printer_mirror_input(const uint8_t *bytes, size_t len, int enabled,
                                 size_t already_written) {
    size_t pos = 0;

    while (pos < len) {
        size_t esc = pos;
        size_t final;
        size_t p;
        int param = -1;

        while (esc + 1 < len &&
               !(bytes[esc] == 0x1b && bytes[esc + 1] == '['))
            esc++;
        if (esc + 1 >= len) {
            if (enabled)
                printer_write_range(bytes, pos, len, already_written);
            return;
        }

        final = esc + 2;
        while (final < len &&
               !(bytes[final] >= 0x40 && bytes[final] <= 0x7e))
            final++;
        if (final >= len) {
            if (enabled)
                printer_write_range(bytes, pos, len, already_written);
            return;
        }
        if (bytes[final] != 'i') {
            final++;
            if (enabled)
                printer_write_range(bytes, pos, final, already_written);
            pos = final;
            continue;
        }

        p = esc + 2;
        if (p < final && bytes[p] >= '0' && bytes[p] <= '9') {
            param = 0;
            while (p < final && bytes[p] >= '0' && bytes[p] <= '9') {
                param = param * 10 + (bytes[p] - '0');
                p++;
            }
        }
        final++;
        if (enabled)
            printer_write_range(bytes, pos, final, already_written);
        if (p == final - 1 && param == 4)
            enabled = 0;
        else if (p == final - 1 && param == 5)
            enabled = 1;
        pos = final;
    }
}

int tprinter_open(const char *path) {
    if (!path)
        return 0;
    tprinter_close();
    if (strcmp(path, "-") == 0) {
        printer_fd = STDOUT_FILENO;
        printer_fd_owned = 0;
    } else {
        printer_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        printer_fd_owned = 1;
    }
    if (printer_fd < 0) {
        fprintf(stderr, "cupidterminal: cannot open printer output %s: %s\n",
                path, strerror(errno));
        printer_fd_owned = 0;
        return -1;
    }
    term.print_mode = 1;
    return 0;
}

void tprinter_close(void) {
    if (printer_fd_owned && printer_fd >= 0)
        close(printer_fd);
    printer_fd = -1;
    printer_fd_owned = 0;
}

void tprinter_toggle(void) {
    term.print_mode = !term.print_mode;
}

void tprinter_screen(void) {
    for (int row = 0; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            char cluster[MAX_CLUSTER_UTF8];
            size_t len = term_render_cluster(row, col, cluster, sizeof(cluster));
            if (len == 0)
                printer_write(" ", 1);
            else
                printer_write(cluster, len);
        }
        printer_write("\n", 1);
    }
}

void tprinter_selection(void) {
    char *selection = getsel();
    if (selection) {
        printer_write(selection, strlen(selection));
        free(selection);
    }
}

/*
 * Config globals needed by terminal body code and declared extern in config.h.
 * When building unit tests (CUPID_NO_MAIN), test_common.c provides these instead.
 */
#ifndef CUPID_NO_MAIN
char *vtiden = "\033[?6c";
int allowwindowops = 0;
unsigned int tabspaces = 8;

/* argv globals (consumed by ARGBEGIN/ARGEND in main()) */
char *argv0;
char *opt_font = NULL;
char *opt_class = NULL;
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";
int allowaltscreen = 1;
char *termname = "cupidterminal-256color";
unsigned int defaultfg = 258;
unsigned int defaultbg = 259;
unsigned int defaultcs = 256;
double usedfontsize = 0;
double defaultfontsize = 0;
char **opt_cmd = NULL;
char *opt_embed = NULL;
char *opt_io = NULL;
char *opt_line = NULL;
char *opt_name = NULL;
char *opt_title = NULL;
int opt_fixed = 0;
unsigned int cols = 80;
unsigned int rows = 24;

static void usage(void) {
    fprintf(stderr,
        "usage: cupidterminal [-aiv] [-c class] [-f font] [-g geometry] "
        "[-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid] [[-e] command [args ...]]\n"
        "       cupidterminal [-aiv] [-c class] [-f font] [-g geometry] "
        "[-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid] -l line [stty_args ...]\n");
    exit(1);
}
#endif /* !CUPID_NO_MAIN */

/* DEC Special Graphics (VT100 ACS): maps 0x41-0x7E to box-drawing etc. (st/rxvt table) */
static const char *const vt100_acs[62] = {
    "\xe2\x86\x91", "\xe2\x86\x93", "\xe2\x86\x92", "\xe2\x86\x90", "\xe2\x96\x88", "\xe2\x96\x9a", "\xe2\x98\x83", /* A-G */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* H-O */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* P-W */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, " ", /* X-_ */
    "\xe2\x97\x86", "\xe2\x96\x92", "\xe2\x90\x89", "\xe2\x90\x8c", "\xe2\x90\x8d", "\xe2\x90\x8a", "\xc2\xb0", "\xc2\xb1", /* `-g */
    "\xe2\x90\xa4", "\xe2\x90\x8b", "\xe2\x94\x98", "\xe2\x94\x90", "\xe2\x94\x8c", "\xe2\x94\x94", "\xe2\x94\xbc", "\xe2\x8e\xba", /* h-o */
    "\xe2\x8e\xbb", "\xe2\x94\x80", "\xe2\x8e\xbc", "\xe2\x8e\xbd", "\xe2\x94\x9c", "\xe2\x94\xa4", "\xe2\x94\xb4", "\xe2\x94\xac", /* p-w */
    "\xe2\x94\x82", "\xe2\x89\xa4", "\xe2\x89\xa5", "\xcf\x80", "\xe2\x89\xa0", "\xc2\xa3", "\xc2\xb7", /* x-~ */
};

int term_rows = 24;
int term_cols = 80;

Term term;
TermLine *term_lines = NULL;

static TermLine *primary_term_lines = NULL;
static TermLine *alternate_term_lines = NULL;
static unsigned char *tab_stops = NULL;

#define HISTORY_SIZE 2000
static TermLine *history_lines = NULL;
static int history_head = 0;   /* next insertion slot */
static int history_count = 0;  /* number of valid rows in history */

static void init_default_tab_stops(unsigned char *tabs, int cols) {
    unsigned int ts = (tabspaces > 0) ? tabspaces : 8;

    if (!tabs || cols <= 0) {
        return;
    }

    memset(tabs, 0, (size_t)cols);
    for (unsigned int c = ts; c < (unsigned int)cols; c += ts) {
        tabs[c] = 1;
    }
}

static int has_tab_stop_at(int col) {
    unsigned int ts = (tabspaces > 0) ? tabspaces : 8;

    if (col <= 0 || col >= term_cols) {
        return 0;
    }
    if (tab_stops) {
        return tab_stops[col] ? 1 : 0;
    }
    return ((unsigned int)col % ts) == 0;
}

static int next_tab_stop_col(int col) {
    for (int c = col + 1; c < term_cols; c++) {
        if (has_tab_stop_at(c)) {
            return c;
        }
    }
    return term_cols - 1;
}

static int prev_tab_stop_col(int col) {
    for (int c = col - 1; c > 0; c--) {
        if (has_tab_stop_at(c)) {
            return c;
        }
    }
    return 0;
}

static void init_default_cell(Glyph *cell) {
    if (!cell) {
        return;
    }

    cell->u = 0;
    cell->fg = COLOR_DEFAULT_FG;
    cell->bg = COLOR_DEFAULT_BG;
    cell->mode = 0;
}

static void *xcalloc_or_die(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "cupidterminal: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static TermLine *alloc_term_lines(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return NULL;
    TermLine *tl = xcalloc_or_die((size_t)rows, sizeof(TermLine));
    for (int r = 0; r < rows; r++) {
        tl[r].line = xcalloc_or_die((size_t)cols, sizeof(Glyph));
        for (int c = 0; c < cols; c++) {
            init_default_cell(&tl[r].line[c]);
        }
        /* combs lazy: NULL until term_append_comb allocates */
        tl[r].combs = NULL;
        tl[r].ncombs = 0;
        tl[r].combs_cap = 0;
        tl[r].dirty = 0;
    }
    return tl;
}

static void free_term_lines(TermLine *tl, int rows) {
    if (!tl) return;
    for (int r = 0; r < rows; r++) {
        free(tl[r].line);
        if (tl[r].combs) {
            for (uint16_t i = 0; i < tl[r].ncombs; i++) free(tl[r].combs[i].runes);
            free(tl[r].combs);
        }
    }
    free(tl);
}

static void free_line_combs(TermLine *line) {
    if (!line) return;
    for (uint16_t i = 0; i < line->ncombs; i++)
        free(line->combs[i].runes);
    free(line->combs);
    line->combs = NULL;
    line->ncombs = 0;
    line->combs_cap = 0;
}

static CombMark *find_line_comb(TermLine *line, int col) {
    if (!line) return NULL;
    for (uint16_t i = 0; i < line->ncombs; i++)
        if (line->combs[i].col == col)
            return &line->combs[i];
    return NULL;
}

static void copy_line_contents(TermLine *dst, const TermLine *src,
                               int copy_cols) {
    if (!dst || !src || !dst->line || !src->line || copy_cols <= 0)
        return;

    free_line_combs(dst);
    memcpy(dst->line, src->line, (size_t)copy_cols * sizeof(Glyph));
    for (int col = 0; col < copy_cols; col++)
        dst->line[col].mode &= ~ATTR_HASCOMB;

    for (uint16_t i = 0; i < src->ncombs; i++) {
        const CombMark *source = &src->combs[i];
        CombMark *target;

        if (source->col < 0 || source->col >= copy_cols || source->n == 0)
            continue;
        if (dst->ncombs == dst->combs_cap) {
            uint16_t cap = dst->combs_cap ? (uint16_t)(dst->combs_cap * 2) : 4;
            CombMark *grown = realloc(dst->combs, (size_t)cap * sizeof(*grown));
            if (!grown) {
                fprintf(stderr, "cupidterminal: out of memory\n");
                exit(EXIT_FAILURE);
            }
            dst->combs = grown;
            dst->combs_cap = cap;
        }
        target = &dst->combs[dst->ncombs++];
        target->col = source->col;
        target->n = source->n;
        target->cap = source->n;
        target->runes = xcalloc_or_die(source->n, sizeof(*target->runes));
        memcpy(target->runes, source->runes,
               (size_t)source->n * sizeof(*target->runes));
        dst->line[target->col].mode |= ATTR_HASCOMB;
    }
}

static TermLine *alloc_history_lines(int cols) {
    return alloc_term_lines(HISTORY_SIZE, cols);
}

static TermLine *resize_history_lines(TermLine *old_lines, int old_cols,
                                      int new_cols) {
    TermLine *new_lines = alloc_history_lines(new_cols);
    int copy_cols = old_cols < new_cols ? old_cols : new_cols;

    if (old_lines && copy_cols > 0)
        for (int r = 0; r < HISTORY_SIZE; r++)
            copy_line_contents(&new_lines[r], &old_lines[r], copy_cols);
    free_term_lines(old_lines, HISTORY_SIZE);
    return new_lines;
}

static void push_history_line(const TermLine *row, Term *state) {
    if (!history_lines || !row || !state || state->alt_screen_active ||
        term_cols <= 0) {
        return;
    }

    copy_line_contents(&history_lines[history_head], row, term_cols);
    history_head = (history_head + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }

    if (state->scrollback_offset > 0) {
        if (state->scrollback_offset < history_count) {
            state->scrollback_offset++;
        } else {
            state->scrollback_offset = history_count;
        }
    }
}

static TermLine *history_row_by_relative_index(int rel) {
    int oldest;
    int slot;

    if (!history_lines || rel < 0 || rel >= history_count) {
        return NULL;
    }

    oldest = (history_head - history_count + HISTORY_SIZE) % HISTORY_SIZE;
    slot = (oldest + rel) % HISTORY_SIZE;
    return &history_lines[slot];
}

static TermLine *resize_term_lines(TermLine *old_tl, int old_rows, int old_cols,
                                   int new_rows, int new_cols, int row_shift) {
    TermLine *new_tl = alloc_term_lines(new_rows, new_cols);

    if (!new_tl) {
        return NULL;
    }

    if (old_tl) {
        int available_rows = old_rows - row_shift;
        int copy_rows = (available_rows < new_rows) ? available_rows : new_rows;
        int copy_cols = (old_cols < new_cols) ? old_cols : new_cols;
        for (int r = 0; r < copy_rows; r++) {
            if (old_tl[r + row_shift].line && new_tl[r].line)
                copy_line_contents(&new_tl[r], &old_tl[r + row_shift], copy_cols);
        }
        free_term_lines(old_tl, old_rows);
    }

    return new_tl;
}

static void clear_term_lines_defaults(TermLine *tl) {
    if (!tl) return;
    for (int r = 0; r < term_rows; r++) {
        if (!tl[r].line) continue;
        free_line_combs(&tl[r]);
        for (int c = 0; c < term_cols; c++) {
            init_default_cell(&tl[r].line[c]);
        }
    }
}

static void clear_cell(Glyph *cell, const Term *state) {
    if (!cell || !state) {
        return;
    }

    cell->u = 0;
    cell->fg = state->current_fg;
    cell->bg = state->current_bg;
    cell->mode = state->current_mode & ~(ATTR_WIDE | ATTR_WDUMMY);
}

static void remove_line_comb(TermLine *line, int col) {
    if (!line) return;
    for (uint16_t i = 0; i < line->ncombs; i++) {
        if (line->combs[i].col != col)
            continue;
        free(line->combs[i].runes);
        if (i + 1 < line->ncombs)
            memmove(&line->combs[i], &line->combs[i + 1],
                    (size_t)(line->ncombs - i - 1) * sizeof(*line->combs));
        line->ncombs--;
        break;
    }
    if (line->line && col >= 0 && col < term_cols)
        line->line[col].mode &= ~ATTR_HASCOMB;
}

static void clear_cell_at(TermLine *line, int col, const Term *state) {
    if (!line || !line->line || col < 0 || col >= term_cols)
        return;
    remove_line_comb(line, col);
    clear_cell(&line->line[col], state);
}

static void shift_line_right(TermLine *line, int from, int shift,
                             const Term *state) {
    if (!line || !line->line || shift <= 0 || from < 0 || from >= term_cols)
        return;
    if (shift > term_cols - from)
        shift = term_cols - from;

    for (uint16_t i = 0; i < line->ncombs;) {
        int col = line->combs[i].col;
        if (col >= term_cols - shift) {
            remove_line_comb(line, col);
            continue;
        }
        if (col >= from)
            line->combs[i].col += shift;
        i++;
    }
    memmove(&line->line[from + shift], &line->line[from],
            (size_t)(term_cols - from - shift) * sizeof(Glyph));
    for (int col = from; col < from + shift; col++)
        clear_cell_at(line, col, state);
}

static void shift_line_left(TermLine *line, int from, int shift,
                            const Term *state) {
    if (!line || !line->line || shift <= 0 || from < 0 || from >= term_cols)
        return;
    if (shift > term_cols - from)
        shift = term_cols - from;

    for (uint16_t i = 0; i < line->ncombs;) {
        int col = line->combs[i].col;
        if (col >= from && col < from + shift) {
            remove_line_comb(line, col);
            continue;
        }
        if (col >= from + shift)
            line->combs[i].col -= shift;
        i++;
    }
    memmove(&line->line[from], &line->line[from + shift],
            (size_t)(term_cols - from - shift) * sizeof(Glyph));
    for (int col = term_cols - shift; col < term_cols; col++)
        clear_cell_at(line, col, state);
}

/*
 * free_row_combs: release all combining-mark data owned by term_lines[row].
 * After this call combs/ncombs/combs_cap are zeroed; the .line pointer and
 * .dirty flag are untouched.  Safe to call when combs is already NULL.
 */
static void free_row_combs(int row) {
    TermLine *tl = &term_lines[row];
    free_line_combs(tl);
}

static int scroll_region_top(const Term *state) {
    int top;

    if (!state) {
        return 0;
    }

    top = state->scroll_top;
    if (top < 0 || top >= term_rows) {
        top = 0;
    }
    return top;
}

static int scroll_region_bottom(const Term *state) {
    int bottom;
    int top;

    if (!state) {
        return term_rows - 1;
    }

    top = scroll_region_top(state);
    bottom = state->scroll_bottom;
    if (bottom < 0 || bottom >= term_rows) {
        bottom = term_rows - 1;
    }
    if (bottom < top) {
        bottom = term_rows - 1;
    }
    return bottom;
}

static int cursor_min_row(const Term *state) {
    if (state && state->origin_mode) {
        return scroll_region_top(state);
    }
    return 0;
}

static int cursor_max_row(const Term *state) {
    if (state && state->origin_mode) {
        return scroll_region_bottom(state);
    }
    return term_rows - 1;
}

static void cursor_home(Term *state) {
    if (!state) {
        return;
    }
    state->wrap_next = 0;
    state->row = cursor_min_row(state);
    state->col = 0;
}

static void cancel_pending_wrap(Term *state);

static void mark_row_dirty(int row) {
    if (term_lines && row >= 0 && row < term_rows)
        term_lines[row].dirty = 1;
}

static void mark_rows_dirty(int top, int bottom) {
    if (!term_lines) return;
    if (top < 0) top = 0;
    if (bottom >= term_rows) bottom = term_rows - 1;
    for (int r = top; r <= bottom; r++)
        term_lines[r].dirty = 1;
}

static void mark_all_rows_dirty(void) {
    if (term_lines) for (int r = 0; r < term_rows; r++) term_lines[r].dirty = 1;
}

void tfulldirt(void) {
    mark_all_rows_dirty();
}

void kscrollup_n(int n) {
    if (n <= 0 || term.alt_screen_active) {
        return;
    }
    if (history_count <= 0) {
        return;
    }
    term.scrollback_offset += n;
    if (term.scrollback_offset > history_count) {
        term.scrollback_offset = history_count;
    }
    mark_all_rows_dirty();
}

void kscrolldown_n(int n) {
    if (n <= 0 || term.alt_screen_active) {
        return;
    }
    term.scrollback_offset -= n;
    if (term.scrollback_offset < 0) {
        term.scrollback_offset = 0;
    }
    mark_all_rows_dirty();
}

void kscrollreset(void) {
    if (term.scrollback_offset != 0) {
        term.scrollback_offset = 0;
        mark_all_rows_dirty();
    }
}

int tgetscrolloffset(void) {
    return term.scrollback_offset;
}

static TermLine *resolve_visual_line(int visual_row) {
    int offset = term.scrollback_offset;
    int live_row;
    int rel;

    if (visual_row < 0 || visual_row >= term_rows || term_cols <= 0 || !term_lines) {
        return NULL;
    }

    if (term.alt_screen_active || offset <= 0) {
        return &term_lines[visual_row];
    }

    if (offset > history_count) {
        offset = history_count;
    }

    if (visual_row < offset) {
        rel = history_count - offset + visual_row;
        return history_row_by_relative_index(rel);
    }

    live_row = visual_row - offset;
    if (live_row < 0 || live_row >= term_rows) {
        return NULL;
    }
    return &term_lines[live_row];
}

const Glyph *tgetline(int visual_row) {
    TermLine *line = resolve_visual_line(visual_row);
    return line ? line->line : NULL;
}

static void clear_row_range(int row, int start_col, int end_col, const Term *state) {
    if (!state || !term_lines || row < 0 || row >= term_rows) {
        return;
    }

    if (start_col < 0) {
        start_col = 0;
    }
    if (end_col >= term_cols) {
        end_col = term_cols - 1;
    }
    if (start_col > end_col) {
        return;
    }

    if (start_col > 0 && (term_lines[row].line[start_col].mode & ATTR_WDUMMY)) {
        start_col--;
    }
    if (end_col + 1 < term_cols && (term_lines[row].line[end_col].mode & ATTR_WIDE)) {
        end_col++;
    }

    for (int c = start_col; c <= end_col; c++) {
        clear_cell_at(&term_lines[row], c, state);
    }
    mark_row_dirty(row);
}

static void clear_screen_range(int start_row, int start_col, int end_row, int end_col, const Term *state) {
    if (!state || !term_lines || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    if (start_row < 0) {
        start_row = 0;
    }
    if (end_row >= term_rows) {
        end_row = term_rows - 1;
    }
    if (start_row > end_row) {
        return;
    }

    for (int r = start_row; r <= end_row; r++) {
        int row_start = (r == start_row) ? start_col : 0;
        int row_end = (r == end_row) ? end_col : term_cols - 1;
        clear_row_range(r, row_start, row_end, state);
    }
}

/*
 * selscroll: adjust selection coordinates when scrolling, mirroring st's
 * selscroll().  Called before the actual buffer scroll so row numbers are
 * still valid for boundary checks.
 */
static void selscroll_adjust(Term *state, int orig, int n) {
    /* n > 0 = scroll up n lines (content moves up, rows decrease by n) */
    if (!state->sel_active) return;

    state->sel_anchor_row -= n;
    state->sel_row        -= n;

    /* If both ends fell off the top, clear selection */
    if (state->sel_anchor_row < orig && state->sel_row < orig) {
        state->sel_active = 0;
        return;
    }
    /* Clamp to scrolling region */
    if (state->sel_anchor_row < orig) {
        state->sel_anchor_row = orig;
        state->sel_anchor_col = 0;
    }
    if (state->sel_row < orig) {
        state->sel_row = orig;
        state->sel_col = 0;
    }
}

static void scroll_up_one_line(Term *state) {
    int top;
    int bottom;

    if (!state || !term_lines || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);
    if (top > bottom) {
        return;
    }

    if (!state->alt_screen_active && top == 0 && bottom == term_rows - 1) {
        push_history_line(&term_lines[top], state);
    }

    selscroll_adjust(state, top, 1);

    /*
     * Swap full TermLine structs so combs ownership moves with the row.
     * Save the top row (scrolling off), shift rows up by one, then put
     * the saved TermLine at bottom (as the new blank row).
     */
    {
        TermLine saved = term_lines[top];
        for (int r = top + 1; r <= bottom; r++) {
            term_lines[r - 1] = term_lines[r];
        }
        /* Place saved TermLine at bottom; its .line buffer becomes the new
           blank row — free any combs it carried then clear the cells. */
        term_lines[bottom] = saved;
        free_row_combs(bottom);
        for (int c = 0; c < term_cols; c++) {
            init_default_cell(&term_lines[bottom].line[c]);
        }
        term_lines[bottom].dirty = 1;
    }
    mark_rows_dirty(top, bottom - 1);
}

static void scroll_down_one_line(Term *state) {
    int top;
    int bottom;

    if (!state || !term_lines || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);
    if (top > bottom) {
        return;
    }

    /* Shift selection down (n = -1 → move rows up by -1 = down by 1) */
    selscroll_adjust(state, top, -1);

    /*
     * Swap full TermLine structs so combs ownership moves with the row.
     * Save the bottom row (scrolling off), shift rows down by one, then
     * put the saved TermLine at top (as the new blank row).
     */
    {
        TermLine saved = term_lines[bottom];
        for (int r = bottom - 1; r >= top; r--) {
            term_lines[r + 1] = term_lines[r];
        }
        /* Place saved TermLine at top; free combs it carried, clear cells. */
        term_lines[top] = saved;
        free_row_combs(top);
        for (int c = 0; c < term_cols; c++) {
            init_default_cell(&term_lines[top].line[c]);
        }
        term_lines[top].dirty = 1;
    }
    mark_rows_dirty(top + 1, bottom);
}

static void scroll_up_n_lines(Term *state, int n) {
    int top = scroll_region_top(state);
    int bottom = scroll_region_bottom(state);

    if (n <= 0 || top > bottom) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    while (n-- > 0) {
        scroll_up_one_line(state);
    }
}

static void scroll_down_n_lines(Term *state, int n) {
    int top = scroll_region_top(state);
    int bottom = scroll_region_bottom(state);

    if (n <= 0 || top > bottom) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    while (n-- > 0) {
        scroll_down_one_line(state);
    }
}

static void reverse_index(Term *state) {
    int top;
    int bottom;

    if (!state) {
        return;
    }

    cancel_pending_wrap(state);
    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);

    if (state->row < top || state->row > bottom) {
        if (state->row > 0) {
            state->row--;
        }
        return;
    }

    if (state->row == top) {
        scroll_down_one_line(state);
    } else {
        state->row--;
    }
}

static void clamp_cursor(Term *state) {
    int min_row;
    int max_row;

    if (!state) {
        return;
    }

    min_row = cursor_min_row(state);
    max_row = cursor_max_row(state);

    if (max_row < min_row) {
        min_row = 0;
        max_row = term_rows - 1;
    }

    if (state->row < min_row) state->row = min_row;
    if (state->row > max_row) state->row = max_row;
    if (state->col < 0) state->col = 0;
    if (state->col >= term_cols) {
        if (!(state->wrap_next && state->autowrap_mode && state->col == term_cols)) {
            state->col = term_cols - 1;
        }
    }
    if (state->col < 0) state->col = 0;
    if (!state->autowrap_mode) {
        state->wrap_next = 0;
        if (state->col >= term_cols) {
            state->col = term_cols - 1;
        }
    }
}

static void cancel_pending_wrap(Term *state) {
    if (!state) {
        return;
    }
    state->wrap_next = 0;
    state->wrap_overwrite_next = 0;
    /* Virtual cursor past last column (col == term_cols) folds to last cell. */
    if (state->col >= term_cols) {
        state->col = term_cols - 1;
    }
}

static void save_cursor_state(Term *state) {
    if (!state) {
        return;
    }

    cancel_pending_wrap(state);
    if (state->alt_screen_active) {
        state->alt_saved_row = state->row;
        state->alt_saved_col = state->col;
        state->alt_saved_fg = state->current_fg;
        state->alt_saved_bg = state->current_bg;
        state->alt_saved_mode = state->current_mode;
    } else {
        state->saved_row = state->row;
        state->saved_col = state->col;
        state->saved_fg = state->current_fg;
        state->saved_bg = state->current_bg;
        state->saved_mode = state->current_mode;
    }
}

static void restore_cursor_state(Term *state) {
    if (!state) {
        return;
    }

    cancel_pending_wrap(state);
    if (state->alt_screen_active) {
        state->row = state->alt_saved_row;
        state->col = state->alt_saved_col;
        state->current_fg = state->alt_saved_fg;
        state->current_bg = state->alt_saved_bg;
        state->current_mode = state->alt_saved_mode;
    } else {
        state->row = state->saved_row;
        state->col = state->saved_col;
        state->current_fg = state->saved_fg;
        state->current_bg = state->saved_bg;
        state->current_mode = state->saved_mode;
    }
    clamp_cursor(state);
}

static void activate_alternate_screen(Term *state) {
    if (!state || !allowaltscreen || state->alt_screen_active) {
        return;
    }

    if (!alternate_term_lines) {
        alternate_term_lines = alloc_term_lines(term_rows, term_cols);
        if (!alternate_term_lines) {
            return;
        }
    }

    term_lines = alternate_term_lines;
    state->alt_screen_active = 1;
    mark_all_rows_dirty();
    state->utf8_len = 0;
    state->wrap_next = 0;
    state->scrollback_offset = 0;
}

static void deactivate_alternate_screen(Term *state) {
    if (!state || !allowaltscreen || !state->alt_screen_active) {
        return;
    }

    clear_term_lines_defaults(alternate_term_lines);
    term_lines = primary_term_lines;
    state->alt_screen_active = 0;
    state->utf8_len = 0;
    state->wrap_next = 0;
    state->scrollback_offset = 0;
    clamp_cursor(state);
    mark_all_rows_dirty();
}

static int parse_csi_params(const char *body, int body_len, int *params, int max_params) {
    int count = 0;
    int value = 0;
    int have_digits = 0;
    int saw_separator = 0;
    int overflowed = 0;

    if (!body || body_len <= 0 || !params || max_params <= 0) {
        return 0;
    }

    for (int i = 0; i < body_len; i++) {
        char ch = body[i];

        if (ch >= '0' && ch <= '9') {
            if (!have_digits) {
                value = 0;
                have_digits = 1;
                overflowed = 0;
            }
            if (!overflowed) {
                int digit = ch - '0';
                if (value > (INT_MAX - digit) / 10) {
                    value = INT_MAX;
                    overflowed = 1;
                } else {
                    value = (value * 10) + digit;
                }
            }
            saw_separator = 0;
        } else if (ch == ';' || ch == ':') {
            if (count < max_params) {
                params[count++] = have_digits ? value : 0;
            }
            value = 0;
            have_digits = 0;
            saw_separator = 1;
            overflowed = 0;
        }
    }

    if (have_digits) {
        if (count < max_params) {
            params[count++] = value;
        }
    } else if (saw_separator) {
        if (count < max_params) {
            params[count++] = 0;
        }
    }

    return count;
}

static int parse_csi_params_dec(const char *body, int body_len, int *params, int max_params, int *is_private) {
    *is_private = 0;
    if (body_len > 0 && body[0] == '?') {
        *is_private = 1;
        body++;
        body_len--;
    }
    return parse_csi_params(body, body_len, params, max_params);
}

static int csi_param_default(const int *params, int count, int idx, int def) {
    if (!params || idx < 0 || idx >= count) {
        return def;
    }
    return params[idx] > 0 ? params[idx] : def;
}

static int csi_has_param(const int *params, int count, int value) {
    if (!params || count <= 0) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        if (params[i] == value) {
            return 1;
        }
    }
    return 0;
}

static void put_codepoint(utf8proc_int32_t codepoint, Term *state) {
    utf8proc_uint8_t encoded[4];
    utf8proc_ssize_t encoded_len;

    if (!state) {
        return;
    }

    encoded_len = utf8proc_encode_char(codepoint, encoded);
    if (encoded_len <= 0) {
        return;
    }

    for (utf8proc_ssize_t i = 0; i < encoded_len; i++) {
        tputc((char)encoded[i], state);
    }
}

static int utf8_expected_len(uint8_t lead) {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

void term_append_comb(int row, int col, Rune cp) {
    TermLine *tl;
    CombMark *cm = NULL;

    if (!term_lines || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return;
    }
    tl = &term_lines[row];
    if (!tl->line) return;

    /* Find or create CombMark for this column. */
    cm = find_line_comb(tl, col);
    if (!cm) {
        if (tl->ncombs == tl->combs_cap) {
            uint16_t new_cap = tl->combs_cap ? (uint16_t)(tl->combs_cap * 2) : 4;
            CombMark *grown = realloc(tl->combs, (size_t)new_cap * sizeof(*tl->combs));
            if (!grown) return;
            tl->combs = grown;
            tl->combs_cap = new_cap;
        }
        cm = &tl->combs[tl->ncombs++];
        cm->col = col;
        cm->n = 0;
        cm->cap = 0;
        cm->runes = NULL;
    }
    if (cm->n >= MAX_COMBINING_MARKS)
        return;
    if (cm->n == cm->cap) {
        unsigned int grown_cap = cm->cap ? (unsigned int)cm->cap * 2u : 2u;
        uint8_t new_rcap;
        if (grown_cap > MAX_COMBINING_MARKS)
            grown_cap = MAX_COMBINING_MARKS;
        new_rcap = (uint8_t)grown_cap;
        Rune *grown = realloc(cm->runes, (size_t)new_rcap * sizeof(*cm->runes));
        if (!grown) return;
        cm->runes = grown;
        cm->cap = new_rcap;
    }
    cm->runes[cm->n++] = cp;
    tl->line[col].mode |= ATTR_HASCOMB;
    tl->dirty = 1;
}

/* Phase 3.6: Encode the cluster anchored at (row,col) — base Rune plus any
   combining marks from the row's side-channel — into out/cap as NUL-terminated
   UTF-8.  Returns bytes written (NOT counting NUL), 0 on empty / error. */
static size_t render_line_cluster(const TermLine *line, int col,
                                  char *out, size_t cap) {
    const Glyph *g;
    size_t n = 0;

    if (!line || !line->line || !out || cap == 0 ||
        col < 0 || col >= term_cols) {
        if (out && cap) out[0] = '\0';
        return 0;
    }
    g = &line->line[col];

    if (g->u != 0) {
        utf8proc_uint8_t buf[4];
        utf8proc_ssize_t r = utf8proc_encode_char((utf8proc_int32_t)g->u, buf);
        if (r > 0 && (size_t)r < cap) {
            for (utf8proc_ssize_t i = 0; i < r; i++) out[n++] = (char)buf[i];
        }
    }

    if (g->mode & ATTR_HASCOMB) {
        const CombMark *cm = NULL;
        for (uint16_t i = 0; i < line->ncombs; i++) {
            if (line->combs[i].col == col) {
                cm = &line->combs[i];
                break;
            }
        }
        if (cm) {
            for (uint8_t i = 0; i < cm->n; i++) {
                utf8proc_uint8_t buf[4];
                utf8proc_ssize_t r = utf8proc_encode_char((utf8proc_int32_t)cm->runes[i], buf);
                if (r > 0 && n + (size_t)r < cap) {
                    for (utf8proc_ssize_t j = 0; j < r; j++) out[n++] = (char)buf[j];
                }
            }
        }
    }
    out[n] = '\0';
    return n;
}

size_t term_render_cluster(int row, int col, char *out, size_t cap) {
    if (!term_lines || row < 0 || row >= term_rows) {
        if (out && cap) out[0] = '\0';
        return 0;
    }
    return render_line_cluster(&term_lines[row], col, out, cap);
}

size_t term_render_visual_cluster(int row, int col, char *out, size_t cap) {
    return render_line_cluster(resolve_visual_line(row), col, out, cap);
}

static int append_combining_mark(int row, int col, const uint8_t *bytes, int byte_len) {
    Glyph *cell;

    if (!term_lines || !bytes || byte_len <= 0 || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return 0;
    }

    if ((term_lines[row].line[col].mode & ATTR_WDUMMY) && col > 0) {
        col--;
    }
    cell = &term_lines[row].line[col];
    if (cell->u == 0 || (cell->mode & ATTR_WDUMMY)) {
        return 0;
    }

    /* Phase 3.6: side-channel is the sole storage path for combining marks. */
    {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t r = utf8proc_iterate(
            (const utf8proc_uint8_t *)bytes, (utf8proc_ssize_t)byte_len, &cp);
        if (r > 0 && cp >= 0) {
            term_append_comb(row, col, (Rune)cp);
        }
    }

    mark_row_dirty(row);
    return 1;
}

static void normalize_cell_for_write(int row, int col, const Term *state) {
    if (!state || !term_lines || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return;
    }

    if (term_lines[row].line[col].mode & ATTR_WDUMMY) {
        if (col > 0 && (term_lines[row].line[col - 1].mode & ATTR_WIDE)) {
            clear_cell_at(&term_lines[row], col - 1, state);
        }
        clear_cell_at(&term_lines[row], col, state);
    }

    if (term_lines[row].line[col].mode & ATTR_WIDE) {
        if (col + 1 < term_cols && (term_lines[row].line[col + 1].mode & ATTR_WDUMMY)) {
            clear_cell_at(&term_lines[row], col + 1, state);
        }
        clear_cell_at(&term_lines[row], col, state);
    }

    remove_line_comb(&term_lines[row], col);
}

static void advance_row_with_scroll(Term *state) {
    int top;
    int bottom;
    int was_in_region;

    if (!state) {
        return;
    }

    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);
    was_in_region = (state->row >= top && state->row <= bottom);

    state->row++;

    if (was_in_region && state->row > bottom) {
        scroll_up_one_line(state);
        state->row = bottom;
    } else if (state->row >= term_rows) {
        state->row = term_rows - 1;
    }
}

static void wrap_to_next_line(Term *state) {
    if (!state) {
        return;
    }

    /* Mark the last glyph on this line as soft-wrapped (st's ATTR_WRAP) */
    if (term_lines && state->row >= 0 && state->row < term_rows && term_cols > 0) {
        term_lines[state->row].line[term_cols - 1].mode |= ATTR_WRAP;
    }

    state->wrap_next = 0;
    state->col = 0;
    advance_row_with_scroll(state);
}

static void osc_reset(Term *state) {
    state->osc_active = 0;
    state->osc_esc_pending = 0;
    state->osc_overflow = 0;
    state->osc_len = 0;
}

static void osc_append_byte(Term *state, uint8_t b) {
    if (!state) {
        return;
    }
    if (state->osc_len >= (int)(sizeof(state->osc_buf) - 1)) {
        state->osc_overflow = 1;
        return;
    }
    state->osc_buf[state->osc_len++] = (char)b;
}

static int base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static size_t osc52_decode_base64(const char *encoded, uint8_t *out, size_t out_cap, int *ok) {
    int vals[4] = {0, 0, 0, 0};
    int vcount = 0;
    int saw_pad = 0;
    size_t out_len = 0;

    if (ok) {
        *ok = 0;
    }
    if (!encoded || !out) {
        return 0;
    }

    for (const char *p = encoded; *p; p++) {
        unsigned char uch = (unsigned char)*p;
        int v;

        if (isspace(uch)) {
            continue;
        }

        if (*p == '=') {
            saw_pad++;
            if (vcount >= 4) {
                return 0;
            }
            vals[vcount++] = 0;
        } else {
            if (saw_pad) {
                return 0;
            }
            v = base64_value(*p);
            if (v < 0) {
                return 0;
            }
            vals[vcount++] = v;
        }

        if (vcount == 4) {
            size_t add = (saw_pad == 0) ? 3u : (saw_pad == 1) ? 2u : 1u;
            if (out_len + add > out_cap) {
                return 0;
            }

            out[out_len++] = (uint8_t)((vals[0] << 2) | (vals[1] >> 4));
            if (add >= 2) {
                out[out_len++] = (uint8_t)(((vals[1] & 0x0F) << 4) | (vals[2] >> 2));
            }
            if (add == 3) {
                out[out_len++] = (uint8_t)(((vals[2] & 0x03) << 6) | vals[3]);
            }

            if (saw_pad) {
                for (p++; *p; p++) {
                    if (!isspace((unsigned char)*p)) {
                        return 0;
                    }
                }
                if (ok) {
                    *ok = 1;
                }
                return out_len;
            }

            vcount = 0;
            saw_pad = 0;
        }
    }

    if (vcount == 2) {
        if (out_len + 1 > out_cap) {
            return 0;
        }
        out[out_len++] = (uint8_t)((vals[0] << 2) | (vals[1] >> 4));
    } else if (vcount == 3) {
        if (out_len + 2 > out_cap) {
            return 0;
        }
        out[out_len++] = (uint8_t)((vals[0] << 2) | (vals[1] >> 4));
        out[out_len++] = (uint8_t)(((vals[1] & 0x0F) << 4) | (vals[2] >> 2));
    } else if (vcount != 0) {
        return 0;
    }

    if (ok) {
        *ok = 1;
    }
    return out_len;
}

/*
 * Parse an X11 color specification into a packed 24-bit true-color value.
 * Handles "#rrggbb", "rgb:RR/GG/BB", and 8-bit "#RRRRGGGGBBBB".
 * Returns TRUECOLOR(r,g,b), or 0 on failure.
 */
static uint32_t parse_x11_color(const char *spec) {
    unsigned int r, g, b;
    if (!spec || !*spec) return 0;
    if (*spec == '#') {
        size_t n = strlen(spec + 1);
        if (n == 6) {
            if (sscanf(spec + 1, "%02x%02x%02x", &r, &g, &b) == 3)
                return TRUECOLOR(r, g, b);
        } else if (n == 12) {
            unsigned int r4, g4, b4;
            if (sscanf(spec + 1, "%04x%04x%04x", &r4, &g4, &b4) == 3)
                return TRUECOLOR(r4 >> 8, g4 >> 8, b4 >> 8);
        }
    } else if (strncmp(spec, "rgb:", 4) == 0) {
        if (sscanf(spec + 4, "%x/%x/%x", &r, &g, &b) == 3) {
            if (r > 0xFF) r >>= 8;
            if (g > 0xFF) g >>= 8;
            if (b > 0xFF) b >>= 8;
            return TRUECOLOR(r, g, b);
        }
    }
    return 0;
}

static void osc_finalize(Term *state) {
    const char *payload;
    const char *semi;
    const char *arg1;
    const char *arg2;
    size_t title_len;
    int cmd;

    if (!state) {
        return;
    }
    if (state->osc_overflow) {
        osc_reset(state);
        return;
    }

    state->osc_buf[state->osc_len] = '\0';
    payload = state->osc_buf;
    semi = strchr(payload, ';');
    if (!semi) {
        osc_reset(state);
        return;
    }

    cmd = (int)strtol(payload, NULL, 10);
    arg1 = semi + 1;

    if (cmd == 0 || cmd == 2) {
        const char *title = semi + 1;
        title_len = strlen(title);
        if (title_len >= sizeof(state->window_title)) {
            title_len = sizeof(state->window_title) - 1;
        }
        memcpy(state->window_title, title, title_len);
        state->window_title[title_len] = '\0';
        state->title_dirty = 1;
        xsettitle(state->window_title);
    } else if (cmd == 4) {
        /* OSC 4;index;color – set palette entry */
        const char *idx_end;
        int idx;
        uint32_t col;
        idx = (int)strtol(arg1, (char **)&idx_end, 10);
        if (idx_end > arg1 && *idx_end == ';' && idx >= 0 && idx < 256) {
            const char *colstr = idx_end + 1;
            if (strcmp(colstr, "?") != 0) {
                col = parse_x11_color(colstr);
                if (col) {
                    state->palette_override[idx] = col;
                    state->palette_overridden[idx] = 1;
                    mark_all_rows_dirty();
                }
            }
        }
    } else if (cmd == 10 || cmd == 11 || cmd == 12) {
        /* OSC 10/11/12;color – set fg/bg/cursor dynamic color */
        if (strcmp(arg1, "?") != 0) {
            uint32_t col = parse_x11_color(arg1);
            if (col) {
                if (cmd == 10)      state->osc_fg_color = col;
                else if (cmd == 11) state->osc_bg_color = col;
                else                state->osc_cs_color = col;
                mark_all_rows_dirty();
            }
        }
    } else if (cmd == 52 && allowwindowops) {
        int ok = 0;
        size_t decoded_len;

        arg2 = strchr(arg1, ';');
        if (arg2 && arg2[1] != '\0' && strcmp(arg2 + 1, "?") != 0) {
            decoded_len = osc52_decode_base64(arg2 + 1, state->osc52_buf, sizeof(state->osc52_buf), &ok);
            if (ok) {
                state->osc52_len = decoded_len;
                state->osc52_pending = 1;
            }
        }
    } else if (cmd == 104) {
        /* OSC 104;index – reset palette entry (or all if no arg) */
        const char *p = arg1;
        if (!*p) {
            memset(state->palette_overridden, 0, sizeof(state->palette_overridden));
            mark_all_rows_dirty();
        } else {
            while (*p) {
                int idx = (int)strtol(p, (char **)&p, 10);
                if (idx >= 0 && idx < 256) {
                    state->palette_overridden[idx] = 0;
                    state->palette_override[idx] = 0;
                }
                if (*p == ';') p++;
            }
            mark_all_rows_dirty();
        }
    } else if (cmd == 110) {
        state->osc_fg_color = 0;
        mark_all_rows_dirty();
    } else if (cmd == 111) {
        state->osc_bg_color = 0;
        mark_all_rows_dirty();
    } else if (cmd == 112) {
        state->osc_cs_color = 0;
        mark_all_rows_dirty();
    }

    osc_reset(state);
}

static void terminal_soft_reset(Term *state) {
    if (!state) {
        return;
    }

    clear_term_lines_defaults(primary_term_lines);
    clear_term_lines_defaults(alternate_term_lines);

    term_lines = primary_term_lines ? primary_term_lines : term_lines;
    state->alt_screen_active = 0;

    state->row = 0;
    state->col = 0;
    treset(state);

    state->saved_row = 0;
    state->saved_col = 0;
    state->saved_fg = COLOR_DEFAULT_FG;
    state->saved_bg = COLOR_DEFAULT_BG;
    state->saved_mode = 0;
    state->alt_saved_row = 0;
    state->alt_saved_col = 0;
    state->alt_saved_fg = COLOR_DEFAULT_FG;
    state->alt_saved_bg = COLOR_DEFAULT_BG;
    state->alt_saved_mode = 0;

    state->cursor_visible = 1;
    state->scroll_top = -1;
    state->scroll_bottom = -1;
    state->bracketed_paste_mode = 0;
    state->autowrap_mode = 1;
    state->origin_mode = 0;
    state->insert_mode = 0;
    state->print_mode = 0;
    state->wrap_next = 0;

    state->mouse_reporting_basic = 0;
    state->mouse_reporting_button = 0;
    state->mouse_reporting_any = 0;
    state->mouse_reporting_x10 = 0;
    state->mouse_sgr_mode = 0;
    state->application_cursor_keys = 0;
    state->application_keypad = 0;
    state->keyboard_lock = 0;
    state->meta_eight_bit = 0;

    state->charset_g0 = 0;
    state->charset_g1 = 0;
    state->charset_g2 = 0;
    state->charset_g3 = 0;
    state->gl = 0;

    state->utf8_len = 0;
    state->csi_pending_len = 0;
    state->str_ignore_active = 0;
    state->str_ignore_esc_pending = 0;
    state->scrollback_offset = 0;
    state->osc52_len = 0;
    state->osc52_pending = 0;
    osc_reset(state);

    if (tab_stops) {
        init_default_tab_stops(tab_stops, term_cols);
    }
    mark_all_rows_dirty();
}

void tresize(int new_rows, int new_cols) {
    TermLine *new_history = NULL;
    TermLine *new_primary_tl;
    TermLine *new_alt_tl = NULL;
    unsigned char *new_tabs = NULL;
    int old_rows = term_rows;
    int old_cols = term_cols;
    int row_shift = 0;

    if (new_rows <= 0) new_rows = 1;
    if (new_cols <= 0) new_cols = 1;
    if (new_rows < old_rows && term.row >= new_rows)
        row_shift = term.row - new_rows + 1;

    if (primary_term_lines != NULL && new_rows == term_rows && new_cols == term_cols) {
        term_lines = (term.alt_screen_active && alternate_term_lines) ? alternate_term_lines : primary_term_lines;
        return;
    }

    new_primary_tl = resize_term_lines(primary_term_lines, old_rows, old_cols,
                                       new_rows, new_cols, row_shift);
    if (!new_primary_tl) {
        return;
    }

    if (alternate_term_lines) {
        new_alt_tl = resize_term_lines(alternate_term_lines, old_rows, old_cols,
                                       new_rows, new_cols, row_shift);
        if (!new_alt_tl) {
            primary_term_lines = new_primary_tl;
            alternate_term_lines = NULL;
            term.alt_screen_active = 0;
            term_rows = new_rows;
            term_cols = new_cols;
            term_lines = primary_term_lines;
            clamp_cursor(&term);
            return;
        }
    }

    if (history_lines) {
        new_history = resize_history_lines(history_lines, old_cols, new_cols);
    } else {
        new_history = alloc_history_lines(new_cols);
        if (new_history) {
            history_count = 0;
            history_head = 0;
        }
    }

    primary_term_lines = new_primary_tl;
    alternate_term_lines = new_alt_tl;
    if (new_history) {
        history_lines = new_history;
    }
    term_rows = new_rows;
    term_cols = new_cols;

    new_tabs = xcalloc_or_die((size_t)new_cols, sizeof(unsigned char));
    {
        if (tab_stops) {
            int copy_cols = (old_cols < new_cols) ? old_cols : new_cols;
            if (copy_cols > 0) {
                memcpy(new_tabs, tab_stops, (size_t)copy_cols);
            }
            for (int c = copy_cols; c < new_cols; c++) {
                new_tabs[c] = (unsigned char)(((c % 8) == 0) ? 1 : 0);
            }
            free(tab_stops);
        } else {
            init_default_tab_stops(new_tabs, new_cols);
        }
        tab_stops = new_tabs;
    }

    if (term.alt_screen_active) {
        if (!alternate_term_lines)
            alternate_term_lines = alloc_term_lines(term_rows, term_cols);
        term_lines = alternate_term_lines ? alternate_term_lines : primary_term_lines;
    } else {
        term_lines = primary_term_lines;
    }

    mark_all_rows_dirty();

    term.scroll_top = -1;
    term.scroll_bottom = -1;
    if (term.scrollback_offset > history_count) {
        term.scrollback_offset = history_count;
    }

    clamp_cursor(&term);
    if (term.saved_row < 0) term.saved_row = 0;
    if (term.saved_row >= term_rows) term.saved_row = term_rows - 1;
    if (term.saved_col < 0) term.saved_col = 0;
    if (term.saved_col >= term_cols) term.saved_col = term_cols - 1;
    if (term.alt_saved_row < 0) term.alt_saved_row = 0;
    if (term.alt_saved_row >= term_rows) term.alt_saved_row = term_rows - 1;
    if (term.alt_saved_col < 0) term.alt_saved_col = 0;
    if (term.alt_saved_col >= term_cols) term.alt_saved_col = term_cols - 1;
}

void tnew(Term *state) {
    if (!state) {
        return;
    }

    if (primary_term_lines) {
        free_term_lines(primary_term_lines, term_rows);
        primary_term_lines = NULL;
    }
    if (alternate_term_lines) {
        free_term_lines(alternate_term_lines, term_rows);
        alternate_term_lines = NULL;
    }
    if (tab_stops) {
        free(tab_stops);
        tab_stops = NULL;
    }
    if (history_lines) {
        free_term_lines(history_lines, HISTORY_SIZE);
        history_lines = NULL;
    }
    history_count = 0;
    history_head = 0;
    term_lines = NULL;

    term_rows = 24;
    term_cols = 80;

    memset(state, 0, sizeof(*state));
    state->csi_pending_len = 0;
    state->current_fg = COLOR_DEFAULT_FG;
    state->current_bg = COLOR_DEFAULT_BG;
    state->saved_fg = COLOR_DEFAULT_FG;
    state->saved_bg = COLOR_DEFAULT_BG;
    state->cursor_visible = 1;
    state->autowrap_mode = 1;
    state->origin_mode = 0;
    state->insert_mode = 0;
    state->scroll_top = -1;
    state->scroll_bottom = -1;
    state->alt_saved_scroll_top = -1;
    state->alt_saved_scroll_bottom = -1;
    state->title_dirty = 0;
    state->utf8_mode = 1;
    state->scrollback_offset = 0;
    state->wrap_next = 0;
    state->str_ignore_active = 0;
    state->str_ignore_esc_pending = 0;
    strncpy(state->window_title, "cupidterminal", sizeof(state->window_title) - 1);

    tresize(24, 80);
}

void treset(Term *s) {
    if (!s) {
        return;
    }
    s->current_fg = COLOR_DEFAULT_FG;
    s->current_bg = COLOR_DEFAULT_BG;
    s->current_mode = 0;
}

static void csihandle(const char *seq, int len, Term *state,
    terminal_response_fn response_fn, void *response_ctx) {
    int param_values[16] = {0};
    int param_count;
    int is_private = 0;
    char cmd;

    if (!seq || !state || len < 3 || seq[0] != '\033' || seq[1] != '[') {
        return;
    }

    cmd = seq[len - 1];
    param_count = parse_csi_params_dec(&seq[2], len - 3, param_values, 16, &is_private);

    {
        int min_row = cursor_min_row(state);
        int max_row = cursor_max_row(state);
        if (state->row < min_row) state->row = min_row;
        if (state->row > max_row) state->row = max_row;
    }
    if (state->col < 0) state->col = 0;

    if (is_private) {
        if (cmd == 'h') {
            if (csi_has_param(param_values, param_count, 6)) {
                state->origin_mode = 1;
                cursor_home(state);
            }
            if (csi_has_param(param_values, param_count, 7)) {
                state->autowrap_mode = 1;
            }
            if (csi_has_param(param_values, param_count, 25)) {
                state->cursor_visible = 1;
            }
            if (allowaltscreen &&
                csi_has_param(param_values, param_count, 1049)) {
                save_cursor_state(state);
                activate_alternate_screen(state);
            } else if (csi_has_param(param_values, param_count, 47) ||
                       csi_has_param(param_values, param_count, 1047)) {
                activate_alternate_screen(state);
            }
            if (csi_has_param(param_values, param_count, 1048)) {
                save_cursor_state(state);
            }
            if (csi_has_param(param_values, param_count, 2004)) {
                state->bracketed_paste_mode = 1;
            }
            if (csi_has_param(param_values, param_count, 1000)) {
                state->mouse_reporting_x10 = 0;
                state->mouse_reporting_basic = 1;
                state->mouse_reporting_button = 0;
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1002)) {
                state->mouse_reporting_x10 = 0;
                state->mouse_reporting_basic = 0;
                state->mouse_reporting_button = 1;
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1003)) {
                state->mouse_reporting_x10 = 0;
                state->mouse_reporting_basic = 0;
                state->mouse_reporting_button = 0;
                state->mouse_reporting_any = 1;
            }
            if (csi_has_param(param_values, param_count, 1006)) {
                state->mouse_sgr_mode = 1;
            }
            if (csi_has_param(param_values, param_count, 9)) {
                state->mouse_reporting_x10 = 1;
                state->mouse_reporting_basic = 0;
                state->mouse_reporting_button = 0;
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1034)) {
                state->meta_eight_bit = 1;
            }
            if (csi_has_param(param_values, param_count, 1)) {
                state->application_cursor_keys = 1;
            }
            if (csi_has_param(param_values, param_count, 5)) {
                if (!state->screen_reverse) {
                    state->screen_reverse = 1;
                    mark_all_rows_dirty();
                }
            }
            if (csi_has_param(param_values, param_count, 1004)) {
                state->focus_mode = 1;
            }
        } else if (cmd == 'q') {
            int p = (param_count && param_values[0] >= 0) ? param_values[0] : 0;
            if (p >= 0 && p <= 7) {
                state->cursorshape = p;
            }
        } else if (cmd == 'l') {
            if (csi_has_param(param_values, param_count, 6)) {
                state->origin_mode = 0;
                cursor_home(state);
            }
            if (csi_has_param(param_values, param_count, 7)) {
                state->autowrap_mode = 0;
                state->wrap_next = 0;
                state->wrap_overwrite_next = 0;
                if (state->col >= term_cols) {
                    state->col = term_cols - 1;
                }
            }
            if (csi_has_param(param_values, param_count, 25)) {
                state->cursor_visible = 0;
            }
            if (allowaltscreen &&
                csi_has_param(param_values, param_count, 1049)) {
                deactivate_alternate_screen(state);
                restore_cursor_state(state);
            } else if (csi_has_param(param_values, param_count, 47) ||
                       csi_has_param(param_values, param_count, 1047)) {
                deactivate_alternate_screen(state);
            }
            if (csi_has_param(param_values, param_count, 1048)) {
                restore_cursor_state(state);
            }
            if (csi_has_param(param_values, param_count, 2004)) {
                state->bracketed_paste_mode = 0;
            }
            if (csi_has_param(param_values, param_count, 1000)) {
                state->mouse_reporting_basic = 0;
            }
            if (csi_has_param(param_values, param_count, 1002)) {
                state->mouse_reporting_button = 0;
            }
            if (csi_has_param(param_values, param_count, 1003)) {
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1006)) {
                state->mouse_sgr_mode = 0;
            }
            if (csi_has_param(param_values, param_count, 9)) {
                state->mouse_reporting_x10 = 0;
            }
            if (csi_has_param(param_values, param_count, 1034)) {
                state->meta_eight_bit = 0;
            }
            if (csi_has_param(param_values, param_count, 1)) {
                state->application_cursor_keys = 0;
            }
            if (csi_has_param(param_values, param_count, 5)) {
                if (state->screen_reverse) {
                    state->screen_reverse = 0;
                    mark_all_rows_dirty();
                }
            }
            if (csi_has_param(param_values, param_count, 1004)) {
                state->focus_mode = 0;
            }
        }
        return;
    }

    if ((cmd == 'h' || cmd == 'l') &&
        csi_has_param(param_values, param_count, 2)) {
        state->keyboard_lock = (cmd == 'h');
        return;
    }

    if (cmd == 'n' && response_fn && param_count == 1) {
        if (param_values[0] == 5) {
            static const char status_ok[] = "\033[0n";
            response_fn((const uint8_t *)status_ok, sizeof(status_ok) - 1, response_ctx);
            return;
        }
        if (param_values[0] == 6) {
            int report_row = state->row;
            int report_col = state->col;
            char buf[32];
            int n;

            if (report_row < 0) report_row = 0;
            if (report_row >= term_rows) report_row = term_rows - 1;
            if (report_col < 0) report_col = 0;
            /* CPR uses 1-based columns; virtual margin (col==term_cols) reports last column+1 */
            if (report_col >= term_cols) {
                report_col = term_cols - 1;
            }

            n = snprintf(buf, sizeof(buf), "\033[%d;%dR", report_row + 1, report_col + 1);
            if (n > 0 && (size_t)n < sizeof(buf)) {
                response_fn((const uint8_t *)buf, (size_t)n, response_ctx);
            }
            return;
        }
    }

    if ((param_count == 0 || (param_count == 1 && param_values[0] == 0)) && cmd == 'c' && response_fn) {
        static const char da[] = "\033[?6c";
        response_fn((const uint8_t *)da, sizeof(da) - 1, response_ctx);
        return;
    }

    switch (cmd) {
        case 'q': {
            int p = (param_count && param_values[0] >= 0) ? param_values[0] : 0;
            if (p <= 7) {
                state->cursorshape = p;
            }
        } break;

        case 'i': {
            int p = param_count ? param_values[0] : 0;
            if (p == 0) tprinter_screen();
            else if (p == 1 && state->row >= 0 && state->row < term_rows) {
                for (int col = 0; col < term_cols; col++) {
                    char cluster[MAX_CLUSTER_UTF8];
                    size_t n = term_render_cluster(state->row, col, cluster, sizeof(cluster));
                    printer_write(n ? cluster : " ", n ? n : 1);
                }
                printer_write("\n", 1);
            } else if (p == 2) tprinter_selection();
            else if (p == 4) state->print_mode = 0;
            else if (p == 5) state->print_mode = 1;
        } break;

        case 'H':
        case 'f': {
            int min_row = cursor_min_row(state);
            int max_row = cursor_max_row(state);
            int r = csi_param_default(param_values, param_count, 0, 1);
            int c = csi_param_default(param_values, param_count, 1, 1);

            if (state->origin_mode) {
                r = min_row + r - 1;
            } else {
                r--;
            }
            c--;
            if (r < min_row) r = min_row;
            if (r > max_row) r = max_row;
            if (c < 0) c = 0;
            if (c >= term_cols) c = term_cols - 1;
            cancel_pending_wrap(state);
            state->row = r;
            state->col = c;
        } break;

        case 'E': {
            int min_row = cursor_min_row(state);
            int max_row = cursor_max_row(state);
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->row += n;
            if (state->row < min_row) state->row = min_row;
            if (state->row > max_row) state->row = max_row;
            state->col = 0;
        } break;

        case 'F': {
            int min_row = cursor_min_row(state);
            int max_row = cursor_max_row(state);
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->row -= n;
            if (state->row < min_row) state->row = min_row;
            if (state->row > max_row) state->row = max_row;
            state->col = 0;
        } break;

        case 'G':
        case '`': {
            int c = csi_param_default(param_values, param_count, 0, 1) - 1;
            cancel_pending_wrap(state);
            if (c < 0) c = 0;
            if (c >= term_cols) c = term_cols - 1;
            state->col = c;
        } break;

        case 'J': {
            int n = (param_count ? param_values[0] : 0);

            cancel_pending_wrap(state);
            if (n == 2 || n == 3) {
                clear_screen_range(0, 0, term_rows - 1, term_cols - 1, state);
            } else if (n == 0) {
                clear_screen_range(state->row, state->col, term_rows - 1, term_cols - 1, state);
            } else if (n == 1) {
                clear_screen_range(0, 0, state->row, state->col, state);
            }
        } break;

        case 'K': {
            int n = (param_count ? param_values[0] : 0);

            cancel_pending_wrap(state);
            if (n == 2) {
                clear_row_range(state->row, 0, term_cols - 1, state);
            } else if (n == 0) {
                clear_row_range(state->row, state->col, term_cols - 1, state);
            } else if (n == 1) {
                clear_row_range(state->row, 0, state->col, state);
            }
        } break;

        case 'A': {
            int min_row = cursor_min_row(state);
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->row -= n;
            if (state->row < min_row) state->row = min_row;
        } break;

        case 'B': {
            int max_row = cursor_max_row(state);
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->row += n;
            if (state->row > max_row) state->row = max_row;
        } break;

        case 'C': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->col += n;
            if (state->col >= term_cols) {
                if (state->autowrap_mode && state->col == term_cols) {
                    state->wrap_next = 1;
                } else {
                    state->col = term_cols - 1;
                }
            }
        } break;

        case 'D': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int was_margin = state->wrap_next && state->col >= term_cols;
            cancel_pending_wrap(state);
            if (was_margin && n > 0) {
                n--;
            }
            state->col -= n;
            if (state->col < 0) state->col = 0;
        } break;

        case 'I': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            while (n-- > 0) {
                state->col = next_tab_stop_col(state->col);
            }
        } break;

        case 'Z': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            while (n-- > 0) {
                state->col = prev_tab_stop_col(state->col);
            }
        } break;

        case 'g': {
            int n = (param_count ? param_values[0] : 0);
            if (n == 0) {
                if (tab_stops && state->col >= 0 && state->col < term_cols) {
                    tab_stops[state->col] = 0;
                }
            } else if (n == 3) {
                if (tab_stops) {
                    memset(tab_stops, 0, (size_t)term_cols);
                }
            }
        } break;

        case 'd': {
            int min_row = cursor_min_row(state);
            int max_row = cursor_max_row(state);
            int r = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            if (state->origin_mode) {
                r = min_row + r - 1;
            } else {
                r--;
            }
            if (r < min_row) r = min_row;
            if (r > max_row) r = max_row;
            state->row = r;
        } break;

        case 'a': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            state->col += n;
            if (state->col >= term_cols) state->col = term_cols - 1;
        } break;

        case 'h': {
            if (csi_has_param(param_values, param_count, 4)) {
                state->insert_mode = 1;
            }
            if (csi_has_param(param_values, param_count, 12)) {
                state->echo_mode = 0;  /* SRM set = echo off */
            }
            if (csi_has_param(param_values, param_count, 20)) {
                state->lnm_mode = 1;
            }
        } break;

        case 'l': {
            if (csi_has_param(param_values, param_count, 4)) {
                state->insert_mode = 0;
            }
            if (csi_has_param(param_values, param_count, 12)) {
                state->echo_mode = 1;  /* SRM reset = echo on */
            }
            if (csi_has_param(param_values, param_count, 20)) {
                state->lnm_mode = 0;
            }
        } break;

        case 'r': {
            if (param_count == 0) {
                state->scroll_top = -1;
                state->scroll_bottom = -1;
                cursor_home(state);
                break;
            }

            int top = csi_param_default(param_values, param_count, 0, 1);
            int bottom = csi_param_default(param_values, param_count, 1, term_rows);

            if (top < 1) top = 1;
            if (bottom < 1) bottom = term_rows;
            if (top > term_rows) top = term_rows;
            if (bottom > term_rows) bottom = term_rows;

            if (top >= bottom) {
                break;
            }

            state->scroll_top = top - 1;
            state->scroll_bottom = bottom - 1;
            cursor_home(state);
        } break;

        case 's':
            save_cursor_state(state);
            break;

        case 'u':
            restore_cursor_state(state);
            break;

        case 'm': {
            if (param_count == 0) {
                param_values[0] = 0;
                param_count = 1;
            }

            for (int i = 0; i < param_count; i++) {
                int p = param_values[i];
                if (p == 0) {
                    state->current_fg = COLOR_DEFAULT_FG;
                    state->current_bg = COLOR_DEFAULT_BG;
                    state->current_mode = 0;
                } else if (p == 1) {
                    state->current_mode |= ATTR_BOLD;
                } else if (p == 2) {
                    state->current_mode |= ATTR_FAINT;
                } else if (p == 3) {
                    state->current_mode |= ATTR_ITALIC;
                } else if (p == 4) {
                    state->current_mode |= ATTR_UNDERLINE;
                } else if (p == 5 || p == 6) {
                    state->current_mode |= ATTR_BLINK;
                } else if (p == 7) {
                    state->current_mode |= ATTR_REVERSE;
                } else if (p == 8) {
                    state->current_mode |= ATTR_INVISIBLE;
                } else if (p == 9) {
                    state->current_mode |= ATTR_STRUCK;
                } else if (p == 22) {
                    state->current_mode &= ~(ATTR_BOLD | ATTR_FAINT);
                } else if (p == 23) {
                    state->current_mode &= ~ATTR_ITALIC;
                } else if (p == 24) {
                    state->current_mode &= ~ATTR_UNDERLINE;
                } else if (p == 25) {
                    state->current_mode &= ~ATTR_BLINK;
                } else if (p == 27) {
                    state->current_mode &= ~ATTR_REVERSE;
                } else if (p == 28) {
                    state->current_mode &= ~ATTR_INVISIBLE;
                } else if (p == 29) {
                    state->current_mode &= ~ATTR_STRUCK;
                } else if (p >= 30 && p <= 37) {
                    state->current_fg = (uint32_t)(p - 30);
                } else if (p >= 90 && p <= 97) {
                    state->current_fg = (uint32_t)(p - 90 + 8);
                } else if (p == 39) {
                    state->current_fg = COLOR_DEFAULT_FG;
                } else if (p >= 40 && p <= 47) {
                    state->current_bg = (uint32_t)(p - 40);
                } else if (p >= 100 && p <= 107) {
                    state->current_bg = (uint32_t)(p - 100 + 8);
                } else if (p == 49) {
                    state->current_bg = COLOR_DEFAULT_BG;
                } else if (p == 38 || p == 48) {
                    if (i + 2 < param_count && param_values[i + 1] == 5) {
                        int n = param_values[i + 2];
                        if (n < 0) n = 0;
                        if (n > 255) n = 255;
                        if (p == 38) {
                            state->current_fg = (uint32_t)n;
                        } else {
                            state->current_bg = (uint32_t)n;
                        }
                        i += 2;
                    } else if (i + 4 < param_count && param_values[i + 1] == 2) {
                        /* 38;2;r;g;b or 48;2;r;g;b */
                        int r = param_values[i + 2], g = param_values[i + 3], b = param_values[i + 4];
                        if (r < 0) r = 0; else if (r > 255) r = 255;
                        if (g < 0) g = 0; else if (g > 255) g = 255;
                        if (b < 0) b = 0; else if (b > 255) b = 255;
                        uint32_t rgb = TRUECOLOR(r, g, b);
                        if (p == 38) {
                            state->current_fg = rgb;
                        } else {
                            state->current_bg = rgb;
                        }
                        i += 4;
                    }
                }
            }
        } break;

        case '@': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int from;
            int shift;

            cancel_pending_wrap(state);
            if (n <= 0 || state->row < 0 || state->row >= term_rows) break;
            from = state->col;
            if (from < 0) from = 0;
            shift = (from + n < term_cols) ? n : (term_cols - from);
            if (shift <= 0) break;
            shift_line_right(&term_lines[state->row], from, shift, state);
            mark_row_dirty(state->row);
        } break;

        case 'P': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int from;
            int shift;

            cancel_pending_wrap(state);
            if (n <= 0 || state->row < 0 || state->row >= term_rows) break;
            from = state->col;
            if (from < 0) from = 0;
            shift = (from + n < term_cols) ? n : (term_cols - from);
            if (shift <= 0) break;
            shift_line_left(&term_lines[state->row], from, shift, state);
            mark_row_dirty(state->row);
        } break;

        case 'L': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int top;
            int bottom;
            int r;
            int max_insert;

            cancel_pending_wrap(state);
            top = scroll_region_top(state);
            bottom = scroll_region_bottom(state);
            if (state->row < top || state->row > bottom || n <= 0) break;
            r = state->row;
            max_insert = bottom - r + 1;
            if (n > max_insert) n = max_insert;
            /*
             * CSI L: insert n blank lines at r, shifting r..bottom-n down.
             * Swap full TermLine structs so combs move with their rows.
             * The n rows at bottom-n+1..bottom scroll off — their combs
             * are freed; the line buffers are reused as the new blank rows
             * at r..r+n-1.
             *
             * Strategy: rotate the range [r..bottom] downward by n slots
             * using a temporary array of size n for the displaced rows.
             */
            {
                /* Save the n rows that will scroll off (bottom-n+1..bottom). */
                TermLine *displaced = malloc((size_t)n * sizeof(TermLine));
                if (displaced) {
                    for (int i = 0; i < n; i++)
                        displaced[i] = term_lines[bottom - n + 1 + i];
                    /* Shift rows [r .. bottom-n] down by n. */
                    for (int row = bottom; row >= r + n; row--)
                        term_lines[row] = term_lines[row - n];
                    /* Put saved rows at r..r+n-1 as the new blank lines. */
                    for (int i = 0; i < n; i++) {
                        int row = r + i;
                        term_lines[row] = displaced[i];
                        free_row_combs(row);
                        for (int c = 0; c < term_cols; c++)
                            init_default_cell(&term_lines[row].line[c]);
                        term_lines[row].dirty = 1;
                    }
                    free(displaced);
                } else {
                    /* OOM fallback: memcpy-only (combs may desync) */
                    for (int row = bottom; row >= r + n; row--)
                        memcpy(term_lines[row].line, term_lines[row - n].line,
                               (size_t)term_cols * sizeof(Glyph));
                    for (int row = r; row < r + n && row <= bottom; row++)
                        clear_row_range(row, 0, term_cols - 1, state);
                }
            }
            mark_rows_dirty(r, bottom);
        } break;

        case 'M': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int top;
            int bottom;
            int r;
            int max_del;

            cancel_pending_wrap(state);
            top = scroll_region_top(state);
            bottom = scroll_region_bottom(state);
            if (state->row < top || state->row > bottom || n <= 0) break;
            r = state->row;
            max_del = bottom - r + 1;
            if (n > max_del) n = max_del;
            /*
             * CSI M: delete n lines at r, shifting r+n..bottom up by n.
             * Swap full TermLine structs so combs move with their rows.
             * The n rows at r..r+n-1 scroll off — their combs are freed;
             * the line buffers are reused as the new blank rows at
             * bottom-n+1..bottom.
             *
             * Strategy: save deleted rows, shift remaining rows up, put
             * saved rows at bottom-n+1..bottom as blank lines.
             */
            {
                TermLine *displaced = malloc((size_t)n * sizeof(TermLine));
                if (displaced) {
                    for (int i = 0; i < n; i++)
                        displaced[i] = term_lines[r + i];
                    /* Shift rows [r+n .. bottom] up by n. */
                    for (int row = r; row <= bottom - n; row++)
                        term_lines[row] = term_lines[row + n];
                    /* Put saved rows at bottom-n+1..bottom as blank lines. */
                    for (int i = 0; i < n; i++) {
                        int row = bottom - n + 1 + i;
                        term_lines[row] = displaced[i];
                        free_row_combs(row);
                        for (int c = 0; c < term_cols; c++)
                            init_default_cell(&term_lines[row].line[c]);
                        term_lines[row].dirty = 1;
                    }
                    free(displaced);
                } else {
                    /* OOM fallback: memcpy-only (combs may desync) */
                    for (int row = r; row <= bottom - n; row++)
                        memcpy(term_lines[row].line, term_lines[row + n].line,
                               (size_t)term_cols * sizeof(Glyph));
                    for (int row = bottom - n + 1; row <= bottom; row++)
                        clear_row_range(row, 0, term_cols - 1, state);
                }
            }
            mark_rows_dirty(r, bottom);
        } break;

        case 'X': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            int from;
            int to;

            cancel_pending_wrap(state);
            if (n <= 0 || state->row < 0 || state->row >= term_rows) break;
            from = state->col;
            if (from < 0) from = 0;
            to = from + n;
            if (to > term_cols) to = term_cols;
            clear_row_range(state->row, from, to - 1, state);
        } break;

        case 'b': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            utf8proc_int32_t codepoint;
            ssize_t parsed;

            if (n <= 0 || n > 65535) break;
            if (state->lastc[0] == '\0') break;

            parsed = utf8proc_iterate((const utf8proc_uint8_t *)state->lastc, -1, &codepoint);
            if (parsed <= 0) break;

            for (int i = 0; i < n; i++) {
                put_codepoint(codepoint, state);
            }
        } break;

        case 'S': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            scroll_up_n_lines(state, n);
        } break;

        case 'T': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
            scroll_down_n_lines(state, n);
        } break;

        default:
            break;
    }
}

void tputc(char c, Term *state) {
    if (!state || !term_lines || term_rows <= 0 || term_cols <= 0) {
        return;
    }
    if (state->row < 0) state->row = 0;
    if (state->row >= term_rows) state->row = term_rows - 1;
    if (state->col < 0) state->col = 0;
    if (state->col > term_cols) {
        state->col = term_cols;
    }
    if (state->col >= term_cols && !(state->wrap_next && state->col == term_cols && state->autowrap_mode)) {
        state->col = term_cols - 1;
    }

    if (c == '\b') {
        state->utf8_len = 0;
        if (state->wrap_next) {
            state->wrap_next = 0;
            state->col = term_cols - 1;
        } else if (state->col > 0) {
            state->col--;
        }
        return;
    }

    if ((unsigned char)c == 0x7F) {
        state->utf8_len = 0;
        return;
    }

    if (c == '\t') {
        state->utf8_len = 0;
        if (state->wrap_next) {
            /* Stay on virtual margin; next printable overwrites last column (xterm-style). */
            state->col = term_cols;
            state->wrap_overwrite_next = 1;
            return;
        }
        state->col = next_tab_stop_col(state->col);
        return;
    }

    if (c == '\a') {
        state->utf8_len = 0;
        xbell();
        return;
    }

    if (c == '\n' || c == '\v' || c == '\f') {
        state->utf8_len = 0;
        cancel_pending_wrap(state);
        advance_row_with_scroll(state);
        return;
    }

    if (c == '\r') {
        state->utf8_len = 0;
        cancel_pending_wrap(state);
        state->col = 0;
        return;
    }

    if ((unsigned char)c < 0x20) {
        state->utf8_len = 0;
        return;
    }

    if (state->utf8_len >= (int)sizeof(state->utf8_buf)) {
        state->utf8_len = 0;
    }
    state->utf8_buf[state->utf8_len++] = (uint8_t)c;

    {
        int expected = utf8_expected_len(state->utf8_buf[0]);
        if (expected == 0) {
            state->utf8_len = 0;
            return;
        }
        if (state->utf8_len > expected) {
            state->utf8_len = 0;
            return;
        }
        for (int i = 1; i < state->utf8_len; i++) {
            if ((state->utf8_buf[i] & 0xC0) != 0x80) {
                state->utf8_len = 0;
                return;
            }
        }
        if (state->utf8_len < expected) {
            return;
        }
    }

    {
        utf8proc_int32_t codepoint;
        ssize_t result = utf8proc_iterate(state->utf8_buf, state->utf8_len, &codepoint);

        if (result > 0) {
            /* DEC Special Graphics (ACS): translate when active GL charset is DEC Special Graphics */
            {
                int charsets[] = {
                    state->charset_g0,
                    state->charset_g1,
                    state->charset_g2,
                    state->charset_g3
                };
                int acs = charsets[(state->gl >= 0 && state->gl < 4) ? state->gl : 0];
                if (acs && codepoint >= 0x41 && codepoint <= 0x7E) {
                    const char *repl = vt100_acs[codepoint - 0x41];
                    if (repl) {
                        size_t rlen = strlen(repl);
                        if (rlen > 0 && rlen < (size_t)sizeof(state->utf8_buf)) {
                            memcpy(state->utf8_buf, repl, rlen + 1);
                            state->utf8_len = (int)rlen;
                            if (utf8proc_iterate(state->utf8_buf,
                                    state->utf8_len, &codepoint) <= 0)
                                codepoint = '?';
                        }
                    }
                }
            }

            int width = wcwidth((wchar_t)codepoint);
            int row;
            int col;

            if (width < 0 && codepoint >= 0x80) {
                int fallback_width = utf8proc_charwidth(codepoint);
                if (fallback_width >= 0) {
                    width = fallback_width;
                }
            }
            if (width < 0) {
                width = 1;
            }
            if (width > 2) {
                width = 1;
            }

            if (width == 0) {
                int target_col;

                if (state->wrap_next) {
                    target_col = term_cols - 1;
                } else {
                    target_col = state->col - 1;
                }

                (void)append_combining_mark(state->row, target_col, state->utf8_buf, state->utf8_len);
                state->utf8_len = 0;
                return;
            }

            if (state->wrap_next) {
                if (state->autowrap_mode) {
                    if (state->col == term_cols && state->wrap_overwrite_next) {
                        state->wrap_overwrite_next = 0;
                        state->wrap_next = 0;
                        state->col = term_cols - 1;
                    } else {
                        wrap_to_next_line(state);
                    }
                } else {
                    state->wrap_next = 0;
                    state->wrap_overwrite_next = 0;
                    if (state->col >= term_cols) {
                        state->col = term_cols - 1;
                    }
                }
            }

            if (width == 2 && term_cols == 1) {
                width = 1;
            }
            if (width == 2 && state->col >= term_cols - 1) {
                if (state->autowrap_mode) {
                    wrap_to_next_line(state);
                } else {
                    state->col = term_cols - width;
                }
            }

            row = state->row;
            col = state->col;
            if (row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
                state->utf8_len = 0;
                return;
            }

            if (state->insert_mode) {
                int shift = width;
                shift_line_right(&term_lines[row], col, shift, state);
            }

            normalize_cell_for_write(row, col, state);
            {
                utf8proc_int32_t decoded_cp = 0;
                utf8proc_ssize_t decoded_r = utf8proc_iterate(
                    (const utf8proc_uint8_t *)state->utf8_buf,
                    (utf8proc_ssize_t)state->utf8_len, &decoded_cp);
                term_lines[row].line[col].u = (decoded_r > 0 && decoded_cp >= 0)
                    ? (Rune)decoded_cp : 0;
            }
            term_lines[row].line[col].fg = state->current_fg;
            term_lines[row].line[col].bg = state->current_bg;
            term_lines[row].line[col].mode = state->current_mode & ~(ATTR_WIDE | ATTR_WDUMMY);
            if (width == 2)
                term_lines[row].line[col].mode |= ATTR_WIDE;

            mark_row_dirty(row);

            memcpy(state->lastc, state->utf8_buf, (size_t)state->utf8_len);
            state->lastc[state->utf8_len] = '\0';

            if (width == 2 && col + 1 < term_cols) {
                normalize_cell_for_write(row, col + 1, state);
                clear_cell_at(&term_lines[row], col + 1, state);
                term_lines[row].line[col + 1].mode = (term_lines[row].line[col + 1].mode & ~(ATTR_WIDE | ATTR_WDUMMY)) | ATTR_WDUMMY;
            }

            if (col + width < term_cols) {
                state->col = col + width;
                state->wrap_next = 0;
            } else {
                state->col = state->autowrap_mode ? term_cols : term_cols - 1;
                state->wrap_next = state->autowrap_mode ? 1 : 0;
            }
            state->utf8_len = 0;
        } else {
            state->utf8_len = 0;
        }
    }
}

#define CSI_PENDING_MAX 1024
/* Must be at least CSI_PENDING_MAX + BUF_SIZE (65536) so that a pending
 * partial CSI never causes bytes from the subsequent read to be silently
 * dropped when the two are merged before parsing. */
#define COMBINED_MAX (CSI_PENDING_MAX + 65536 + 16)

void twrite(const uint8_t *bytes, size_t len, Term *state,
    terminal_response_fn response_fn, void *response_ctx) {
    size_t pending_before;

    if (!bytes || !state) {
        return;
    }

    /* Prepend any partial CSI from previous read */
    uint8_t combined_buf[COMBINED_MAX];
    const uint8_t *buf = bytes;
    size_t buflen = len;
    pending_before = (size_t)state->csi_pending_len;
    if (state->csi_pending_len > 0) {
        size_t total = (size_t)state->csi_pending_len + len;
        if (total > COMBINED_MAX) total = COMBINED_MAX;
        memcpy(combined_buf, state->csi_pending, (size_t)state->csi_pending_len);
        memcpy(combined_buf + state->csi_pending_len, bytes, total - (size_t)state->csi_pending_len);
        buf = combined_buf;
        buflen = total;
        state->csi_pending_len = 0;
    }
    if (buflen == 0) return;
    printer_mirror_input(buf, buflen, state->print_mode,
                         state->print_mode ? pending_before : 0);

    size_t i = 0;
    while (i < buflen) {
        if (state->str_ignore_active) {
            uint8_t b = buf[i++];

            if (state->str_ignore_esc_pending) {
                if (b == '\\') {
                    state->str_ignore_active = 0;
                    state->str_ignore_esc_pending = 0;
                    continue;
                }
                state->str_ignore_esc_pending = 0;
            }

            if (b == 0x9C || b == 0x18 || b == 0x1A) {
                state->str_ignore_active = 0;
                state->str_ignore_esc_pending = 0;
                continue;
            }
            if (b == 0x1B) {
                state->str_ignore_esc_pending = 1;
            }
            continue;
        }

        if (state->osc_active) {
            uint8_t b = buf[i++];

            if (state->osc_esc_pending) {
                if (b == '\\') {
                    osc_finalize(state);
                    continue;
                }
                osc_append_byte(state, 0x1B);
                state->osc_esc_pending = 0;
            }

            if (b == 0x07 || b == 0x9C || b == 0x18 || b == 0x1A) {
                osc_finalize(state);
                continue;
            }
            if (b == 0x1B) {
                state->osc_esc_pending = 1;
                continue;
            }

            osc_append_byte(state, b);
            continue;
        }

        if (buf[i] == 0x1B) {
            size_t start = i;
            i++;
            state->utf8_len = 0;

            if (i >= buflen) {
                size_t tail = buflen - start;
                if (tail > 0 && tail <= CSI_PENDING_MAX) {
                    memcpy(state->csi_pending, buf + start, tail);
                    state->csi_pending_len = (int)tail;
                }
                break;
            }

            if (buf[i] == '7') {
                save_cursor_state(state);
                i++;
                continue;
            }
            if (buf[i] == '8') {
                restore_cursor_state(state);
                i++;
                continue;
            }
            if (buf[i] == 'D') {
                cancel_pending_wrap(state);
                advance_row_with_scroll(state);
                i++;
                continue;
            }
            if (buf[i] == 'E') {
                cancel_pending_wrap(state);
                advance_row_with_scroll(state);
                state->col = 0;
                i++;
                continue;
            }
            if (buf[i] == 'M') {
                reverse_index(state);
                i++;
                continue;
            }
            if (buf[i] == 'H') {
                if (tab_stops && state->col > 0 && state->col < term_cols) {
                    tab_stops[state->col] = 1;
                }
                i++;
                continue;
            }
            if (buf[i] == 'c') {
                terminal_soft_reset(state);
                i++;
                continue;
            }

            /* ESC % G → switch to UTF-8 mode; ESC % @ → switch to legacy mode */
            if (buf[i] == '%' && i + 1 < buflen) {
                uint8_t x = buf[i + 1];
                if (x == 'G') state->utf8_mode = 1;
                else if (x == '@') state->utf8_mode = 0;
                i += 2;
                continue;
            }
            if (buf[i] == '%' && i + 1 >= buflen) {
                /* Incomplete sequence; save pending */
                size_t tail = buflen - start;
                if (tail > 0 && tail <= CSI_PENDING_MAX) {
                    memcpy(state->csi_pending, buf + start, tail);
                    state->csi_pending_len = (int)tail;
                }
                break;
            }

            /* ESC followed by (, ), *, or + designates G0 through G3. */
            if ((buf[i] == '(' || buf[i] == ')' || buf[i] == '*' || buf[i] == '+') &&
                i + 1 < buflen) {
                int g = (int)buf[i] - (int)'(';
                uint8_t x = buf[i + 1];
                int value = (x == '0') ? 1 : 0;
                if (x == '0') {
                    if (g == 0) state->charset_g0 = value;
                    else if (g == 1) state->charset_g1 = value;
                    else if (g == 2) state->charset_g2 = value;
                    else state->charset_g3 = value;
                } else if (x == 'B') {
                    if (g == 0) state->charset_g0 = value;
                    else if (g == 1) state->charset_g1 = value;
                    else if (g == 2) state->charset_g2 = value;
                    else state->charset_g3 = value;
                }
                i += 2;
                continue;
            }
            if (buf[i] == 'n' || buf[i] == 'o') {
                state->gl = 2 + (buf[i] - 'n');
                i++;
                continue;
            }
            if ((buf[i] == '(' || buf[i] == ')' || buf[i] == '*' || buf[i] == '+') &&
                i + 1 >= buflen) {
                size_t tail = buflen - start;
                if (tail > 0 && tail <= CSI_PENDING_MAX) {
                    memcpy(state->csi_pending, buf + start, tail);
                    state->csi_pending_len = (int)tail;
                }
                break;
            }

            if (buf[i] == '[') {
                size_t q;

                i++;
                q = i;
                while (q < buflen && !(buf[q] >= '@' && buf[q] <= '~')) {
                    q++;
                }

                if (q < buflen) {
                    int seq_len = (int)(q - start + 1);
                    csihandle((const char *)&buf[start], seq_len, state, response_fn, response_ctx);
                    i = q + 1;
                    continue;
                }

                /* Partial CSI: save for next read */
                size_t tail = buflen - start;
                if (tail > 0 && tail <= CSI_PENDING_MAX) {
                    memcpy(state->csi_pending, buf + start, tail);
                    state->csi_pending_len = (int)tail;
                }
                break;
            }

            if (buf[i] == ']') {
                i++;
                state->osc_active = 1;
                state->osc_esc_pending = 0;
                state->osc_overflow = 0;
                state->osc_len = 0;
                continue;
            }

            if (buf[i] == 'P' || buf[i] == '_' || buf[i] == '^') {
                i++;
                state->str_ignore_active = 1;
                state->str_ignore_esc_pending = 0;
                continue;
            }

            /* DECKPAM (ESC =) and DECKPNM (ESC >) – keypad mode switches.
             * Must be consumed here; without this the '=' or '>' byte falls
             * through to tputc() and is printed literally in the prompt. */
            if (buf[i] == '=' || buf[i] == '>') {
                state->application_keypad = (buf[i] == '=');
                i++;
                continue;
            }

            if (buf[i] == '#' && i + 1 < buflen) {
                if (buf[i + 1] == '8') {
                    for (int row = 0; row < term_rows; row++) {
                        for (int col = 0; col < term_cols; col++) {
                            clear_cell_at(&term_lines[row], col, state);
                            term_lines[row].line[col].u = 'E';
                            term_lines[row].line[col].fg = state->current_fg;
                            term_lines[row].line[col].bg = state->current_bg;
                            term_lines[row].line[col].mode = state->current_mode;
                        }
                        mark_row_dirty(row);
                    }
                }
                i += 2;
                continue;
            }
            if (buf[i] == '#' && i + 1 >= buflen) {
                size_t tail = buflen - start;
                if (tail > 0 && tail <= CSI_PENDING_MAX) {
                    memcpy(state->csi_pending, buf + start, tail);
                    state->csi_pending_len = (int)tail;
                }
                break;
            }

            /* Unknown ESC X: consume the second byte as a no-op so it is not
             * fed to tputc() as a printable character. */
            i++;
            continue;
        }

        {
            uint8_t b = buf[i++];
            if (state->utf8_len == 0 && b == 0x84) {
                cancel_pending_wrap(state);
                advance_row_with_scroll(state);
                continue;
            }
            if (b == 0x0E) {  /* SO: invoke G1 into GL */
                state->gl = 1;
                state->utf8_len = 0;
                continue;
            }
            if (b == 0x0F) {  /* SI: invoke G0 into GL */
                state->gl = 0;
                state->utf8_len = 0;
                continue;
            }
            if (b == 0x18) {
                state->utf8_len = 0;
                state->csi_pending_len = 0;
                continue;
            }
            if (b == 0x1A) {
                state->utf8_len = 0;
                state->csi_pending_len = 0;
                continue;
            }
            if (state->utf8_len == 0 && b == 0x85) {
                state->utf8_len = 0;
                cancel_pending_wrap(state);
                state->col = 0;
                advance_row_with_scroll(state);
                continue;
            }
            if (state->utf8_len == 0 && b == 0x88) {
                state->utf8_len = 0;
                if (tab_stops && state->col >= 0 && state->col < term_cols) {
                    tab_stops[state->col] = 1;
                }
                continue;
            }
            if (state->utf8_len == 0 && b == 0x8D) {
                state->utf8_len = 0;
                reverse_index(state);
                continue;
            }
            if (state->utf8_len == 0 && b == 0x90) {
                state->utf8_len = 0;
                state->str_ignore_active = 1;
                state->str_ignore_esc_pending = 0;
                continue;
            }
            if (state->utf8_len == 0 && b == 0x9B) {
                size_t start = i - 1;
                size_t q = i;

                while (q < buflen && !(buf[q] >= '@' && buf[q] <= '~')) {
                    q++;
                }
                if (q < buflen) {
                    size_t seq_len = q - start + 1;
                    char seq_buf[CSI_PENDING_MAX + 4];

                    if (seq_len > CSI_PENDING_MAX) {
                        seq_len = CSI_PENDING_MAX;
                    }
                    seq_buf[0] = '\033';
                    seq_buf[1] = '[';
                    if (seq_len > 1) {
                        memcpy(seq_buf + 2, buf + start + 1, seq_len - 1);
                    }
                    csihandle(seq_buf, (int)(seq_len + 1), state, response_fn, response_ctx);
                    i = q + 1;
                    continue;
                }

                {
                    size_t tail = buflen - start;
                    if (tail > 0 && tail <= CSI_PENDING_MAX) {
                        memcpy(state->csi_pending, buf + start, tail);
                        state->csi_pending_len = (int)tail;
                    }
                }
                break;
            }
            if (state->utf8_len == 0 && b == 0x9C) {
                state->utf8_len = 0;
                if (state->osc_active) {
                    osc_finalize(state);
                }
                state->str_ignore_active = 0;
                state->str_ignore_esc_pending = 0;
                continue;
            }
            if (state->utf8_len == 0 && b == 0x9D) {
                state->utf8_len = 0;
                state->osc_active = 1;
                state->osc_esc_pending = 0;
                state->osc_len = 0;
                continue;
            }
            if (state->utf8_len == 0 && (b == 0x9E || b == 0x9F)) {
                state->utf8_len = 0;
                state->str_ignore_active = 1;
                state->str_ignore_esc_pending = 0;
                continue;
            }
            if (state->utf8_len == 0 && b == 0x9A && response_fn && vtiden) {
                state->utf8_len = 0;
                response_fn((const uint8_t *)vtiden, strlen(vtiden), response_ctx);
                continue;
            }
            tputc((char)b, state);
        }
    }
}

size_t tpastefmt(const uint8_t *input, size_t input_len, int bracketed_mode,
    uint8_t *output, size_t output_cap) {
    static const uint8_t prefix[] = "\033[200~";
    static const uint8_t suffix[] = "\033[201~";
    size_t offset = 0;

    if (!output || output_cap == 0 || (!input && input_len > 0)) {
        return 0;
    }

    if (bracketed_mode) {
        if (sizeof(prefix) - 1 > output_cap) {
            return 0;
        }
        memcpy(output + offset, prefix, sizeof(prefix) - 1);
        offset += sizeof(prefix) - 1;
    }

    if (input_len > 0) {
        if (offset + input_len > output_cap) {
            return 0;
        }
        memcpy(output + offset, input, input_len);
        offset += input_len;
    }

    if (bracketed_mode) {
        if (offset + (sizeof(suffix) - 1) > output_cap) {
            return 0;
        }
        memcpy(output + offset, suffix, sizeof(suffix) - 1);
        offset += sizeof(suffix) - 1;
    }

    return offset;
}

/* ======================================================================== */
/* Selection API — all selection state lives in Term; x.c calls these.      */
/* ======================================================================== */

static int visual_line_length(int row) {
    const Glyph *line = tgetline(row);
    int len = term_cols;

    if (!line || term_cols <= 0)
        return 0;
    if (line[term_cols - 1].mode & ATTR_WRAP)
        return term_cols;
    while (len > 0 && (line[len - 1].u == 0 || line[len - 1].u == ' '))
        len--;
    return len;
}

/* Returns 1 if (row,col) cluster text is a word-delimiter. */
static int sel_cell_is_delim(int row, int col) {
    char cluster[MAX_CLUSTER_UTF8];
    utf8proc_int32_t cp;
    utf8proc_ssize_t n;
    size_t clen;
    if (row < 0 || row >= term_rows || col < 0 || col >= term_cols) return 1;
    clen = term_render_visual_cluster(row, col, cluster, sizeof cluster);
    if (clen == 0) return 1;
    n = utf8proc_iterate((const utf8proc_uint8_t *)cluster, (utf8proc_ssize_t)clen, &cp);
    if (n <= 0) return 1;
    return wcschr(worddelimiters, (wchar_t)cp) != NULL;
}

static void selsnap_st(int *x, int *y, int direction) {
    int newx, newy, prevdelim, delim;
    const Glyph *line;
    Rune previous;

    switch (term.sel_snap) {
    case SNAP_WORD:
        line = tgetline(*y);
        if (!line) break;
        previous = line[*x].u;
        prevdelim = sel_cell_is_delim(*y, *x);
        for (;;) {
            newx = *x + direction; newy = *y;
            if (newx < 0 || newx >= term_cols) {
                newy += direction;
                newx = (newx + term_cols) % term_cols;
                if (newy < 0 || newy >= term_rows) break;
                int wrap_row = direction > 0 ? *y : newy;
                line = tgetline(wrap_row);
                if (!line || !(line[term_cols - 1].mode & ATTR_WRAP))
                    break;
            }
            if (newx >= visual_line_length(newy))
                break;
            line = tgetline(newy);
            if (!line) break;
            delim = sel_cell_is_delim(newy, newx);
            if (!(line[newx].mode & ATTR_WDUMMY) &&
                (delim != prevdelim ||
                 (delim && line[newx].u != previous)))
                break;
            *x = newx;
            *y = newy;
            previous = line[newx].u;
            prevdelim = delim;
        }
        break;
    case SNAP_LINE:
        *x = (direction < 0) ? 0 : (term_cols - 1);
        if (direction < 0) {
            while (*y > 0) {
                line = tgetline(*y - 1);
                if (!line || !(line[term_cols - 1].mode & ATTR_WRAP))
                    break;
                (*y)--;
            }
        } else {
            while (*y < term_rows - 1) {
                line = tgetline(*y);
                if (!line || !(line[term_cols - 1].mode & ATTR_WRAP))
                    break;
                (*y)++;
            }
        }
        break;
    }
}

/* Private helper: compute ordered start/end of current selection.
   Returns 0 if no active selection, 1 on success. */
static int selection_bounds(int *sr, int *sc, int *er, int *ec) {
    int ar, ac, br, bc;
    if (!term.sel_active) { *sr = *sc = *er = *ec = 0; return 0; }
    ar = term.sel_anchor_row; ac = term.sel_anchor_col;
    br = term.sel_row;        bc = term.sel_col;

    if (term.sel_type == SEL_RECTANGULAR) {
        *sr = (ar<br)?ar:br; *er = (ar<br)?br:ar;
        *sc = (ac<bc)?ac:bc; *ec = (ac<bc)?bc:ac;
        return 1;
    }
    if (ar > br || (ar == br && ac > bc)) {
        int t; t=ar;ar=br;br=t; t=ac;ac=bc;bc=t;
    }
    *sr=ar; *sc=ac; *er=br; *ec=bc;
    if (term.sel_snap) { selsnap_st(sc,sr,-1); selsnap_st(ec,er,+1); }
    if (term.sel_type != SEL_RECTANGULAR) {
        int len = visual_line_length(*sr);
        if (len < *sc)
            *sc = len;
        if (visual_line_length(*er) <= *ec)
            *ec = term_cols - 1;
    }
    return 1;
}

void selstart(int col, int row, int snap, int type) {
    term.sel_type = type;
    term.sel_snap = snap;
    term.sel_active = 1;
    term.sel_alt_screen = term.alt_screen_active;
    term.sel_anchor_row = term.sel_row = row;
    term.sel_anchor_col = term.sel_col = col;
    tfulldirt();
}

void selextend(int col, int row) {
    if (term.sel_active) {
        term.sel_row = row;
        term.sel_col = col;
        tfulldirt();
    }
}

void selrelease(int col, int row) {
    if (!term.sel_active) return;
    term.sel_row = row;
    term.sel_col = col;
    /* Caller (x.c) handles copy-to-clipboard via getsel(). */
    tfulldirt();
}

void selclear(void) {
    term.sel_active = 0;
    tfulldirt();
}

int selisactive(void) {
    return term.sel_active;
}

int selected(int col, int row) {
    int sr, sc, er, ec;
    if (!selection_bounds(&sr, &sc, &er, &ec)) return 0;
    if (term.sel_alt_screen != term.alt_screen_active) return 0;
    if (row < sr || row > er) return 0;
    if (term.sel_type == SEL_RECTANGULAR) return (col >= sc && col <= ec);
    if (sr == er) return (col >= sc && col <= ec);
    if (row == sr) return (col >= sc);
    if (row == er) return (col <= ec);
    return 1;
}

char *getsel(void) {
    int sr, sc, er, ec;
    size_t row_capacity;
    size_t selected_rows;
    size_t max_size;
    size_t pos = 0;
    char *buf;

    if (!selection_bounds(&sr, &sc, &er, &ec)) return NULL;

    selected_rows = (size_t)(er - sr + 1);
    if ((size_t)term_cols > (SIZE_MAX - 1) / MAX_CLUSTER_UTF8)
        return NULL;
    row_capacity = (size_t)term_cols * MAX_CLUSTER_UTF8 + 1;
    if (selected_rows > (SIZE_MAX - 1) / row_capacity)
        return NULL;
    max_size = selected_rows * row_capacity + 1;
    buf = malloc(max_size);
    if (!buf) return NULL;
    buf[0] = '\0';

    for (int r = sr; r <= er; r++) {
        const Glyph *line = tgetline(r);
        int line_len = visual_line_length(r);
        int cstart = (term.sel_type == SEL_RECTANGULAR) ? sc : ((r == sr) ? sc : 0);
        int lastx = (term.sel_type == SEL_RECTANGULAR) ? ec : ((r == er) ? ec : (term_cols - 1));
        int cend;
        uint16_t last_mode = 0;

        if (!line || line_len == 0) {
            buf[pos++] = '\n';
            continue;
        }
        cend = lastx < line_len - 1 ? lastx : line_len - 1;
        while (cend >= cstart &&
               (line[cend].u == 0 || line[cend].u == ' '))
            cend--;
        if (cend >= 0)
            last_mode = line[cend].mode;

        for (int c = cstart; c <= cend; c++) {
            char cluster[MAX_CLUSTER_UTF8];
            size_t glen;
            if (line[c].mode & ATTR_WDUMMY)
                continue;
            glen = term_render_visual_cluster(r, c, cluster, sizeof cluster);
            if (glen == 0) { glen = 1; cluster[0] = ' '; cluster[1] = '\0'; }
            if (pos + glen >= max_size - 2) break;
            memcpy(&buf[pos], cluster, glen);
            pos += glen;
        }
        if ((r < er || lastx >= line_len) &&
            (!(last_mode & ATTR_WRAP) || term.sel_type == SEL_RECTANGULAR))
            buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

/* ======================================================================== */
/* PTY write — owns echo-mode and lnm-mode expansion                        */
/* ======================================================================== */

void ttywrite(const char *s, size_t n, int may_echo) {
    uint8_t expanded[512];
    const uint8_t *to_write;
    size_t to_len;

    if (!s || n == 0) return;
    kscrollreset();

    if (may_echo && term.echo_mode)
        twrite((const uint8_t *)s, n, &term, NULL, NULL);

    to_write = (const uint8_t *)s;
    to_len   = n;

    if (term.lnm_mode && n <= sizeof(expanded) / 2) {
        size_t out = 0;
        for (size_t i = 0; i < n && out + 2 <= sizeof(expanded); i++) {
            if (((const uint8_t *)s)[i] == '\r') {
                expanded[out++] = '\r';
                expanded[out++] = '\n';
            } else {
                expanded[out++] = ((const uint8_t *)s)[i];
            }
        }
        to_write = expanded;
        to_len   = out;
    }

    if (g_pty_session.master_fd >= 0) {
        if (pty_session_write(&g_pty_session, to_write, to_len) < 0)
            perror("write error on tty");
    }
}

/* ======================================================================== */
/* main() — program entry point (st convention: lives at bottom of st.c)    */
/* ======================================================================== */

#ifndef CUPID_NO_MAIN
int main(int argc, char *argv[]) {
    setlocale(LC_CTYPE, "");

    ARGBEGIN {
    case 'a':
        allowaltscreen = 0;
        break;
    case 'c':
        opt_class = EARGF(usage());
        break;
    case 'e':
        if (argc > 0) --argc, ++argv;
        goto run;
    case 'f':
        opt_font = EARGF(usage());
        break;
    case 'g':
        parse_geometry_str(EARGF(usage()), &cols, &rows);
        break;
    case 'i':
        opt_fixed = 1;
        break;
    case 'o':
        opt_io = EARGF(usage());
        break;
    case 'l':
        opt_line = EARGF(usage());
        break;
    case 'n':
        opt_name = EARGF(usage());
        break;
    case 't':
    case 'T':
        opt_title = EARGF(usage());
        break;
    case 'v':
        fprintf(stderr, "cupidterminal " VERSION "\n");
        exit(0);
    case 'w':
        opt_embed = EARGF(usage());
        break;
    default:
        usage();
    } ARGEND;

run:
    if (argc > 0) opt_cmd = argv;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (!opt_title)
        opt_title = (opt_line || !opt_cmd) ? "cupidterminal" : opt_cmd[0];

    pty_install_signal_handlers();

    xinit((int)cols, (int)rows, &g_pty_session);

    if (tprinter_open(opt_io) == -1)
        return EXIT_FAILURE;

    if (pty_session_spawn(&g_pty_session, opt_line, SHELL, opt_cmd, TERM) == -1)
        return EXIT_FAILURE;

    xsync_pty_winsize(&g_pty_session);
    run();

    tprinter_close();
    return 0;
}
#endif /* !CUPID_NO_MAIN */
