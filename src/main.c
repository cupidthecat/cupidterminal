// main.c
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <pty.h>
#include <string.h>
#include <termios.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <utf8proc.h> // unicode
#include <stdint.h>
#include "draw.h"
#include "input.h"
#include "config.h"
#include "terminal_state.h"
#include <X11/Xatom.h>

#define BUF_SIZE 1024

int master_fd;  // PTY master file descriptor
extern int g_cell_w, g_cell_h; 
extern int g_cell_gap;
const unsigned char *clipboard_get_data(size_t *len_out);

void handle_resize(int sig) {
    (void)sig; // Silence unused parameter warning

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        // Forward the size to the PTY master
        ioctl(master_fd, TIOCSWINSZ, &ws);
    }
}

void spawn_shell() {
    // Set TERM environment variable
    if (setenv("TERM", "xterm-256color", 1) == -1) {
        perror("setenv TERM failed");
    }
    if (setenv("LANG", "en_US.UTF-8", 1) == -1) {
        perror("setenv LANG failed");
    }
    if (setenv("LC_ALL", "en_US.UTF-8", 1) == -1) {
        perror("setenv LC_ALL failed");
    }

    // Ensure correct terminal size before starting shell
    handle_resize(0);

    int slave_fd;
    pid_t pid;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == -1) {
        perror("openpty failed");
        exit(EXIT_FAILURE);
    }

    fcntl(master_fd, F_SETFL, O_NONBLOCK); // Set non-blocking mode

    pid = fork();
    if (pid == -1) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        close(master_fd);
        setsid();

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl failed");
            exit(EXIT_FAILURE);
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        execlp(SHELL, SHELL, NULL); // Use SHELL from config.h
        perror("execlp failed");
        exit(EXIT_FAILURE);
    }

    close(slave_fd);
}

void handle_pty_output(Display *display, Window window, GC gc, TerminalState *state) {
    char buf[BUF_SIZE];
    ssize_t num_read = read(master_fd, buf, BUF_SIZE - 1);

    if (num_read > 0) {
        buf[num_read] = '\0';

        const uint8_t *p = (const uint8_t *)buf;
        while (*p) {
            if (*p == 0x1B) { // ESC
                const uint8_t *start = p++;
                if (*p == '[') {
                    // CSI: ESC [ ... <final>
                    p++;
                    const uint8_t *q = p;
                    while (*q && !((*q >= '@' && *q <= '~'))) q++; // final byte
                    if (*q) {
                        int len = (int)((q - 1) - start + 2); // include ESC '[' ... final
                        handle_ansi_sequence((const char*)start, len, state, display);
                        p = q + 1;
                        continue;
                    } else {
                        // incomplete; bail out and print raw
                        p = start;
                    }
                } else if (*p == ']') {
                    // OSC: ESC ] ... BEL(0x07) or ST (ESC \)
                    p++;
                    while (*p && *p != 0x07) {
                        if (*p == 0x1B && *(p+1) == '\\') { p += 2; break; } // ST
                        p++;
                    }
                    if (*p == 0x07) p++; // skip BEL
                    continue; // drop OSC entirely
                } else {
                    // some other ESC sequence; ignore it
                    continue;
                }
            }

            // tabs → spaces (simple 4-space expansion)
            if (*p == '\t') {
                for (int i = 0; i < 4; i++) put_char(' ', state);
                p++;
                continue;
            }

            // normal UTF-8 byte goes through the decoder in put_char
            put_char(*p++, state);
        }

        draw_text(display, window, gc);
    }
}

int main() {
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
    XSelectInput(display, window,
        ExposureMask | KeyPressMask | PropertyChangeMask | StructureNotifyMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(display, window);

    gc = XCreateGC(display, window, 0, NULL);

    // Start shell process in a pseudo-terminal
    spawn_shell();

    initialize_xft(display, window);
    
    // Intern atoms once
    Atom XA_CLIPBOARD = XInternAtom(display, "CLIPBOARD", False);
    Atom XA_UTF8      = XInternAtom(display, "UTF8_STRING", False);
    Atom XA_TEXT      = XInternAtom(display, "TEXT", False);
    Atom XA_TARGETS   = XInternAtom(display, "TARGETS", False);
    Atom XA_XSEL_DATA = XInternAtom(display, "XSEL_DATA", False); // property name we’ll use on paste

    // Main event loop (handles both PTY output and X11 events)
    while (1) {
        FD_ZERO(&fds);
        FD_SET(master_fd, &fds);
        FD_SET(ConnectionNumber(display), &fds);

        if (select(master_fd + 1, &fds, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(master_fd, &fds)) {
                handle_pty_output(display, window, gc, &term_state);
            }

            while (XPending(display)) {
                XNextEvent(display, &event);

                if (event.type == KeyPress) {
                    handle_keypress(display, window, &event);
                } else if (event.type == Expose) {
                    draw_text(display, window, gc);
                } else if (event.type == SelectionNotify) {
                    handle_paste_event(display, window, &event);
                } else if (event.type == ConfigureNotify) {
                    XWindowAttributes wa; XGetWindowAttributes(display, window, &wa);
                    int cw = (g_cell_w > 0) ? (g_cell_w + g_cell_gap) : xft_font->max_advance_width;
                    int ch = (g_cell_h > 0) ? g_cell_h : (xft_font->ascent + xft_font->descent + 2);
                    struct winsize ws = {
                        .ws_col = wa.width  / (cw ? cw : 8),
                        .ws_row = wa.height / (ch ? ch : 16),
                    };
                    ioctl(master_fd, TIOCSWINSZ, &ws);
                    
                } else if (event.type == ButtonPress && event.xbutton.button == Button1) {
                    int r,c; xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                    term_state.sel_active = 1;
                    term_state.sel_anchor_row = term_state.sel_row = r;
                    term_state.sel_anchor_col = term_state.sel_col = c;
                    draw_text(display, window, gc);
                } else if (event.type == MotionNotify && term_state.sel_active &&
                         (event.xmotion.state & Button1Mask)) {
                    int r,c; xy_to_cell(event.xmotion.x, event.xmotion.y, &r, &c);
                    term_state.sel_row = r; term_state.sel_col = c;
                    draw_text(display, window, gc);
                } else if (event.type == ButtonRelease && event.xbutton.button == Button1) {
                    int r,c; xy_to_cell(event.xbutton.x, event.xbutton.y, &r, &c);
                    term_state.sel_row = r; term_state.sel_col = c;
                    draw_text(display, window, gc);
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
        }
    }

    cleanup_xft();
    XCloseDisplay(display);
    return 0;
}
