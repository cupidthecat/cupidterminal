#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t pixels(Display *dpy, Window win) {
    XWindowAttributes a;
    if (!XGetWindowAttributes(dpy, win, &a)) exit(2);
    XImage *im = XGetImage(dpy, win, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    if (!im) exit(2);
    uint64_t hash = 1469598103934665603ULL;
    for (int y = 0; y < a.height; y++)
        for (int x = 0; x < a.width; x++) {
            hash ^= XGetPixel(im, x, y);
            hash *= 1099511628211ULL;
        }
    XDestroyImage(im);
    return hash;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 2;
    Window win = strtoul(argv[1], NULL, 0);
    struct timespec delay = {0, 250000000};
    uint64_t previous = pixels(dpy, win);
    int changes = 0;
    for (int i = 0; i < 16; i++) {
        nanosleep(&delay, NULL);
        uint64_t current = pixels(dpy, win);
        if (current != previous) changes++;
        previous = current;
    }
    XCloseDisplay(dpy);
    if (changes < 2) {
        fprintf(stderr, "TEST FAILURE: idle blinking changed only %d times\n", changes);
        return 1;
    }
    puts("PASS: x11/idle_blink");
    return 0;
}
