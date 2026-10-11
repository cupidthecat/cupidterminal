#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fputs("focus peer: cannot open display\n", stderr);
        return 1;
    }
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                        20, 20, 120, 80, 0, 0, 0);
    XStoreName(display, window, "cupid-focus-peer");
    XMapWindow(display, window);
    XSync(display, False);
    for (;;) pause();
}
