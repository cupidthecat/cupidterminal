// cupid.h

#ifndef CUPID_H
#define CUPID_H

#include <stddef.h>
#include <stdint.h>
#include <utf8proc.h>

typedef uint_least32_t Rune;

#define TRUECOLOR(r,g,b)  (0x01000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define IS_TRUECOL(x)     ((x) >= 0x01000000u && (x) <= 0x01FFFFFFu)

extern int term_rows;
extern int term_cols;
#define MAX_CHARS 4096

#define MAX_UTF8_CHAR_SIZE 32  // buffer size for last-printed codepoint scratch (CSI REP)

#define ATTR_BOLD       (1 << 0)
#define ATTR_FAINT      (1 << 1)
#define ATTR_ITALIC     (1 << 2)
#define ATTR_UNDERLINE  (1 << 3)
#define ATTR_REVERSE    (1 << 4)
#define ATTR_BLINK      (1 << 5)
#define ATTR_INVISIBLE  (1 << 6)
#define ATTR_STRUCK     (1 << 7)
#define ATTR_WRAP       (1 << 8)  /* line was soft-wrapped here (st compat) */
#define ATTR_WIDE       (1 << 10) /* lead cell of a wide (double-width) glyph */
#define ATTR_WDUMMY     (1 << 11) /* trailing continuation cell of a wide glyph */

#define COLOR_DEFAULT_FG 258
#define COLOR_DEFAULT_BG 259

#define Glyph Glyph_
typedef struct {
    Rune u;                  /* base codepoint (sole text storage as of Phase 3.6) */
    uint16_t mode;
    uint32_t fg;
    uint32_t bg;
} Glyph;

typedef Glyph *Line;

/* ATTR_HASCOMB on a base Glyph means there is a CombMark for this column
   in the row's combs[] array. Phase 3 introduces this; never set in
   Phase 0/1/2 code. */
#define ATTR_HASCOMB (1 << 9)

typedef struct CombMark {
    int      col;     /* column of cluster's base cell */
    Rune    *runes;   /* combining codepoints, dynamic */
    uint8_t  n;
    uint8_t  cap;
} CombMark;

typedef struct TermLine {
    Line       line;     /* Glyph[cols] — base cells */
    CombMark  *combs;    /* sorted by col, sparse; NULL when row has no combs */
    uint16_t   ncombs;
    uint16_t   combs_cap;
    uint8_t    dirty;    /* per-row dirty flag: 1 means row must be redrawn */
} TermLine;

/* Append a combining codepoint to the cluster anchored at column `col`
   on row `row`. Sets ATTR_HASCOMB on the base. Allocates as needed.
   Phase 3.5 implements this; declared here so Phase 3.1 can compile. */
void term_append_comb(int row, int col, Rune cp);

/* Phase 3.6: Encode the cluster anchored at (row,col) — base codepoint
   plus any combining marks from the row's side-channel — into out/cap as
   NUL-terminated UTF-8.  Returns bytes written (NOT counting NUL),
   0 on empty / error. */
size_t term_render_cluster(int row, int col, char *out, size_t cap);

/* Callback for DSR/DA responses: terminal writes bytes back to host */
typedef void (*terminal_response_fn)(const uint8_t *bytes, size_t len, void *ctx);

