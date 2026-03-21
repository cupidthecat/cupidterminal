// src/input.c
#define _POSIX_C_SOURCE 199309L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <wchar.h>
#include <utf8proc.h>
#include "input.h"
#include "terminal_state.h"
#include "draw.h"
#include "config.h"
#include "config_keys.h"
#include <X11/Xutil.h>

#define SNAP_WORD 1
#define SNAP_LINE 2
#define SEL_REGULAR 0
#define SEL_RECTANGULAR 1

#define TIMEDIFF_MS(t1, t2) \
    (((t1).tv_sec - (t2).tv_sec) * 1000 + ((t1).tv_nsec - (t2).tv_nsec) / 1000000)

extern TerminalState term_state;
extern TerminalCell **terminal_buffer;
extern int term_rows, term_cols;
extern Display *global_display;
extern Window global_window;

static unsigned char *g_clip_data = NULL;
static struct timespec g_tclick1;
static struct timespec g_tclick2;
static size_t g_clip_len = 0;
static int g_pty_fd = -1;
static int g_numlock = 0;

void input_set_pty_fd(int fd) {
    g_pty_fd = fd;
}

static int match(unsigned int mask, unsigned int state) {
    return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

static const char *kmap(KeySym k, unsigned int state) {
    const Key *kp;
    int i;

    for (i = 0; i < (int)LEN(mappedkeys); i++) {
        if (mappedkeys[i] == k)
            break;
    }
    if (i == (int)LEN(mappedkeys) && (k & 0xFFFF) < 0xFD00)
        return NULL;

    for (kp = key; kp < key + LEN(key); kp++) {
        if (kp->k != k)
            continue;
        if (!match(kp->mask, state))
            continue;
        if (kp->appcursor && (term_state.application_cursor_keys ? kp->appcursor < 0 : kp->appcursor > 0))
            continue;
        if (kp->appkey && g_numlock && kp->appkey == 2)
            continue;
        return kp->s;
    }
    return NULL;
}

void clipcopy(const Arg *arg) {
    (void)arg;
    if (global_display && global_window)
        copy_to_clipboard(global_display, global_window);
}

void clippaste(const Arg *arg) {
    (void)arg;
    if (global_display && global_window)
        paste_from_clipboard(global_display, global_window);
}

void selpaste(const Arg *arg) {
    (void)arg;
    if (global_display && global_window)
        paste_from_clipboard(global_display, global_window);
}

void zoom(const Arg *arg) {
    if (global_display && global_window) {
        xft_zoom(global_display, global_window, arg->f);
    }
}

void zoomreset(const Arg *arg) {
    (void)arg;
    if (global_display && global_window) {
        xft_zoom_reset(global_display, global_window);
    }
}

void sendbreak(const Arg *arg) {
    (void)arg;
    if (g_pty_fd >= 0)
        tcsendbreak(g_pty_fd, 0);
}

void numlock(const Arg *arg) {
    (void)arg;
    g_numlock ^= 1;
}

static void tty_write_all_may_echo(int fd, const uint8_t *data, size_t len, int may_echo) {
    uint8_t expanded[512];
    const uint8_t *to_write = data;
    size_t to_len = len;

    if (fd < 0 || !data || len == 0) {
        return;
    }

    if (may_echo && term_state.echo_mode) {
        terminal_consume_bytes(data, len, &term_state, NULL, NULL);
    }

    if (term_state.lnm_mode && len <= sizeof(expanded) / 2) {
        size_t out = 0;
        for (size_t i = 0; i < len && out + 2 <= sizeof(expanded); i++) {
            if (data[i] == '\r') {
                expanded[out++] = '\r';
                expanded[out++] = '\n';
            } else {
                expanded[out++] = data[i];
            }
        }
        to_write = expanded;
        to_len = out;
    }

    {
        size_t sent = 0;
        while (sent < to_len) {
            ssize_t rc = write(fd, to_write + sent, to_len - sent);
            if (rc > 0) {
                sent += (size_t)rc;
                continue;
            }
            if (rc < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }
}

static void tty_write_all(int fd, const uint8_t *data, size_t len) {
    tty_write_all_may_echo(fd, data, len, 0);
}

void ttysend(const Arg *arg) {
    if (arg->s && g_pty_fd >= 0)
        tty_write_all_may_echo(g_pty_fd, (const uint8_t *)arg->s, strlen(arg->s), 1);
}

/* Check if cell at (r,c) is a word delimiter */
static int cell_is_delim(int r, int c) {
    const char *s;
    utf8proc_int32_t cp;
    utf8proc_ssize_t n;

    if (r < 0 || r >= term_rows || c < 0 || c >= term_cols)
        return 1;
    s = terminal_buffer[r][c].c;
    if (!s || !*s)
        return 1; /* space or empty */
    n = utf8proc_iterate((const utf8proc_uint8_t *)s, -1, &cp);
    if (n <= 0)
        return 1;
    return wcschr(worddelimiters, (wchar_t)cp) != NULL;
}

static void selsnap(int *x, int *y, int direction) {
    int newx, newy;
    int prevdelim, delim;

    switch (term_state.sel_snap) {
    case SNAP_WORD:
        prevdelim = cell_is_delim(*y, *x);
        for (;;) {
            newx = *x + direction;
            newy = *y;
            if (newx < 0 || newx >= term_cols) {
                newy += direction;
                newx = (newx + term_cols) % term_cols;
                if (newy < 0 || newy >= term_rows)
                    break;
            }
            delim = cell_is_delim(newy, newx);
            if (delim != prevdelim)
                break;
            *x = newx;
            *y = newy;
            prevdelim = delim;
        }
        break;
    case SNAP_LINE:
        *x = (direction < 0) ? 0 : (term_cols - 1);
        break;
    }
}

void selection_get_effective_bounds(int *sr, int *sc, int *er, int *ec) {
    int ar, ac, br, bc;

    if (!term_state.sel_active) {
        *sr = *sc = *er = *ec = 0;
        return;
    }
    ar = term_state.sel_anchor_row;
    ac = term_state.sel_anchor_col;
    br = term_state.sel_row;
    bc = term_state.sel_col;

    if (term_state.sel_type == SEL_RECTANGULAR) {
        *sr = (ar < br) ? ar : br;
        *er = (ar < br) ? br : ar;
        *sc = (ac < bc) ? ac : bc;
        *ec = (ac < bc) ? bc : ac;
        return;
    }

    /* Normalize for regular selection */
    if (ar * term_cols + ac > br * term_cols + bc) {
        int t;
        t = ar; ar = br; br = t;
        t = ac; ac = bc; bc = t;
    }
    *sr = ar; *sc = ac; *er = br; *ec = bc;
    if (term_state.sel_snap) {
        selsnap(sc, sr, -1);
        selsnap(ec, er, +1);
    }
}

void selection_start(int col, int row, unsigned int state) {
    int type;
    int snap = 0;
    struct timespec now;

    state &= ~(Button1Mask | Button2Mask | Button3Mask);
    for (type = 1; type < (int)LEN(selmasks); type++) {
        if (match(selmasks[type], state)) {
            term_state.sel_type = SEL_RECTANGULAR;
            term_state.sel_snap = 0;
            term_state.sel_active = 1;
            term_state.sel_anchor_row = term_state.sel_row = row;
            term_state.sel_anchor_col = term_state.sel_col = col;
            return;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (TIMEDIFF_MS(now, g_tclick2) <= (int)tripleclicktimeout)
        snap = SNAP_LINE;
    else if (TIMEDIFF_MS(now, g_tclick1) <= (int)doubleclicktimeout)
        snap = SNAP_WORD;
    g_tclick2 = g_tclick1;
    g_tclick1 = now;

    term_state.sel_type = SEL_REGULAR;
    term_state.sel_snap = snap;
    term_state.sel_active = 1;
    term_state.sel_anchor_row = term_state.sel_row = row;
    term_state.sel_anchor_col = term_state.sel_col = col;
}

void selection_extend(int col, int row) {
    if (term_state.sel_active) {
        term_state.sel_row = row;
        term_state.sel_col = col;
    }
}

void selection_release(Display *display, Window window, int col, int row) {
    if (!term_state.sel_active)
        return;
    term_state.sel_row = row;
    term_state.sel_col = col;
    copy_to_clipboard(display, window);
    term_state.sel_active = 0;
}

const unsigned char *clipboard_get_data(size_t *len_out) {
    if (len_out) *len_out = g_clip_len;
    return g_clip_data;
}

void clipboard_set_data(Display *display, Window window, const uint8_t *data, size_t len) {
    Atom clipboard;
    Atom primary;
    unsigned char *copy;

    free(g_clip_data);
    g_clip_data = NULL;
    g_clip_len = 0;

    if (!display || !window || !data || len == 0) {
        return;
    }

    copy = malloc(len);
    if (!copy) {
        return;
    }
    memcpy(copy, data, len);

    g_clip_data = copy;
    g_clip_len = len;

    clipboard = XInternAtom(display, "CLIPBOARD", False);
    primary = XA_PRIMARY;
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XSetSelectionOwner(display, primary, window, CurrentTime);
}

void copy_to_clipboard(Display *display, Window window) {
    int sr, sc, er, ec;

    if (!term_state.sel_active) {
        return;
    }
    selection_get_effective_bounds(&sr, &sc, &er, &ec);

    size_t max_size = term_rows * term_cols * MAX_UTF8_CHAR_SIZE + term_rows + 1;
    char *selection = malloc(max_size);
    size_t pos = 0;
    selection[0] = '\0';

    for (int r = sr; r <= er; r++) {
        int cstart, cend;
        if (term_state.sel_type == SEL_RECTANGULAR) {
            cstart = sc;
            cend = ec;
        } else {
            cstart = (r == sr) ? sc : 0;
            cend   = (r == er) ? ec : (term_cols - 1);
        }

        /* build line */
        size_t line_start = pos;
        for (int c = cstart; c <= cend; c++) {
            const char *g = terminal_buffer[r][c].c;
            size_t glen = (*g) ? strlen(g) : 1;

            if (pos + glen >= max_size - 2) break;
            if (*g) { memcpy(&selection[pos], g, glen); }
            else    { selection[pos] = ' '; }
            pos += glen;
        }
        // trim trailing spaces of that line
        while (pos > line_start && selection[pos-1] == ' ') pos--;
        if (pos < max_size - 1) selection[pos++] = '\n';
        selection[pos] = '\0';
    }

    clipboard_set_data(display, window, (const uint8_t *)selection, pos);
    free(selection);
}

void paste_from_clipboard(Display *display, Window window) {
    Atom clipboard   = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom xsel_data   = XInternAtom(display, "XSEL_DATA", False);
    XConvertSelection(display, clipboard, utf8_string, xsel_data, window, CurrentTime);
}

void handle_paste_event(Display *display, Window window, XEvent *event, int pty_fd) {
    if (event->type != SelectionNotify) return;

    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    Atom xsel_data   = XInternAtom(display, "XSEL_DATA", False);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    XGetWindowProperty(display, window, xsel_data, 0, 1<<20, False, utf8_string,
                       &actual_type, &actual_format, &nitems, &bytes_after, &data);

    if (data && pty_fd >= 0) {
        size_t payload_cap = (size_t)nitems + 16;
        uint8_t *payload = malloc(payload_cap);
        size_t payload_len = 0;

        if (payload) {
            payload_len = terminal_format_paste_payload(
                (const uint8_t *)data,
                (size_t)nitems,
                term_state.bracketed_paste_mode,
                payload,
                payload_cap);
            if (payload_len > 0) {
                tty_write_all_may_echo(pty_fd, payload, payload_len, 1);
            }
            free(payload);
        }

        XFree(data);
    }
}

void send_mouse_report(int pty_fd, int event_type, int button, int col, int row,
    unsigned int modifiers, int sgr_mode) {
    if (pty_fd < 0 || col < 1 || row < 1) return;

    int code;
    if (event_type == 2) {
        code = 32;
    } else {
        code = 0;
    }

    if ((!sgr_mode && event_type == 1) || button == 12) {
        code += 3;
    } else if (button >= 8) {
        code += 128 + button - 8;
    } else if (button >= 4) {
        code += 64 + button - 4;
    } else {
        code += button - 1;
    }

    if (modifiers & ShiftMask) code += 4;
    if (modifiers & Mod1Mask) code += 8;
    if (modifiers & ControlMask) code += 16;

    if (sgr_mode) {
        char buf[32];
        char final = (event_type == 1) ? 'm' : 'M';
        int n = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c", code, col, row, final);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            tty_write_all(pty_fd, (const uint8_t *)buf, (size_t)n);
        }
    } else if (col <= 223 && row <= 223) {
        unsigned char buf[6];
        buf[0] = '\033';
        buf[1] = '[';
        buf[2] = 'M';
        buf[3] = (unsigned char)(32 + code);
        buf[4] = (unsigned char)(32 + col);
        buf[5] = (unsigned char)(32 + row);
        tty_write_all(pty_fd, buf, 6);
    }
}

/* Wrapper for key/char output - uses CRLF and echo modes */
static void tty_write(int fd, const char *data, size_t len) {
    if (fd < 0 || !data || len == 0) {
        return;
    }
    tty_write_all_may_echo(fd, (const uint8_t *)data, len, 1);
}

static int mouseaction(XEvent *e, unsigned int release) {
    const MouseShortcut *ms;
    unsigned int state = e->xbutton.state & ~(Button1Mask | Button2Mask | Button3Mask);

    for (ms = mshortcuts; ms < mshortcuts + LEN(mshortcuts); ms++) {
        if (ms->release == release && ms->button == e->xbutton.button &&
            (match(ms->mod, state) || match(ms->mod, state & ~forcemousemod))) {
            if (ms->func)
                ms->func(&ms->arg);
            return 1;
        }
    }
    return 0;
}

int handle_mouse_shortcut(XEvent *event, int pty_fd) {
    g_pty_fd = pty_fd;
    if (event->type == ButtonPress)
        return mouseaction(event, 0);
    if (event->type == ButtonRelease)
        return mouseaction(event, 1);
    return 0;
}

void handle_keypress(Display *display, Window window, XEvent *event, int pty_fd) {
    char buffer[128];
    KeySym keysym;
    int len;
    unsigned int state;
    Status xim_status;

    (void)display;
    (void)window;

    if (g_xic) {
        keysym = NoSymbol;
        len = XmbLookupString(g_xic, &event->xkey, buffer, sizeof(buffer) - 1,
                              &keysym, &xim_status);
        if (len < 0) len = 0;
        buffer[len] = '\0';
        switch (xim_status) {
        case XLookupNone:
            /* IM consumed the event; it will deliver the result later via a
               synthetic event.  Do not process this event further. */
            return;
        case XBufferOverflow:
            len = 0;
            keysym = XLookupKeysym(&event->xkey, 0);
            break;
        case XLookupChars:
            /* XmbLookupString did not set keysym; resolve it separately so
               shortcut and kmap checks work correctly. */
            keysym = XLookupKeysym(&event->xkey, 0);
            break;
        case XLookupKeySym:
        case XLookupBoth:
            /* keysym (and possibly chars) already set by XmbLookupString. */
            break;
        default:
            keysym = XLookupKeysym(&event->xkey, 0);
            break;
        }
    } else {
        len = XLookupString(&event->xkey, buffer, sizeof(buffer), &keysym, NULL);
    }
    state = event->xkey.state;
    const Shortcut *bp;
    const char *customkey;

    g_pty_fd = pty_fd;

    /* 1. Check shortcuts */
    for (bp = shortcuts; bp < shortcuts + LEN(shortcuts); bp++) {
        if (keysym == bp->keysym && match(bp->mod, state)) {
            if (bp->func)
                bp->func(&bp->arg);
            return;
        }
    }

    /* 2. Check key table (kmap) */
    if ((customkey = kmap(keysym, state))) {
        tty_write_all_may_echo(pty_fd, (const uint8_t *)customkey, strlen(customkey), 1);
        return;
    }

    /* 3. Fall through to default handling */
    if (keysym == XK_Up || keysym == XK_Down || keysym == XK_Left || keysym == XK_Right) {
        unsigned int st = event->xkey.state;
        int mod = 0;
        if (st & ShiftMask) mod |= 1;
        if (st & Mod1Mask) mod |= 2;
        if (st & ControlMask) mod |= 4;
        if (mod > 0) mod += 1;

        char buf[16];
        const char *seq;
        size_t seqlen;
        if (term_state.application_cursor_keys) {
            static const char *app[] = {"\x1bOA", "\x1bOB", "\x1bOC", "\x1bOD"};
            static const char app_char[] = {'A', 'B', 'C', 'D'};
            int idx = (keysym == XK_Up) ? 0 : (keysym == XK_Down) ? 1 : (keysym == XK_Left) ? 3 : 2;
            if (mod > 0) {
                seqlen = (size_t)snprintf(buf, sizeof(buf), "\x1b[1;%d%c", mod, app_char[idx]);
                seq = buf;
            } else {
                seq = app[idx];
                seqlen = 3;
            }
        } else {
            if (mod > 0) {
                char ch = (keysym == XK_Up) ? 'A' : (keysym == XK_Down) ? 'B' : (keysym == XK_Left) ? 'D' : 'C';
                seqlen = (size_t)snprintf(buf, sizeof(buf), "\x1b[1;%d%c", mod, ch);
                seq = buf;
            } else {
                static const char *norm[] = {"\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"};
                int idx = (keysym == XK_Up) ? 0 : (keysym == XK_Down) ? 1 : (keysym == XK_Left) ? 3 : 2;
                seq = norm[idx];
                seqlen = 3;
            }
        }
        tty_write(pty_fd, seq, seqlen);
        return;
    }
    
    // (nice to have)
    else if (keysym == XK_Home)  { tty_write(pty_fd, "\x1b[H", 3); }
    else if (keysym == XK_End)   { tty_write(pty_fd, "\x1b[F", 3); }
    else if (keysym == XK_Page_Up)   { tty_write(pty_fd, "\x1b[5~", 4); }
    else if (keysym == XK_Page_Down) { tty_write(pty_fd, "\x1b[6~", 4); }
    else if (keysym == XK_Insert)    { tty_write(pty_fd, "\x1b[2~", 4); }
    else if (keysym == XK_Delete)    { tty_write(pty_fd, "\x1b[3~", 4); }
    /* Function keys F1-F12: xterm sequences */
    else if (keysym == XK_F1)        { tty_write(pty_fd, "\x1bOP", 4); }
    else if (keysym == XK_F2)        { tty_write(pty_fd, "\x1bOQ", 4); }
    else if (keysym == XK_F3)        { tty_write(pty_fd, "\x1bOR", 4); }
    else if (keysym == XK_F4)        { tty_write(pty_fd, "\x1bOS", 4); }
    else if (keysym == XK_F5)        { tty_write(pty_fd, "\x1b[15~", 5); }
    else if (keysym == XK_F6)        { tty_write(pty_fd, "\x1b[17~", 5); }
    else if (keysym == XK_F7)        { tty_write(pty_fd, "\x1b[18~", 5); }
    else if (keysym == XK_F8)        { tty_write(pty_fd, "\x1b[19~", 5); }
    else if (keysym == XK_F9)        { tty_write(pty_fd, "\x1b[20~", 5); }
    else if (keysym == XK_F10)       { tty_write(pty_fd, "\x1b[21~", 5); }
    else if (keysym == XK_F11)       { tty_write(pty_fd, "\x1b[23~", 5); }
    else if (keysym == XK_F12)       { tty_write(pty_fd, "\x1b[24~", 5); }

    if (keysym == XK_BackSpace) {
        tty_write(pty_fd, "\x7F", 1);
    } else if (keysym == XK_Return) {
        /* Send \r (carriage return), matching what a real keyboard generates
           and what st sends.  In raw PTY mode, readline/zle binds \r to
           accept-line; sending \n (Ctrl+J) can insert a literal newline and
           show the PS2 continuation prompt instead. */
        tty_write(pty_fd, "\r", 1);
    } else if ((event->xkey.state & Mod1Mask) && len > 0) {
        /* Alt/meta-as-escape: send ESC + unmodified character */
        tty_write(pty_fd, "\x1b", 1);
        tty_write(pty_fd, buffer, len);
    } else if (len > 0) {
        tty_write(pty_fd, buffer, len);
    }
}
