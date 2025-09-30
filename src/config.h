// config.h
#ifndef CONFIG_H
#define CONFIG_H

#define FONT "Noto Sans Mono CJK, Noto Color Emoji-12"
#define FONT_SIZE 12
#define TERMINAL_WIDTH 80
#define TERMINAL_HEIGHT 24
#define TERM "xterm-256color"
#define UTF8_SUPPORT 1

#define SHELL "/bin/sh" // Default shell; users can change this to their preferred shell

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include "input.h"
#include "draw.h" // Ensure this line is present

extern int master_fd; // Declare external PTY file descriptor

void handle_keypress(Display *display, Window window, XEvent *event);

#endif // CONFIG_H