typedef struct {
    int row;
    int col;
    uint32_t current_fg;
    uint32_t current_bg;
    uint16_t current_mode;
    int saved_row; int saved_col;
    uint32_t saved_fg; uint32_t saved_bg; uint16_t saved_mode;  /* DECSC/DECRC */
    int cursor_visible;  /* 0 = hidden (DECRST ?25), 1 = visible (default) */
    int scroll_top;     /* 0-based, inclusive; -1 = use 0 */
    int scroll_bottom;  /* 0-based, inclusive; -1 = use term_rows-1 */
    int alt_screen_active;
    int bracketed_paste_mode;
    int autowrap_mode;      /* DECAWM ?7 */
    int origin_mode;        /* DECOM ?6 */
    int insert_mode;        /* IRM 4 */
    int wrap_next;          /* st-like pending wrap flag */
    int wrap_overwrite_next; /* HT at margin: next printable overwrites last col, same row */
    int lnm_mode;          /* LNM 20: LF sends CR+LF */
    int echo_mode;         /* SRM 12: echo input to display */
    int print_mode;        /* MC 5/4 or -o: mirror incoming text */

    /* Mouse reporting: ?1000 basic, ?1002 button event, ?1003 any motion, ?1006 SGR */
    int mouse_reporting_basic;   /* 1000: press/release */
    int mouse_reporting_button;  /* 1002: + motion while pressed */
    int mouse_reporting_any;     /* 1003: + motion always */
    int mouse_reporting_x10;     /* 9: press-only X10 protocol */
    int mouse_sgr_mode;          /* 1006: use <b;x;y;M/m format */
    int application_cursor_keys; /* DECCKM ?1: use SS3 O? for arrows */
    int application_keypad;      /* DECPAM/DECPNM: application keypad */
    int keyboard_lock;           /* KAM 2: suppress keyboard input */
    int meta_eight_bit;          /* DECSET 1034: Meta sets the high bit */

    int alt_saved_row;
    int alt_saved_col;
    uint32_t alt_saved_fg;
    uint32_t alt_saved_bg;
    uint16_t alt_saved_mode;
    int alt_saved_scroll_top;
    int alt_saved_scroll_bottom;

    char window_title[256];
    int title_dirty;

    uint8_t utf8_buf[4];
    int utf8_len;

    int osc_active;
    int osc_esc_pending;
    int osc_overflow;
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

    /* G0-G3 charsets: 0=USA/ASCII, 1=DEC Special Graphics (box drawing) */
    int charset_g0;
    int charset_g1;
    int charset_g2;
    int charset_g3;
    int gl;  /* active G0-G3 table in GL */

    /* DECSET 1004: send \033[I/\033[O on focus in/out */
    int focus_mode;

    /* DECSCNM (DEC private mode 5): global reverse video */
    int screen_reverse;

    /* Dynamic default colors set via OSC 10/11/12 (0=use palette default) */
    uint32_t osc_fg_color;  /* 0 = use default */
    uint32_t osc_bg_color;  /* 0 = use default */
    uint32_t osc_cs_color;  /* 0 = use default */

    /* OSC 4 palette overrides (indexed 0..255); 0 = not overridden */
    uint32_t palette_override[256];
    uint8_t  palette_overridden[256];

    /* UTF-8 mode: 1 = UTF-8 (default), 0 = legacy 8-bit */
    int utf8_mode;

    /* Primary-screen scrollback offset (0 = live bottom) */
    int scrollback_offset;

    /* Non-OSC string collector state (DCS/APC/PM): ignored until ST */
    int str_ignore_active;
    int str_ignore_esc_pending;
} Term;


// Declare the global Term variable
extern Term term;

/* Active screen buffer: primary or alt — whichever is active. */
extern TermLine *term_lines;

// Function prototypes
void tresize(int new_rows, int new_cols);
void tnew(Term *state);
void treset(Term *state);
void twrite(const uint8_t *bytes, size_t len, Term *state,
    terminal_response_fn response_fn, void *response_ctx);
void tputc(char c, Term *state);
size_t tpastefmt(const uint8_t *input, size_t input_len, int bracketed_mode,
    uint8_t *output, size_t output_cap);
void tfulldirt(void);
void kscrollup_n(int n);
void kscrolldown_n(int n);
void kscrollreset(void);
int tgetscrolloffset(void);
const Glyph *tgetline(int visual_row);

/* ---- Selection API (all selection state lives in Term; x.c never
        touches term.sel_* directly) ---------------------------------- */
#ifndef SNAP_WORD
#define SNAP_WORD 1
#define SNAP_LINE 2
#endif
#ifndef SEL_REGULAR
#define SEL_REGULAR    0
#define SEL_RECTANGULAR 1
#endif

void selstart(int col, int row, int snap, int type);
void selextend(int col, int row);
void selrelease(int col, int row);
void selclear(void);
int  selisactive(void);           /* 1 if a selection is in progress */
int  selected(int col, int row);
char *getsel(void);   /* malloc'd UTF-8 of current selection; caller frees */

/* ---- PTY write (owns echo-mode and lnm-mode expansion) ------------- */
void ttywrite(const char *s, size_t n, int may_echo);
int tprinter_open(const char *path);
void tprinter_close(void);
void tprinter_toggle(void);
void tprinter_screen(void);
void tprinter_selection(void);

#endif // CUPID_H
