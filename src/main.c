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
#include "draw.h"
#include "input.h"
#include "config.h"

#define BUF_SIZE 1024

int master_fd;  // PTY master file descriptor

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

void handle_pty_output(Display *display, Window window, GC gc) {
    char buffer[BUF_SIZE];
    ssize_t num_read;

    while ((num_read = read(master_fd, buffer, BUF_SIZE - 1)) > 0) {
        buffer[num_read] = '\0';  // Null-terminate

        // Strip ANSI escape sequences (basic filtering)
        char clean_buffer[BUF_SIZE];
        int clean_index = 0;
        int in_escape_sequence = 0;

        for (ssize_t i = 0; i < num_read; i++) {
            // Skip ANSI escape sequences
            if (buffer[i] == '\033') { 
                in_escape_sequence = 1;
                continue;
            }
            if (in_escape_sequence) {
                if (isalpha(buffer[i])) {
                    in_escape_sequence = 0;
                }
                continue;
            }
            clean_buffer[clean_index++] = buffer[i];
        }
        clean_buffer[clean_index] = '\0';

        append_text(clean_buffer);  // Append cleaned output to buffer
        draw_text(display, window, gc); // Redraw with accumulated text
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
                                 BlackPixel(display, screen),
                                 WhitePixel(display, screen));

    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);

    gc = XCreateGC(display, window, 0, NULL);

    // Start shell process in a pseudo-terminal
    spawn_shell();

    // Main event loop (handles both PTY output and X11 events)
    while (1) {
        FD_ZERO(&fds);
        FD_SET(master_fd, &fds);
        FD_SET(ConnectionNumber(display), &fds);

        if (select(master_fd + 1, &fds, NULL, NULL, NULL) > 0) {
            // Handle shell output
            if (FD_ISSET(master_fd, &fds)) {
                handle_pty_output(display, window, gc);
            }

            // Handle X11 events (keypresses, window updates)
            while (XPending(display)) {
                XNextEvent(display, &event);
                if (event.type == KeyPress) {
                    handle_keypress(display, window, &event);
                }
            }
        }
    }

    XCloseDisplay(display);
    return 0;
}
