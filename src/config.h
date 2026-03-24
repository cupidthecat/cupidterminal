/* config.def.h - cupidterminal configuration (st-style) */

#ifndef CONFIG_H
#define CONFIG_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

/* Compatibility macros for existing code */
#define FONT (opt_font ? opt_font : font)
#define FONT_SIZE (usedfontsize > 0 ? (int)usedfontsize : 12)
#define TERMINAL_WIDTH (cols)
#define TERMINAL_HEIGHT (rows)
#define TERM (termname)
#define UTF8_SUPPORT 1
#define SHELL (shell)

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1u<<13|1u<<14)

/* Arg union for shortcut callbacks */
typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
	const char *s;
} Arg;

/* Shortcut and key structures */
typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
	unsigned int release;
} MouseShortcut;

typedef struct {
	KeySym k;
	unsigned int mask;
	char *s;
	signed char appkey;
	signed char appcursor;
} Key;

/* Selection type for rectangular selection */
#define SEL_RECTANGULAR 1

/* Appearance — full Fontconfig pattern (same style as st config.h "font") */
static char *font __attribute__((unused)) = "undefined medium:pixelsize=12:antialias=true:autohint=true";
extern char *opt_font;
static int borderpx __attribute__((unused)) = 2;
extern double usedfontsize;
extern double defaultfontsize;

/* Shell and execution */
static char *shell __attribute__((unused)) = "/bin/bash";
extern char *utmp;
extern char *scroll;
extern char *stty_args;

/* Identification sequence for DA and DECID */
extern char *vtiden;

/* Kerning multipliers */
static float cwscale __attribute__((unused)) = 1.0f;
static float chscale __attribute__((unused)) = 1.0f;

/* Word delimiters for selection snap */
static wchar_t *worddelimiters __attribute__((unused)) = L" ";

/* Selection timeouts (ms) */
static unsigned int doubleclicktimeout __attribute__((unused)) = 300;
static unsigned int tripleclicktimeout __attribute__((unused)) = 600;

/* Alt screen */
extern int allowaltscreen;

/* Allow OSC 52 clipboard (insecure) */
extern int allowwindowops;

/* Draw latency (ms) */
static double minlatency __attribute__((unused)) = 2;
static double maxlatency __attribute__((unused)) = 33;

/* Blink timeout (0 = disable) */
static unsigned int blinktimeout __attribute__((unused)) = 800;

/* Cursor thickness */
static unsigned int cursorthickness __attribute__((unused)) = 2;

/* Bell volume (-100 to 100, 0 = off) */
static int bellvolume __attribute__((unused)) = 0;

/* Default TERM */
extern char *termname;

/* Tab spaces */
extern unsigned int tabspaces;

/* Default cols/rows (overridden by -g, defined in main.c) */
extern unsigned int cols;
extern unsigned int rows;

/* Terminal colors */
static const char *colorname[] __attribute__((unused)) = {
	"black", "red3", "green3", "yellow3", "blue2", "magenta3", "cyan3", "gray90",
	"gray50", "red", "green", "yellow", "#5c5cff", "magenta", "cyan", "white",
	[255] = 0,
	"#cccccc", "#555555", "gray90", "black",
};

/* Default color indices: fg, bg, cursor, reverse cursor */
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern unsigned int defaultcs;
static unsigned int defaultrcs __attribute__((unused)) = 257;

/* Cursor shape: 0-2 block, 3-4 underline, 5-6 bar, 7 snowman */
static unsigned int cursorshape __attribute__((unused)) = 2;

/* Mouse cursor */
static unsigned int mouseshape __attribute__((unused)) = 0; /* XC_xterm */
static unsigned int mousefg __attribute__((unused)) = 7;
static unsigned int mousebg __attribute__((unused)) = 0;

#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

/* Config callback declarations - implemented in input.c */
void clipcopy(const Arg *);
void clippaste(const Arg *);
void selpaste(const Arg *);
void zoom(const Arg *);
void zoomreset(const Arg *);
void sendbreak(const Arg *);
void numlock(const Arg *);
void ttysend(const Arg *);

/* Config array sizes */
#define LEN(a) (sizeof(a) / sizeof(a)[0])

#endif /* CONFIG_H */
