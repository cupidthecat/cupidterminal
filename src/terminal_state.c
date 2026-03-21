#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <wchar.h>

#include <utf8proc.h>

#include "terminal_state.h"
#include "config.h"

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

TerminalState term_state;
TerminalCell **terminal_buffer = NULL;
uint8_t *dirty_rows = NULL;

static TerminalCell **primary_buffer = NULL;
static TerminalCell **alternate_buffer = NULL;
static unsigned char *tab_stops = NULL;

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

static void init_default_cell(TerminalCell *cell) {
    if (!cell) {
        return;
    }

    memset(cell->c, 0, sizeof(cell->c));
    cell->fg = COLOR_DEFAULT_FG;
    cell->bg = COLOR_DEFAULT_BG;
    cell->attrs = 0;
    cell->width = 1;
    cell->is_continuation = 0;
}

static TerminalCell **alloc_buffer(int rows, int cols) {
    TerminalCell **buffer = calloc((size_t)rows, sizeof(TerminalCell *));
    if (!buffer) {
        return NULL;
    }

    for (int r = 0; r < rows; r++) {
        buffer[r] = calloc((size_t)cols, sizeof(TerminalCell));
        if (!buffer[r]) {
            for (int rr = 0; rr < r; rr++) {
                free(buffer[rr]);
            }
            free(buffer);
            return NULL;
        }
        for (int c = 0; c < cols; c++) {
            init_default_cell(&buffer[r][c]);
        }
    }

    return buffer;
}

static void free_buffer(TerminalCell **buffer, int rows) {
    if (!buffer) {
        return;
    }

    for (int r = 0; r < rows; r++) {
        free(buffer[r]);
    }
    free(buffer);
}

static TerminalCell **resize_buffer(TerminalCell **old_buffer, int old_rows, int old_cols, int new_rows, int new_cols) {
    TerminalCell **new_buffer = alloc_buffer(new_rows, new_cols);

    if (!new_buffer) {
        return NULL;
    }

    if (old_buffer) {
        int copy_rows = (old_rows < new_rows) ? old_rows : new_rows;
        int copy_cols = (old_cols < new_cols) ? old_cols : new_cols;
        for (int r = 0; r < copy_rows; r++) {
            memcpy(new_buffer[r], old_buffer[r], (size_t)copy_cols * sizeof(TerminalCell));
        }
        free_buffer(old_buffer, old_rows);
    }

    return new_buffer;
}

static void clear_buffer_defaults(TerminalCell **buffer) {
    if (!buffer) {
        return;
    }

    for (int r = 0; r < term_rows; r++) {
        for (int c = 0; c < term_cols; c++) {
            init_default_cell(&buffer[r][c]);
        }
    }
}

static void clear_cell(TerminalCell *cell, const TerminalState *state) {
    if (!cell || !state) {
        return;
    }

    memset(cell->c, 0, sizeof(cell->c));
    cell->fg = state->current_fg;
    cell->bg = state->current_bg;
    cell->attrs = state->current_attrs;
    cell->width = 1;
    cell->is_continuation = 0;
}

