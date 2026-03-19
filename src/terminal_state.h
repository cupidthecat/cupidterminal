// terminal_state.h

#ifndef TERMINAL_STATE_H
#define TERMINAL_STATE_H

#include <stddef.h>
#include <stdint.h>

extern int term_rows;
extern int term_cols;
#define MAX_CHARS 4096

#define MAX_UTF8_CHAR_SIZE 32  // UTF-8 bytes stored per cell cluster (base + combining marks)

#define ATTR_BOLD       (1 << 0)
#define ATTR_FAINT      (1 << 1)
#define ATTR_ITALIC     (1 << 2)
#define ATTR_UNDERLINE  (1 << 3)
#define ATTR_REVERSE    (1 << 4)
#define ATTR_BLINK      (1 << 5)
#define ATTR_INVISIBLE  (1 << 6)
#define ATTR_STRUCK     (1 << 7)

#define COLOR_DEFAULT_FG 258
#define COLOR_DEFAULT_BG 259
#define COLOR_TRUE_RGB_BASE 0x01000000u  /* 24-bit: 0x01000000 | (r<<16)|(g<<8)|b */
#define COLOR_IS_TRUE_RGB(c) ((c) >= COLOR_TRUE_RGB_BASE && (c) <= 0x01FFFFFFu)

typedef struct {
    char c[MAX_UTF8_CHAR_SIZE + 1];
    uint32_t fg;  
    uint32_t bg;  
    uint16_t attrs; 
    uint8_t width;           // 1 for normal cells, 2 for wide lead cells
    uint8_t is_continuation; // 1 if this cell is the trailing half of a wide glyph
} TerminalCell;

/* Callback for DSR/DA responses: terminal writes bytes back to host */
typedef void (*terminal_response_fn)(const uint8_t *bytes, size_t len, void *ctx);

typedef struct {
    int row;
    int col;
    uint32_t current_fg;
    uint32_t current_bg;
    uint16_t current_attrs;
    int saved_row; int saved_col;
    uint32_t saved_fg; uint32_t saved_bg; uint16_t saved_attrs;  /* DECSC/DECRC */
    int bell_rung; // 1 if BEL was received since last draw
    int cursor_visible;  /* 0 = hidden (DECRST ?25), 1 = visible (default) */
    int scroll_top;     /* 0-based, inclusive; -1 = use 0 */
    int scroll_bottom;  /* 0-based, inclusive; -1 = use term_rows-1 */
    int alt_screen_active;
    int bracketed_paste_mode;
    int autowrap_mode;      /* DECAWM ?7 */
    int origin_mode;        /* DECOM ?6 */
    int insert_mode;        /* IRM 4 */
    int lnm_mode;          /* LNM 20: LF sends CR+LF */
    int echo_mode;         /* SRM 12: echo input to display */

    /* Mouse reporting: ?1000 basic, ?1002 button event, ?1003 any motion, ?1006 SGR */
    int mouse_reporting_basic;   /* 1000: press/release */
    int mouse_reporting_button;  /* 1002: + motion while pressed */
    int mouse_reporting_any;     /* 1003: + motion always */
    int mouse_sgr_mode;          /* 1006: use <b;x;y;M/m format */
    int application_cursor_keys; /* DECCKM ?1: use SS3 O? for arrows */

    int alt_saved_row;
    int alt_saved_col;
    uint32_t alt_saved_fg;
    uint32_t alt_saved_bg;
    uint16_t alt_saved_attrs;
    int alt_saved_scroll_top;
    int alt_saved_scroll_bottom;

    char window_title[256];
    int title_dirty;

    uint8_t utf8_buf[4];
    int utf8_len;

    int osc_active;
    int osc_esc_pending;
    char osc_buf[512];
    int osc_len;
    uint8_t osc52_buf[8192];
    size_t osc52_len;
    int osc52_pending;

    /* Partial CSI across reads (e.g. 38;2;204;204;204m split) */
    uint8_t csi_pending[1024];
    int csi_pending_len;

    // Selection tracking
    int sel_active;
    int sel_anchor_row, sel_anchor_col; // where drag started
    int sel_row, sel_col;               // current drag end
    int sel_type;   /* 0=SEL_REGULAR, 1=SEL_RECTANGULAR */
    int sel_snap;   /* 0=none, 1=SNAP_WORD, 2=SNAP_LINE */

    /* Last printed character for REP (CSI b) */
    char lastc[MAX_UTF8_CHAR_SIZE + 1];
    /* Cursor shape: 0-2 block, 3-4 underline, 5-6 bar, 7 snowman (DECSCUSR) */
    int cursorshape;

    /* G0/G1 charset: 0=USA/ASCII, 1=DEC Special Graphics (box drawing) */
    int charset_g0;
    int charset_g1;
    int gl;  /* 0=G0 in GL, 1=G1 in GL (SO/SI) */
} TerminalState;


// Declare the global TerminalState variable
extern TerminalState term_state;

// Declare the terminal buffer with attributes
extern TerminalCell **terminal_buffer;

// Function prototypes
void resize_terminal(int new_rows, int new_cols);
void initialize_terminal_state(TerminalState *state);
void reset_attributes(TerminalState *state);
void terminal_consume_bytes(const uint8_t *bytes, size_t len, TerminalState *state,
    terminal_response_fn response_fn, void *response_ctx);
void handle_ansi_sequence(const char *seq, int len, TerminalState *state,
    terminal_response_fn response_fn, void *response_ctx);
void put_char(char c, TerminalState *state);
size_t terminal_format_paste_payload(const uint8_t *input, size_t input_len, int bracketed_mode,
    uint8_t *output, size_t output_cap);

#endif // TERMINAL_STATE_H
