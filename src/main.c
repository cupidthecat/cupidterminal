// main.c
#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <signal.h>
#include <stdint.h>
#include <locale.h>
#include "arg.h"
#include "draw.h"  /* DRAW_LEFT_PAD, DRAW_TOP_PAD for winsize */
#include "input.h"
#include "config.h"
#include "pty_session.h"
#include "terminal_state.h"

#define BUF_SIZE 65536
#define VERSION "0.1"

char *argv0;
char *opt_font = NULL;
char *opt_class = NULL;
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "stty raw pass8 nl -echo -iexten -cstopb 38400";
char *vtiden = "\033[?6c";
int allowaltscreen = 1;
int allowwindowops = 0;
char *termname = "xterm-256color";
unsigned int tabspaces = 8;
unsigned int defaultfg = 258;
unsigned int defaultbg = 259;
unsigned int defaultcs = 256;
double usedfontsize = 12;
double defaultfontsize = 12;
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
    fprintf(stderr, "usage: cupidterminal [-aiv] [-c class] [-f font] [-g geometry] "
        "[-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid] [[-e] command [args ...]]\n"
        "       cupidterminal [-aiv] [-c class] [-f font] [-g geometry] "
        "[-n name] [-o file]\n"
        "          [-T title] [-t title] [-w windowid] -l line [stty_args ...]\n");
    exit(1);
}

static PtySession g_pty_session = {
    .master_fd = -1,
    .child_pid = -1,
    .child_exited = 0,
    .child_status = 0,
};
static volatile sig_atomic_t g_sigchld_pending = 0;

extern int g_cell_w, g_cell_h; 
extern int g_cell_gap;
const unsigned char *clipboard_get_data(size_t *len_out);

/* SIGWINCH: when run from another terminal, forwarding would use the parent
   terminal's size instead of our X11 window. We rely on ConfigureNotify for
   X11 resize. Handler is a no-op to avoid wrong winsize. */
static void handle_resize(int sig) {
    (void)sig;
}

static void handle_sigchld(int sig) {
    (void)sig;
    g_sigchld_pending = 1;
}

static void reap_child_processes(void) {
    int rc;

    if (!g_sigchld_pending) {
        return;
    }

    g_sigchld_pending = 0;
    do {
        rc = pty_session_reap_child(&g_pty_session, NULL);
    } while (rc == 1);
}

static void sync_pty_winsize_from_window(Display *display, Window window, PtySession *session) {
    XWindowAttributes wa;
    int cw;
    int ch;
    unsigned short ws_col;
    unsigned short ws_row;

    if (!display || !session) {
        return;
    }

    if (!XGetWindowAttributes(display, window, &wa)) {
        return;
    }

    cw = (g_cell_w > 0) ? (g_cell_w + g_cell_gap) : xft_font->max_advance_width;
    ch = (g_cell_h > 0) ? g_cell_h : (xft_font->ascent + xft_font->descent + 2);

    /* Account for padding so we don't report more rows/cols than we can display */
    {
        int usable_w = wa.width - DRAW_LEFT_PAD;
        int usable_h = wa.height - DRAW_TOP_PAD;
        if (usable_w < 1) usable_w = 1;
        if (usable_h < 1) usable_h = 1;
        ws_col = (unsigned short)(usable_w / (cw ? cw : 8));
        ws_row = (unsigned short)(usable_h / (ch ? ch : 16));
    }

    if (ws_col == 0) {
        ws_col = 1;
    }
    if (ws_row == 0) {
        ws_row = 1;
    }

    pty_session_set_winsize(session, ws_row, ws_col);
    resize_terminal(ws_row, ws_col);
}

static void pty_response_cb(const uint8_t *bytes, size_t len, void *ctx) {
    PtySession *session = (PtySession *)ctx;
    if (session && session->master_fd >= 0 && len > 0) {
        (void)pty_session_write(session, bytes, len);
    }
}