static int scroll_region_top(const TerminalState *state) {
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

static int scroll_region_bottom(const TerminalState *state) {
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

static int cursor_min_row(const TerminalState *state) {
    if (state && state->origin_mode) {
        return scroll_region_top(state);
    }
    return 0;
}

static int cursor_max_row(const TerminalState *state) {
    if (state && state->origin_mode) {
        return scroll_region_bottom(state);
    }
    return term_rows - 1;
}

static void cursor_home(TerminalState *state) {
    if (!state) {
        return;
    }
    state->row = cursor_min_row(state);
    state->col = 0;
}

static void mark_row_dirty(int row) {
    if (dirty_rows && row >= 0 && row < term_rows)
        dirty_rows[row] = 1;
}

static void mark_rows_dirty(int top, int bottom) {
    if (!dirty_rows) return;
    if (top < 0) top = 0;
    if (bottom >= term_rows) bottom = term_rows - 1;
    for (int r = top; r <= bottom; r++)
        dirty_rows[r] = 1;
}

static void mark_all_rows_dirty(void) {
    if (dirty_rows)
        memset(dirty_rows, 1, (size_t)term_rows);
}

static void clear_row_range(int row, int start_col, int end_col, const TerminalState *state) {
    if (!state || !terminal_buffer || row < 0 || row >= term_rows) {
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

    if (start_col > 0 && terminal_buffer[row][start_col].is_continuation) {
        start_col--;
    }
    if (end_col + 1 < term_cols && terminal_buffer[row][end_col].width == 2) {
        end_col++;
    }

    for (int c = start_col; c <= end_col; c++) {
        clear_cell(&terminal_buffer[row][c], state);
    }
    mark_row_dirty(row);
}

static void clear_screen_range(int start_row, int start_col, int end_row, int end_col, const TerminalState *state) {
    if (!state || !terminal_buffer || term_rows <= 0 || term_cols <= 0) {
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
static void selscroll_adjust(TerminalState *state, int orig, int n) {
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

static void scroll_up_one_line(TerminalState *state) {
    int top;
    int bottom;

    if (!state || !terminal_buffer || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);
    if (top > bottom) {
        return;
    }

    selscroll_adjust(state, top, 1);

    for (int r = top + 1; r <= bottom; r++) {
        memcpy(terminal_buffer[r - 1], terminal_buffer[r], (size_t)term_cols * sizeof(TerminalCell));
    }
    clear_row_range(bottom, 0, term_cols - 1, state);
    mark_rows_dirty(top, bottom);
}

static void scroll_down_one_line(TerminalState *state) {
    int top;
    int bottom;

    if (!state || !terminal_buffer || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    top = scroll_region_top(state);
    bottom = scroll_region_bottom(state);
    if (top > bottom) {
        return;
    }

    /* Shift selection down (n = -1 → move rows up by -1 = down by 1) */
    selscroll_adjust(state, top, -1);

    for (int r = bottom - 1; r >= top; r--) {
        memcpy(terminal_buffer[r + 1], terminal_buffer[r], (size_t)term_cols * sizeof(TerminalCell));
    }
    clear_row_range(top, 0, term_cols - 1, state);
    mark_rows_dirty(top, bottom);
}

static void scroll_up_n_lines(TerminalState *state, int n) {
    int top = scroll_region_top(state);
    int bottom = scroll_region_bottom(state);

    if (n <= 0 || top > bottom) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    while (n-- > 0) {
        scroll_up_one_line(state);
    }
}

static void scroll_down_n_lines(TerminalState *state, int n) {
    int top = scroll_region_top(state);
    int bottom = scroll_region_bottom(state);

    if (n <= 0 || top > bottom) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    while (n-- > 0) {
        scroll_down_one_line(state);
    }
}

static void reverse_index(TerminalState *state) {
    int top;
    int bottom;

    if (!state) {
        return;
    }

    if (state->col >= term_cols) {
        state->col = term_cols - 1;
    }
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

static void clamp_cursor(TerminalState *state) {
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
    if (state->col > term_cols) state->col = term_cols;
    if (!state->autowrap_mode && state->col >= term_cols) {
        state->col = term_cols - 1;
    }
}

static void cancel_pending_wrap(TerminalState *state) {
    if (!state) {
        return;
    }
    if (state->col >= term_cols) {
        state->col = term_cols - 1;
    }
}

static void save_cursor_state(TerminalState *state) {
    if (!state) {
        return;
    }

    cancel_pending_wrap(state);
    state->saved_row = state->row;
    state->saved_col = state->col;
    state->saved_fg = state->current_fg;
    state->saved_bg = state->current_bg;
    state->saved_attrs = state->current_attrs;
}

static void restore_cursor_state(TerminalState *state) {
    if (!state) {
        return;
    }

    cancel_pending_wrap(state);
    state->row = state->saved_row;
    state->col = state->saved_col;
    state->current_fg = state->saved_fg;
    state->current_bg = state->saved_bg;
    state->current_attrs = state->saved_attrs;
    clamp_cursor(state);
}

static void activate_alternate_screen(TerminalState *state) {
    if (!state || state->alt_screen_active) {
        return;
    }

    state->alt_saved_row = state->row;
    state->alt_saved_col = state->col;
    state->alt_saved_fg = state->current_fg;
    state->alt_saved_bg = state->current_bg;
    state->alt_saved_attrs = state->current_attrs;
    state->alt_saved_scroll_top = state->scroll_top;
    state->alt_saved_scroll_bottom = state->scroll_bottom;

    if (!alternate_buffer) {
        alternate_buffer = alloc_buffer(term_rows, term_cols);
        if (!alternate_buffer) {
            return;
        }
    }

    clear_buffer_defaults(alternate_buffer);
    terminal_buffer = alternate_buffer;
    state->alt_screen_active = 1;
    mark_all_rows_dirty();
    state->row = 0;
    state->col = 0;
    state->scroll_top = -1;
    state->scroll_bottom = -1;
    state->utf8_len = 0;
}

static void deactivate_alternate_screen(TerminalState *state) {
    if (!state || !state->alt_screen_active) {
        return;
    }

    terminal_buffer = primary_buffer;
    state->alt_screen_active = 0;
    state->row = state->alt_saved_row;
    state->col = state->alt_saved_col;
    state->current_fg = state->alt_saved_fg;
    state->current_bg = state->alt_saved_bg;
    state->current_attrs = state->alt_saved_attrs;
    state->scroll_top = state->alt_saved_scroll_top;
    state->scroll_bottom = state->alt_saved_scroll_bottom;
    state->utf8_len = 0;
    clamp_cursor(state);
    mark_all_rows_dirty();
}

static int parse_csi_params(const char *body, int body_len, int *params, int max_params) {
    int count = 0;
    int value = 0;
    int have_digits = 0;
    int saw_separator = 0;

    if (!body || body_len <= 0 || !params || max_params <= 0) {
        return 0;
    }

    for (int i = 0; i < body_len; i++) {
        char ch = body[i];

        if (ch >= '0' && ch <= '9') {
            if (!have_digits) {
                value = 0;
                have_digits = 1;
            }
            value = (value * 10) + (ch - '0');
            saw_separator = 0;
        } else if (ch == ';') {
            if (count < max_params) {
                params[count++] = have_digits ? value : 0;
            }
            value = 0;
            have_digits = 0;
            saw_separator = 1;
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

static int append_combining_mark(int row, int col, const uint8_t *bytes, int byte_len) {
    TerminalCell *cell;
    size_t cur_len;

    if (!terminal_buffer || !bytes || byte_len <= 0 || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return 0;
    }

    if (terminal_buffer[row][col].is_continuation && col > 0) {
        col--;
    }
    cell = &terminal_buffer[row][col];
    if (cell->c[0] == '\0' || cell->is_continuation) {
        return 0;
    }

    cur_len = strlen(cell->c);
    if (cur_len + (size_t)byte_len > MAX_UTF8_CHAR_SIZE) {
        return 0;
    }

    memcpy(cell->c + cur_len, bytes, (size_t)byte_len);
    cell->c[cur_len + (size_t)byte_len] = '\0';
    mark_row_dirty(row);
    return 1;
}

static void normalize_cell_for_write(int row, int col, const TerminalState *state) {
    if (!state || !terminal_buffer || row < 0 || row >= term_rows || col < 0 || col >= term_cols) {
        return;
    }

    if (terminal_buffer[row][col].is_continuation) {
        if (col > 0 && terminal_buffer[row][col - 1].width == 2) {
            clear_cell(&terminal_buffer[row][col - 1], state);
        }
        clear_cell(&terminal_buffer[row][col], state);
    }

    if (terminal_buffer[row][col].width == 2) {
        if (col + 1 < term_cols && terminal_buffer[row][col + 1].is_continuation) {
            clear_cell(&terminal_buffer[row][col + 1], state);
        }
        clear_cell(&terminal_buffer[row][col], state);
    }
}

static void advance_row_with_scroll(TerminalState *state) {
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

static void wrap_to_next_line(TerminalState *state) {
    if (!state) {
        return;
    }

    /* Mark the last glyph on this line as soft-wrapped (st's ATTR_WRAP) */
    if (terminal_buffer && state->row >= 0 && state->row < term_rows && term_cols > 0) {
        terminal_buffer[state->row][term_cols - 1].attrs |= ATTR_WRAP;
    }

    state->col = 0;
    advance_row_with_scroll(state);
}

static void osc_reset(TerminalState *state) {
    state->osc_active = 0;
    state->osc_esc_pending = 0;
    state->osc_len = 0;
}

static void osc_append_byte(TerminalState *state, uint8_t b) {
    if (!state || state->osc_len >= (int)(sizeof(state->osc_buf) - 1)) {
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
 * Returns COLOR_TRUE_RGB_BASE | (r<<16)|(g<<8)|b, or 0 on failure.
 */
static uint32_t parse_x11_color(const char *spec) {
    unsigned int r, g, b;
    if (!spec || !*spec) return 0;
    if (*spec == '#') {
        size_t n = strlen(spec + 1);
        if (n == 6) {
            if (sscanf(spec + 1, "%02x%02x%02x", &r, &g, &b) == 3)
                return COLOR_TRUE_RGB_BASE | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        } else if (n == 12) {
            unsigned int r4, g4, b4;
            if (sscanf(spec + 1, "%04x%04x%04x", &r4, &g4, &b4) == 3)
                return COLOR_TRUE_RGB_BASE | ((uint32_t)(r4 >> 8) << 16) |
                       ((uint32_t)(g4 >> 8) << 8) | (b4 >> 8);
        }
    } else if (strncmp(spec, "rgb:", 4) == 0) {
        if (sscanf(spec + 4, "%x/%x/%x", &r, &g, &b) == 3) {
            if (r > 0xFF) r >>= 8;
            if (g > 0xFF) g >>= 8;
            if (b > 0xFF) b >>= 8;
            return COLOR_TRUE_RGB_BASE | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    return 0;
}

static void osc_finalize(TerminalState *state) {
    const char *payload;
    const char *semi;
    const char *arg1;
    const char *arg2;
    size_t title_len;
    int cmd;

    if (!state) {
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

static void terminal_soft_reset(TerminalState *state) {
    if (!state) {
        return;
    }

    if (primary_buffer) {
        clear_buffer_defaults(primary_buffer);
    }
    if (alternate_buffer) {
        clear_buffer_defaults(alternate_buffer);
    }

    terminal_buffer = primary_buffer ? primary_buffer : terminal_buffer;
    state->alt_screen_active = 0;

    state->row = 0;
    state->col = 0;
    reset_attributes(state);

    state->saved_row = 0;
    state->saved_col = 0;
    state->saved_fg = COLOR_DEFAULT_FG;
    state->saved_bg = COLOR_DEFAULT_BG;
    state->saved_attrs = 0;

    state->cursor_visible = 1;
    state->scroll_top = -1;
    state->scroll_bottom = -1;
    state->bracketed_paste_mode = 0;
    state->autowrap_mode = 1;
    state->origin_mode = 0;
    state->insert_mode = 0;

    state->mouse_reporting_basic = 0;
    state->mouse_reporting_button = 0;
    state->mouse_reporting_any = 0;
    state->mouse_sgr_mode = 0;
    state->application_cursor_keys = 0;

    state->charset_g0 = 0;
    state->charset_g1 = 0;
    state->gl = 0;

    state->utf8_len = 0;
    state->csi_pending_len = 0;
    state->osc52_len = 0;
    state->osc52_pending = 0;
    osc_reset(state);

    if (tab_stops) {
        init_default_tab_stops(tab_stops, term_cols);
    }
    mark_all_rows_dirty();
}

void resize_terminal(int new_rows, int new_cols) {
    TerminalCell **new_primary;
    TerminalCell **new_alt = NULL;
    unsigned char *new_tabs = NULL;
    int old_rows = term_rows;
    int old_cols = term_cols;

    if (new_rows <= 0) new_rows = 1;
    if (new_cols <= 0) new_cols = 1;

    if (primary_buffer != NULL && new_rows == term_rows && new_cols == term_cols) {
        terminal_buffer = (term_state.alt_screen_active && alternate_buffer) ? alternate_buffer : primary_buffer;
        return;
    }

    new_primary = resize_buffer(primary_buffer, old_rows, old_cols, new_rows, new_cols);
    if (!new_primary) {
        return;
    }

    if (alternate_buffer) {
        new_alt = resize_buffer(alternate_buffer, old_rows, old_cols, new_rows, new_cols);
        if (!new_alt) {
            primary_buffer = new_primary;
            alternate_buffer = NULL;
            term_state.alt_screen_active = 0;
            term_rows = new_rows;
            term_cols = new_cols;
            terminal_buffer = primary_buffer;
            clamp_cursor(&term_state);
            return;
        }
    }

    primary_buffer = new_primary;
    alternate_buffer = new_alt;
    term_rows = new_rows;
    term_cols = new_cols;

    /* Reallocate dirty_rows; mark all rows dirty after resize. */
    free(dirty_rows);
    dirty_rows = calloc((size_t)new_rows, sizeof(uint8_t));
    if (dirty_rows) memset(dirty_rows, 1, (size_t)new_rows);

    new_tabs = calloc((size_t)new_cols, sizeof(unsigned char));
    if (new_tabs) {
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
    } else if (tab_stops) {
        free(tab_stops);
        tab_stops = NULL;
    }

    if (term_state.alt_screen_active) {
        if (!alternate_buffer) {
            alternate_buffer = alloc_buffer(term_rows, term_cols);
        }
        terminal_buffer = alternate_buffer ? alternate_buffer : primary_buffer;
    } else {
        terminal_buffer = primary_buffer;
    }

    term_state.scroll_top = -1;
    term_state.scroll_bottom = -1;

    clamp_cursor(&term_state);
    if (term_state.saved_row < 0) term_state.saved_row = 0;
    if (term_state.saved_row >= term_rows) term_state.saved_row = term_rows - 1;
    if (term_state.saved_col < 0) term_state.saved_col = 0;
    if (term_state.saved_col >= term_cols) term_state.saved_col = term_cols - 1;
    if (term_state.alt_saved_row < 0) term_state.alt_saved_row = 0;
    if (term_state.alt_saved_row >= term_rows) term_state.alt_saved_row = term_rows - 1;
    if (term_state.alt_saved_col < 0) term_state.alt_saved_col = 0;
    if (term_state.alt_saved_col >= term_cols) term_state.alt_saved_col = term_cols - 1;
}

void initialize_terminal_state(TerminalState *state) {
    if (!state) {
        return;
    }

    if (primary_buffer) {
        free_buffer(primary_buffer, term_rows);
        primary_buffer = NULL;
    }
    if (alternate_buffer) {
        free_buffer(alternate_buffer, term_rows);
        alternate_buffer = NULL;
    }
    if (tab_stops) {
        free(tab_stops);
        tab_stops = NULL;
    }
    free(dirty_rows);
    dirty_rows = NULL;
    terminal_buffer = NULL;

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
    strncpy(state->window_title, "cupidterminal", sizeof(state->window_title) - 1);

    resize_terminal(24, 80);
}

void reset_attributes(TerminalState *s) {
    if (!s) {
        return;
    }
    s->current_fg = COLOR_DEFAULT_FG;
    s->current_bg = COLOR_DEFAULT_BG;
    s->current_attrs = 0;
}

void handle_ansi_sequence(const char *seq, int len, TerminalState *state,
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
            if (csi_has_param(param_values, param_count, 47) ||
                csi_has_param(param_values, param_count, 1047) ||
                csi_has_param(param_values, param_count, 1049)) {
                activate_alternate_screen(state);
            }
            if (csi_has_param(param_values, param_count, 1048)) {
                save_cursor_state(state);
            }
            if (csi_has_param(param_values, param_count, 2004)) {
                state->bracketed_paste_mode = 1;
            }
            if (csi_has_param(param_values, param_count, 1000)) {
                state->mouse_reporting_basic = 1;
                state->mouse_reporting_button = 0;
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1002)) {
                state->mouse_reporting_basic = 0;
                state->mouse_reporting_button = 1;
                state->mouse_reporting_any = 0;
            }
            if (csi_has_param(param_values, param_count, 1003)) {
                state->mouse_reporting_basic = 0;
                state->mouse_reporting_button = 0;
                state->mouse_reporting_any = 1;
            }
            if (csi_has_param(param_values, param_count, 1006)) {
                state->mouse_sgr_mode = 1;
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
                if (state->col >= term_cols) {
                    state->col = term_cols - 1;
                }
            }
            if (csi_has_param(param_values, param_count, 25)) {
                state->cursor_visible = 0;
            }
            if (csi_has_param(param_values, param_count, 47) ||
                csi_has_param(param_values, param_count, 1047) ||
                csi_has_param(param_values, param_count, 1049)) {
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
            if (report_col >= term_cols) report_col = term_cols - 1;

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
                state->col = state->autowrap_mode ? term_cols : term_cols - 1;
            }
        } break;

        case 'D': {
            int n = csi_param_default(param_values, param_count, 0, 1);
            cancel_pending_wrap(state);
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
            if (state->col > term_cols) {
                state->col = state->autowrap_mode ? term_cols : term_cols - 1;
            }
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
                    state->current_attrs = 0;
                } else if (p == 1) {
                    state->current_attrs |= ATTR_BOLD;
                } else if (p == 2) {
                    state->current_attrs |= ATTR_FAINT;
                } else if (p == 3) {
                    state->current_attrs |= ATTR_ITALIC;
                } else if (p == 4) {
                    state->current_attrs |= ATTR_UNDERLINE;
                } else if (p == 5 || p == 6) {
                    state->current_attrs |= ATTR_BLINK;
                } else if (p == 7) {
                    state->current_attrs |= ATTR_REVERSE;
                } else if (p == 8) {
                    state->current_attrs |= ATTR_INVISIBLE;
                } else if (p == 9) {
                    state->current_attrs |= ATTR_STRUCK;
                } else if (p == 22) {
                    state->current_attrs &= ~(ATTR_BOLD | ATTR_FAINT);
                } else if (p == 23) {
                    state->current_attrs &= ~ATTR_ITALIC;
                } else if (p == 24) {
                    state->current_attrs &= ~ATTR_UNDERLINE;
                } else if (p == 25) {
                    state->current_attrs &= ~ATTR_BLINK;
                } else if (p == 27) {
                    state->current_attrs &= ~ATTR_REVERSE;
                } else if (p == 28) {
                    state->current_attrs &= ~ATTR_INVISIBLE;
                } else if (p == 29) {
                    state->current_attrs &= ~ATTR_STRUCK;
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
                        uint32_t rgb = COLOR_TRUE_RGB_BASE | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
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
            for (int c = term_cols - 1; c >= from + shift; c--) {
                terminal_buffer[state->row][c] = terminal_buffer[state->row][c - shift];
            }
            for (int c = from; c < from + shift && c < term_cols; c++) {
                clear_cell(&terminal_buffer[state->row][c], state);
            }
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
            for (int c = from; c < term_cols - shift; c++) {
                terminal_buffer[state->row][c] = terminal_buffer[state->row][c + shift];
            }
            for (int c = term_cols - shift; c < term_cols; c++) {
                clear_cell(&terminal_buffer[state->row][c], state);
            }
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
            for (int row = bottom; row >= r + n; row--) {
                memcpy(terminal_buffer[row], terminal_buffer[row - n], (size_t)term_cols * sizeof(TerminalCell));
            }
            for (int row = r; row < r + n && row <= bottom; row++) {
                clear_row_range(row, 0, term_cols - 1, state);
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
            for (int row = r; row <= bottom - n; row++) {
                memcpy(terminal_buffer[row], terminal_buffer[row + n], (size_t)term_cols * sizeof(TerminalCell));
            }
            for (int row = bottom - n + 1; row <= bottom; row++) {
                clear_row_range(row, 0, term_cols - 1, state);
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
            int i;
            int j;

            if (n <= 0 || n > 65535) break;
            if (state->lastc[0] == '\0') break;
            for (i = 0; i < n; i++) {
                for (j = 0; state->lastc[j] != '\0'; j++) {
                    put_char(state->lastc[j], state);
                }
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

void put_char(char c, TerminalState *state) {
    if (!state || !terminal_buffer || term_rows <= 0 || term_cols <= 0) {
        return;
    }

    if (state->row < 0) state->row = 0;
    if (state->row >= term_rows) state->row = term_rows - 1;
    if (state->col < 0) state->col = 0;

    if (c == '\b') {
        state->utf8_len = 0;
        if (state->col >= term_cols) {
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
        if (state->col >= term_cols) {
            state->col = term_cols - 1;
            return;
        }

        state->col = next_tab_stop_col(state->col);
        return;
    }

    if (c == '\a') {
        state->utf8_len = 0;
        state->bell_rung = 1;
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
                int acs = state->gl ? state->charset_g1 : state->charset_g0;
                if (acs && codepoint >= 0x41 && codepoint <= 0x7E) {
                const char *repl = vt100_acs[codepoint - 0x41];
                if (repl) {
                    size_t rlen = strlen(repl);
                    if (rlen > 0 && rlen < (size_t)sizeof(state->utf8_buf)) {
                        memcpy(state->utf8_buf, repl, rlen + 1);
                        state->utf8_len = (int)rlen;
                        result = utf8proc_iterate(state->utf8_buf, state->utf8_len, &codepoint);
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

                if (state->col >= term_cols) {
                    target_col = term_cols - 1;
                } else {
                    target_col = state->col - 1;
                }

                (void)append_combining_mark(state->row, target_col, state->utf8_buf, state->utf8_len);
                state->utf8_len = 0;
                return;
            }

            if (state->col >= term_cols) {
                if (state->autowrap_mode) {
                    wrap_to_next_line(state);
                } else {
                    state->col = term_cols - 1;
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
                if (col + shift < term_cols) {
                    for (int cc = term_cols - 1; cc >= col + shift; cc--) {
                        terminal_buffer[row][cc] = terminal_buffer[row][cc - shift];
                    }
                }
                for (int cc = col; cc < col + shift && cc < term_cols; cc++) {
                    clear_cell(&terminal_buffer[row][cc], state);
                }
            }

            normalize_cell_for_write(row, col, state);
            memcpy(terminal_buffer[row][col].c, state->utf8_buf, (size_t)state->utf8_len);
            terminal_buffer[row][col].c[state->utf8_len] = '\0';
            terminal_buffer[row][col].fg = state->current_fg;
            terminal_buffer[row][col].bg = state->current_bg;
            terminal_buffer[row][col].attrs = state->current_attrs;
            terminal_buffer[row][col].width = (uint8_t)width;
            terminal_buffer[row][col].is_continuation = 0;

            mark_row_dirty(row);

            memcpy(state->lastc, state->utf8_buf, (size_t)state->utf8_len);
            state->lastc[state->utf8_len] = '\0';

            if (width == 2 && col + 1 < term_cols) {
                normalize_cell_for_write(row, col + 1, state);
                clear_cell(&terminal_buffer[row][col + 1], state);
                terminal_buffer[row][col + 1].width = 0;
                terminal_buffer[row][col + 1].is_continuation = 1;
            }

            state->col += width;
            if (!state->autowrap_mode && state->col >= term_cols) {
                state->col = term_cols - 1;
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

void terminal_consume_bytes(const uint8_t *bytes, size_t len, TerminalState *state,
    terminal_response_fn response_fn, void *response_ctx) {
    if (!bytes || !state) {
        return;
    }

    /* Prepend any partial CSI from previous read */
    uint8_t combined_buf[COMBINED_MAX];
    const uint8_t *buf = bytes;
    size_t buflen = len;
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

    size_t i = 0;
    while (i < buflen) {
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

            if (b == 0x07) {
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

            /* ESC ( X or ESC ) X - G0/G1 charset designation (DEC Special Graphics) */
            if ((buf[i] == '(' || buf[i] == ')') && i + 1 < buflen) {
                int g = (buf[i] == '(') ? 0 : 1;
                uint8_t x = buf[i + 1];
                if (x == '0') {
                    if (g) state->charset_g1 = 1; else state->charset_g0 = 1;
                } else if (x == 'B') {
                    if (g) state->charset_g1 = 0; else state->charset_g0 = 0;
                }
                i += 2;
                continue;
            }
            /*
             * ESC n/o are ISO-2022 LS2/LS3 (select G2/G3 into GL).
             * We currently implement only G0/G1, so treat these as no-ops.
             * Forcing GL to G1 here can leak ACS into normal text and corrupt TUIs.
             */
            if (buf[i] == 'n' || buf[i] == 'o') {
                i++;
                continue;
            }
            if ((buf[i] == '(' || buf[i] == ')') && i + 1 >= buflen) {
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
                    handle_ansi_sequence((const char *)&buf[start], seq_len, state, response_fn, response_ctx);
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
                state->osc_len = 0;
                continue;
            }

            /* DECKPAM (ESC =) and DECKPNM (ESC >) – keypad mode switches.
             * Must be consumed here; without this the '=' or '>' byte falls
             * through to put_char() and is printed literally in the prompt. */
            if (buf[i] == '=' || buf[i] == '>') {
                i++;
                continue;
            }

            /* Unknown ESC X: consume the second byte as a no-op so it is not
             * fed to put_char() as a printable character. */
            i++;
            continue;
        }

        {
            uint8_t b = buf[i++];
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
            if (state->utf8_len == 0 && b == 0x9A && response_fn && vtiden) {
                state->utf8_len = 0;
                response_fn((const uint8_t *)vtiden, strlen(vtiden), response_ctx);
                continue;
            }
            put_char((char)b, state);
        }
    }
}

size_t terminal_format_paste_payload(const uint8_t *input, size_t input_len, int bracketed_mode,
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
