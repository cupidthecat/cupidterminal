/* config.h - cupidterminal non-X11 configuration.
 *
 * This file is included by both st.c (the X11-free terminal logic) and by
 * xconfig.h (the X11 layer's config view).  It MUST NOT include any X11
 * headers so that st.c's translation unit stays Xlib-clean.
 *
 * All X11-dependent definitions (keymap tables, Shortcut/Key/Arg types,
 * X modifier macros) live in src/xconfig.h, which is included only by x.c.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

/* Type/macro compatibility (cupidterminal-specific) */
#define FONT (opt_font ? opt_font : font)
#define FONT_SIZE (usedfontsize > 0 ? (int)usedfontsize : 12)
#define TERMINAL_WIDTH (cols)
#define TERMINAL_HEIGHT (rows)
#define TERM (termname)
#define UTF8_SUPPORT 1
#define SHELL (shell)

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *font __attribute__((unused)) = "undefined medium:pixelsize=12:antialias=true:autohint=true";
extern char *opt_font;
static int borderpx __attribute__((unused)) = 2;
extern double usedfontsize;
extern double defaultfontsize;

/* identification sequence returned in DA and DECID */
extern char *vtiden;

/* Kerning / character bounding-box multipliers */
static float cwscale __attribute__((unused)) = 1.0f;
static float chscale __attribute__((unused)) = 1.0f;

/*
 * word delimiter string
 *
 * More advanced example: L" `'\"()[]{}"
 */
static wchar_t *worddelimiters __attribute__((unused)) = L" ";

/* selection timeouts (in milliseconds) */
static unsigned int doubleclicktimeout __attribute__((unused)) = 300;
static unsigned int tripleclicktimeout __attribute__((unused)) = 600;

/* Tab spaces */
extern unsigned int tabspaces;

/* alt screens */
extern int allowaltscreen;

/* allow certain non-interactive (insecure) window operations */
extern int allowwindowops;

/*
 * draw latency range in ms - from new content/keypress/etc until drawing.
 * within this range, st draws when content stops arriving (idle). mostly it's
 * near minlatency, but it waits longer for slow updates to avoid partial draw.
 * low minlatency will tear/flicker more, as it can "detect" idle too early.
 */
static double minlatency __attribute__((unused)) = 2;
static double maxlatency __attribute__((unused)) = 33;

/*
 * blinking timeout (set to 0 to disable blinking) for the terminal blinking
 * attribute.
 */
static unsigned int blinktimeout __attribute__((unused)) = 800;

/*
 * thickness of underline and bar cursors
 */
static unsigned int cursorthickness __attribute__((unused)) = 2;

/*
 * bell volume. It must be a value between -100 and 100. Use 0 for disabling
 * it
 */
static int bellvolume __attribute__((unused)) = 0;

/* default TERM value */
extern char *termname;

/* Shell and execution */
static char *shell __attribute__((unused)) = "/bin/bash";
extern char *utmp;
extern char *scroll;
extern char *stty_args;

/* Default cols/rows (overridden by -g, defined in main.c) */
extern unsigned int cols;
extern unsigned int rows;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static unsigned int cursorshape __attribute__((unused)) = 2;

/*
 * Default colour and shape of the mouse cursor
 */
static unsigned int mouseshape __attribute__((unused)) = 0; /* XC_xterm */
static unsigned int mousefg __attribute__((unused)) = 7;
static unsigned int mousebg __attribute__((unused)) = 0;

/* Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern unsigned int defaultcs;
static unsigned int defaultrcs __attribute__((unused)) = 257;

/* Terminal colors (16 first used in escape sequence) */
static const char *colorname[] __attribute__((unused)) = {
	"black", "red3", "green3", "yellow3", "blue2", "magenta3", "cyan3", "gray90",
	"gray50", "red", "green", "yellow", "#5c5cff", "magenta", "cyan", "white",
	[255] = 0,
	"#cccccc", "#555555", "gray90", "black",
};

#endif /* CONFIG_H */