int handle_pty_output(Display *display, Window window, GC gc, PtySession *session, TerminalState *state) {
    (void)gc;
    char buf[BUF_SIZE];
    ssize_t num_read = pty_session_read(session, buf, BUF_SIZE - 1);

    if (num_read == 0) {
        return 0; // EOF or error (e.g. child exited)
    }
    if (num_read < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        }
        return 0;
    }

    terminal_consume_bytes((const uint8_t *)buf, (size_t)num_read, state, pty_response_cb, session);
    if (state->title_dirty) {
        XStoreName(display, window, state->window_title[0] ? state->window_title : "cupidterminal");
        state->title_dirty = 0;
    }
    if (state->osc52_pending) {
        clipboard_set_data(display, window, state->osc52_buf, state->osc52_len);
        state->osc52_pending = 0;
        state->osc52_len = 0;
    }
    /* draw deferred to main loop (latency batching) */
    return 1;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;

    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");

    ARGBEGIN {
    case 'a':
        allowaltscreen = 0;
        break;
    case 'c':
        opt_class = EARGF(usage());
        break;
    case 'e':
        if (argc > 0)
            --argc, ++argv;
        goto run;
    case 'f':
        opt_font = EARGF(usage());
        break;
    case 'g': {
        int x = 0, y = 0;
        XParseGeometry(EARGF(usage()), &x, &y, &cols, &rows);
        break;
    }
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
    if (argc > 0)
        opt_cmd = argv;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction SIGCHLD failed");
    }

    signal(SIGWINCH, handle_resize); // Handle window resize signals

    Display *display;
    Window window;
    GC gc;
    XEvent event;
    fd_set fds;

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Failed to open X display\n");
        return EXIT_FAILURE;
    }

    int screen = DefaultScreen(display);
    window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 0, 0, 800, 600, 1,
                                 BlackPixel(display, screen),   // border
                                 BlackPixel(display, screen));  // ← background was WhitePixel
    
    // Make sure future clears use black
    XSetWindowBackground(display, window, BlackPixel(display, screen));
    
    XStoreName(display, window, "cupidterminal");
    {
        Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wm_delete, 1);
    }
    XSelectInput(display, window,
        ExposureMask | KeyPressMask | KeyReleaseMask | PropertyChangeMask |
        StructureNotifyMask | FocusChangeMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(display, window);

    gc = XCreateGC(display, window, 0, NULL);

    /* Start shell / serial-line process */
    if (pty_session_spawn(&g_pty_session, opt_line, SHELL, opt_cmd, TERM) == -1) {
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    initialize_xft(display, window);
    {
        XWindowAttributes wa_init;
        if (XGetWindowAttributes(display, window, &wa_init)) {
            draw_notify_resize(wa_init.width, wa_init.height);
        }
    }
    sync_pty_winsize_from_window(display, window, &g_pty_session);
    
    // Intern atoms once
    Atom XA_UTF8      = XInternAtom(display, "UTF8_STRING", False);
    Atom XA_TEXT      = XInternAtom(display, "TEXT", False);
    Atom XA_TARGETS   = XInternAtom(display, "TARGETS", False);

    /* Draw latency: wait minlatency for idle, cap at maxlatency */
    int drawing = 0;
    struct timespec trigger = {0, 0};
    struct timespec now;

    /* Main event loop (handles both PTY output and X11 events) */
    while (1) {
        int x11_fd;
        int nfds;
        int ready;
        struct timeval *tv_ptr = NULL;
        struct timeval tv;
        double timeout_ms = -1;
        double elapsed;

        reap_child_processes();
        if (!pty_session_child_alive(&g_pty_session)) {
            break;
        }

        FD_ZERO(&fds);
        x11_fd = ConnectionNumber(display);
        FD_SET(x11_fd, &fds);
        nfds = x11_fd + 1;

        if (g_pty_session.master_fd >= 0) {
            FD_SET(g_pty_session.master_fd, &fds);
            if (g_pty_session.master_fd >= nfds) {
                nfds = g_pty_session.master_fd + 1;
            }
        }

        if (XPending(display)) {
            timeout_ms = 0;
        } else if (drawing && minlatency > 0 && maxlatency > 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - trigger.tv_sec) * 1000.0 +
                (now.tv_nsec - trigger.tv_nsec) / 1e6;
            timeout_ms = (maxlatency - elapsed) / maxlatency * minlatency;
            if (timeout_ms <= 0) {
                timeout_ms = 0;
            }
        }

        if (timeout_ms >= 0) {
            tv.tv_sec = (long)(timeout_ms / 1000);
            tv.tv_usec = (long)((timeout_ms - tv.tv_sec * 1000.0) * 1000);
            if (tv.tv_sec < 0) tv.tv_sec = 0;
            if (tv.tv_usec < 0) tv.tv_usec = 0;
            tv_ptr = &tv;
        }

        ready = select(nfds, &fds, NULL, NULL, tv_ptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select failed");
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        int had_input = 0;

        if (ready > 0 && g_pty_session.master_fd >= 0 && FD_ISSET(g_pty_session.master_fd, &fds)) {
            had_input = 1;
            if (!handle_pty_output(display, window, gc, &g_pty_session, &term_state)) {
                reap_child_processes();
                break; // PTY closed, exit main loop
            }
        }

        /* X events may already be queued client-side even when select() times out. */
        while (XPending(display)) {
            had_input = 1;
            XNextEvent(display, &event);

            /* XIM: let the input method filter events before we process them */
            if (XFilterEvent(&event, None))
                continue;

            if (event.type == KeyPress) {
                handle_keypress(display, window, &event, g_pty_session.master_fd);
            } else if (event.type == Expose) {
                /* draw deferred */
            } else if (event.type == SelectionNotify) {
                handle_paste_event(display, window, &event, g_pty_session.master_fd);
            } else if (event.type == ConfigureNotify) {
                draw_notify_resize(event.xconfigure.width, event.xconfigure.height);
                sync_pty_winsize_from_window(display, window, &g_pty_session);
                
            } else if (event.type == ButtonPress || event.type == ButtonRelease ||
                     event.type == MotionNotify) {
                if (handle_mouse_shortcut(&event, g_pty_session.master_fd)) {
                    /* Mouse shortcut handled (e.g. middle-click paste, scroll) */
                } else {
                int mouse_active = term_state.mouse_reporting_basic ||
                    term_state.mouse_reporting_button || term_state.mouse_reporting_any;
                if (mouse_active && g_pty_session.master_fd >= 0 &&
                    !(event.xbutton.state & ShiftMask)) {
                    int r = 0, c = 0, btn = 0, evt = -1;
                    unsigned int mods = 0;
                    if (event.type == ButtonPress) {
                        xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                        btn = event.xbutton.button;
                        mods = event.xbutton.state;
                        evt = 0;
                    } else if (event.type == ButtonRelease) {
                        xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                        btn = event.xbutton.button;
                        mods = event.xbutton.state;
                        if (btn == 4 || btn == 5) evt = -1;
                        else evt = 1;
                    } else {
                        xy_to_cell(event.xmotion.x, event.xmotion.y, &r, &c);
                        mods = event.xmotion.state;
                        if (term_state.mouse_reporting_any) {
                            evt = 2;
                            btn = 12;
                        } else if (term_state.mouse_reporting_button &&
                                (mods & (Button1Mask | Button2Mask | Button3Mask))) {
                            evt = 2;
                            btn = (mods & Button1Mask) ? 1 : (mods & Button2Mask) ? 2 : 3;
                        }
                    }
                    if (evt >= 0 && btn >= 1 && btn <= 12) {
                        send_mouse_report(g_pty_session.master_fd, evt, btn,
                            c + 1, r + 1, mods, term_state.mouse_sgr_mode);
                    }
                } else if (event.type == ButtonPress && event.xbutton.button == Button1) {
                    int r, c;
                    xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                    selection_start(c, r, event.xbutton.state);
                } else if (event.type == MotionNotify && term_state.sel_active &&
                         (event.xmotion.state & Button1Mask)) {
                    int r, c;
                    xy_to_cell(event.xmotion.x, event.xmotion.y, &r, &c);
                    selection_extend(c, r);
                } else if (event.type == ButtonRelease && event.xbutton.button == Button1) {
                    int r, c;
                    xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                    selection_release(display, window, c, r);
                }
                }
            } else if (event.type == FocusIn) {
                /* XIM: notify input context of focus */
                xim_focus_in();
                if (term_state.focus_mode && g_pty_session.master_fd >= 0)
                    (void)pty_session_write(&g_pty_session, "\033[I", 3);
            } else if (event.type == FocusOut) {
                xim_focus_out();
                if (term_state.focus_mode && g_pty_session.master_fd >= 0)
                    (void)pty_session_write(&g_pty_session, "\033[O", 3);
            } else if (event.type == ClientMessage) {
                Atom wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
                Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
                if (event.xclient.message_type == wm_protocols &&
                    (Atom)event.xclient.data.l[0] == wm_delete) {
                    pty_session_close(&g_pty_session);
                    cleanup_xft();
                    XCloseDisplay(display);
                    return 0;
                }
            } else if (event.type == SelectionRequest) {
                XSelectionRequestEvent *req = &event.xselectionrequest;
                XSelectionEvent ev = {
                    .type      = SelectionNotify,
                    .display   = req->display,
                    .requestor = req->requestor,
                    .selection = req->selection,
                    .target    = req->target,
                    .property  = req->property,
                    .time      = req->time
                };
            
                size_t len = 0;
                const unsigned char *data = clipboard_get_data(&len);
            
                if (!data) {
                    ev.property = None; // we have nothing to offer
                    XSendEvent(display, req->requestor, True, 0, (XEvent*)&ev);
                    XFlush(display);
                    continue;
                }
            
                if (req->target == XA_TARGETS) {
                    Atom targets[4] = { XA_UTF8, XA_TEXT, XA_STRING, XA_TARGETS };
                    XChangeProperty(display, req->requestor,
                                    req->property, XA_ATOM, 32, PropModeReplace,
                                    (unsigned char*)targets, 4);
                } else if (req->target == XA_UTF8 || req->target == XA_TEXT || req->target == XA_STRING) {
                    // Serve UTF-8 bytes
                    Atom type = (req->target == XA_STRING) ? XA_STRING : XA_UTF8;
                    XChangeProperty(display, req->requestor,
                                    req->property, type, 8, PropModeReplace,
                                    (unsigned char*)data, (int)len);
                } else {
                    // unsupported target
                    ev.property = None;
                }
            
                XSendEvent(display, req->requestor, True, 0, (XEvent*)&ev);
                XFlush(display);
            }
        }

        /* Draw latency: wait for idle (minlatency) or cap at maxlatency */
        if (had_input) {
            if (!drawing) {
                trigger = now;
                drawing = 1;
            }
            if (minlatency > 0 && maxlatency > 0) {
                elapsed = (now.tv_sec - trigger.tv_sec) * 1000.0 +
                    (now.tv_nsec - trigger.tv_nsec) / 1e6;
                timeout_ms = (maxlatency - elapsed) / maxlatency * minlatency;
                if (timeout_ms > 0)
                    continue;  /* wait for idle */
            }
        }

        draw_text(display, window, gc);
        xximspot(display, window);
        drawing = 0;
    }

    pty_session_close(&g_pty_session);
    cleanup_xft();
    XCloseDisplay(display);
    return 0;
}
