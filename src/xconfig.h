/* xconfig.h — X11-aware config view, included only by x.c.
 *
 * This header brings in X11 headers, then the non-X11 settings from config.h,
 * then defines the X-modifier macros, struct types, and static keymap tables
 * that only the X11 layer (x.c) ever uses.  st.c must NEVER include this file.
 */
#ifndef XCONFIG_H
#define XCONFIG_H

#include <limits.h>  /* UINT_MAX, used in XK_ANY_MOD below */

/* X11 includes — kept out of st.c by virtue of being in this header
   instead of config.h. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/* Pull in the non-X11 config first. */
#include "config.h"

/* X modifier defines used by the keymap tables below. */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1u<<13|1u<<14)

/* Type definitions needed by the keymap tables. */
typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
	const char *s;
} Arg;

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

/* Selection type for rectangular selection (also defined in st.h) */
#ifndef SEL_RECTANGULAR
#define SEL_RECTANGULAR 1
#endif

/* Config callback declarations - implemented in input.c / x.c */
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

/* Internal keyboard shortcuts. */
#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

/*
 * Internal mouse shortcuts.
 * Beware that overloading Button1 will disable the selection.
 */
static MouseShortcut mshortcuts[] __attribute__((unused)) = {
	{ XK_ANY_MOD,   2, selpaste, {.i = 0}, 1 },
	{ ShiftMask,    4, ttysend,  {.s = "\033[5;2~"}, 0 },
	{ ShiftMask,    5, ttysend,  {.s = "\033[6;2~"}, 0 },
};

static Shortcut shortcuts[] __attribute__((unused)) = {
	{ XK_ANY_MOD,     XK_Break,       sendbreak,   {.i = 0} },
	{ TERMMOD,        XK_Prior,       zoom,        {.f = +1} },
	{ TERMMOD,        XK_Next,        zoom,        {.f = -1} },
	{ TERMMOD,        XK_Home,        zoomreset,   {.f = 0} },
	{ TERMMOD,        XK_C,           clipcopy,    {.i = 0} },
	{ TERMMOD,        XK_V,           clippaste,   {.i = 0} },
	{ TERMMOD,        XK_Y,           selpaste,    {.i = 0} },
	{ ShiftMask,      XK_Insert,      selpaste,    {.i = 0} },
	{ TERMMOD,        XK_Num_Lock,    numlock,     {.i = 0} },
};

/*
 * If you want keys other than the X11 function keys (0xFD00 - 0xFFFF)
 * to be mapped below, add them to this array.
 */
static KeySym mappedkeys[] __attribute__((unused)) = { (KeySym)-1 };

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (Mod2Mask) and keyboard layout (XK_SWITCH_MOD) are ignored.
 */
static unsigned int ignoremod __attribute__((unused)) = Mod2Mask | XK_SWITCH_MOD;

/*
 * Force mouse select/shortcuts while mask is active (when MODE_MOUSE is set).
 * Note that if you want to use ShiftMask with selmasks, set this to an other
 * modifier, set to 0 to not use it.
 */
static unsigned int forcemousemod __attribute__((unused)) = ShiftMask;

/*
 * This is the huge key array which defines all compatibility to the Linux
 * world. Please decide about changes wisely.
 */
static Key key[] __attribute__((unused)) = {
	{ XK_KP_Home,       ShiftMask,      "\033[2J",       0,   -1},
	{ XK_KP_Home,       ShiftMask,      "\033[1;2H",     0,   +1},
	{ XK_KP_Home,       XK_ANY_MOD,     "\033[H",        0,   -1},
	{ XK_KP_Home,       XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033Ox",       +1,    0},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033[A",        0,   -1},
	{ XK_KP_Up,         XK_ANY_MOD,     "\033OA",        0,   +1},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033Or",       +1,    0},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033[B",        0,   -1},
	{ XK_KP_Down,       XK_ANY_MOD,     "\033OB",        0,   +1},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033Ot",       +1,    0},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033[D",        0,   -1},
	{ XK_KP_Left,       XK_ANY_MOD,     "\033OD",        0,   +1},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033Ov",       +1,    0},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033[C",        0,   -1},
	{ XK_KP_Right,      XK_ANY_MOD,     "\033OC",        0,   +1},
	{ XK_KP_Prior,      ShiftMask,      "\033[5;2~",     0,    0},
	{ XK_KP_Prior,      XK_ANY_MOD,     "\033[5~",       0,    0},
	{ XK_KP_Begin,      XK_ANY_MOD,     "\033[E",        0,    0},
	{ XK_KP_End,        ControlMask,    "\033[J",       -1,    0},
	{ XK_KP_End,        ControlMask,    "\033[1;5F",    +1,    0},
	{ XK_KP_End,        ShiftMask,      "\033[K",       -1,    0},
	{ XK_KP_End,        ShiftMask,      "\033[1;2F",    +1,    0},
	{ XK_KP_End,        XK_ANY_MOD,     "\033[4~",       0,    0},
	{ XK_KP_Next,       ShiftMask,      "\033[6;2~",     0,    0},
	{ XK_KP_Next,       XK_ANY_MOD,     "\033[6~",       0,    0},
	{ XK_KP_Insert,     ShiftMask,      "\033[2;2~",    +1,    0},
	{ XK_KP_Insert,     ShiftMask,      "\033[4l",      -1,    0},
	{ XK_KP_Insert,     ControlMask,    "\033[L",       -1,    0},
	{ XK_KP_Insert,     ControlMask,    "\033[2;5~",    +1,    0},
	{ XK_KP_Insert,     XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ XK_KP_Insert,     XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ XK_KP_Delete,     ControlMask,    "\033[M",       -1,    0},
	{ XK_KP_Delete,     ControlMask,    "\033[3;5~",    +1,    0},
	{ XK_KP_Delete,     ShiftMask,      "\033[2K",      -1,    0},
	{ XK_KP_Delete,     ShiftMask,      "\033[3;2~",    +1,    0},
	{ XK_KP_Delete,     XK_ANY_MOD,     "\033[P",       -1,    0},
	{ XK_KP_Delete,     XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ XK_KP_Multiply,   XK_ANY_MOD,     "\033Oj",       +2,    0},
	{ XK_KP_Add,        XK_ANY_MOD,     "\033Ok",       +2,    0},
	{ XK_KP_Enter,      XK_ANY_MOD,     "\033OM",       +2,    0},
	{ XK_KP_Enter,      XK_ANY_MOD,     "\r",           -1,    0},
	{ XK_KP_Subtract,   XK_ANY_MOD,     "\033Om",       +2,    0},
	{ XK_KP_Decimal,    XK_ANY_MOD,     "\033On",       +2,    0},
	{ XK_KP_Divide,     XK_ANY_MOD,     "\033Oo",       +2,    0},
	{ XK_KP_0,          XK_ANY_MOD,     "\033Op",       +2,    0},
	{ XK_KP_1,          XK_ANY_MOD,     "\033Oq",       +2,    0},
	{ XK_KP_2,          XK_ANY_MOD,     "\033Or",       +2,    0},
	{ XK_KP_3,          XK_ANY_MOD,     "\033Os",       +2,    0},
	{ XK_KP_4,          XK_ANY_MOD,     "\033Ot",       +2,    0},
	{ XK_KP_5,          XK_ANY_MOD,     "\033Ou",       +2,    0},
	{ XK_KP_6,          XK_ANY_MOD,     "\033Ov",       +2,    0},
	{ XK_KP_7,          XK_ANY_MOD,     "\033Ow",       +2,    0},
	{ XK_KP_8,          XK_ANY_MOD,     "\033Ox",       +2,    0},
	{ XK_KP_9,          XK_ANY_MOD,     "\033Oy",       +2,    0},
	{ XK_Up,            ShiftMask,      "\033[1;2A",     0,    0},
	{ XK_Up,            Mod1Mask,       "\033[1;3A",     0,    0},
	{ XK_Up,         ShiftMask|Mod1Mask,"\033[1;4A",     0,    0},
	{ XK_Up,            ControlMask,    "\033[1;5A",     0,    0},
	{ XK_Up,      ShiftMask|ControlMask,"\033[1;6A",     0,    0},
	{ XK_Up,       ControlMask|Mod1Mask,"\033[1;7A",     0,    0},
	{ XK_Up,ShiftMask|ControlMask|Mod1Mask,"\033[1;8A",  0,    0},
	{ XK_Up,            XK_ANY_MOD,     "\033[A",        0,   -1},
	{ XK_Up,            XK_ANY_MOD,     "\033OA",        0,   +1},
	{ XK_Down,          ShiftMask,      "\033[1;2B",     0,    0},
	{ XK_Down,          Mod1Mask,       "\033[1;3B",     0,    0},
	{ XK_Down,       ShiftMask|Mod1Mask,"\033[1;4B",     0,    0},
	{ XK_Down,          ControlMask,    "\033[1;5B",     0,    0},
	{ XK_Down,    ShiftMask|ControlMask,"\033[1;6B",     0,    0},
	{ XK_Down,     ControlMask|Mod1Mask,"\033[1;7B",     0,    0},
	{ XK_Down,ShiftMask|ControlMask|Mod1Mask,"\033[1;8B",0,    0},
	{ XK_Down,          XK_ANY_MOD,     "\033[B",        0,   -1},
	{ XK_Down,          XK_ANY_MOD,     "\033OB",        0,   +1},
	{ XK_Left,          ShiftMask,      "\033[1;2D",     0,    0},
	{ XK_Left,          Mod1Mask,       "\033[1;3D",     0,    0},
	{ XK_Left,       ShiftMask|Mod1Mask,"\033[1;4D",     0,    0},
	{ XK_Left,          ControlMask,    "\033[1;5D",     0,    0},
	{ XK_Left,    ShiftMask|ControlMask,"\033[1;6D",     0,    0},
	{ XK_Left,     ControlMask|Mod1Mask,"\033[1;7D",     0,    0},
	{ XK_Left,ShiftMask|ControlMask|Mod1Mask,"\033[1;8D",0,    0},
	{ XK_Left,          XK_ANY_MOD,     "\033[D",        0,   -1},
	{ XK_Left,          XK_ANY_MOD,     "\033OD",        0,   +1},
	{ XK_Right,         ShiftMask,      "\033[1;2C",     0,    0},
	{ XK_Right,         Mod1Mask,       "\033[1;3C",     0,    0},
	{ XK_Right,      ShiftMask|Mod1Mask,"\033[1;4C",     0,    0},
	{ XK_Right,         ControlMask,    "\033[1;5C",     0,    0},
	{ XK_Right,   ShiftMask|ControlMask,"\033[1;6C",     0,    0},
	{ XK_Right,    ControlMask|Mod1Mask,"\033[1;7C",     0,    0},
	{ XK_Right,ShiftMask|ControlMask|Mod1Mask,"\033[1;8C",0,   0},
	{ XK_Right,         XK_ANY_MOD,     "\033[C",        0,   -1},
	{ XK_Right,         XK_ANY_MOD,     "\033OC",        0,   +1},
	{ XK_ISO_Left_Tab,  ShiftMask,      "\033[Z",        0,    0},
	{ XK_Return,        Mod1Mask,       "\033\r",        0,    0},
	{ XK_Return,        XK_ANY_MOD,     "\r",            0,    0},
	{ XK_Insert,        ShiftMask,      "\033[4l",      -1,    0},
	{ XK_Insert,        ShiftMask,      "\033[2;2~",    +1,    0},
	{ XK_Insert,        ControlMask,    "\033[L",       -1,    0},
	{ XK_Insert,        ControlMask,    "\033[2;5~",    +1,    0},
	{ XK_Insert,        XK_ANY_MOD,     "\033[4h",      -1,    0},
	{ XK_Insert,        XK_ANY_MOD,     "\033[2~",      +1,    0},
	{ XK_Delete,        ControlMask,    "\033[M",       -1,    0},
	{ XK_Delete,        ControlMask,    "\033[3;5~",    +1,    0},
	{ XK_Delete,        ShiftMask,      "\033[2K",      -1,    0},
	{ XK_Delete,        ShiftMask,      "\033[3;2~",    +1,    0},
	{ XK_Delete,        XK_ANY_MOD,     "\033[P",       -1,    0},
	{ XK_Delete,        XK_ANY_MOD,     "\033[3~",      +1,    0},
	{ XK_BackSpace,     XK_NO_MOD,      "\177",          0,    0},
	{ XK_BackSpace,     Mod1Mask,       "\033\177",      0,    0},
	{ XK_Home,          ShiftMask,      "\033[2J",       0,   -1},
	{ XK_Home,          ShiftMask,      "\033[1;2H",     0,   +1},
	{ XK_Home,          XK_ANY_MOD,     "\033[H",        0,   -1},
	{ XK_Home,          XK_ANY_MOD,     "\033[1~",       0,   +1},
	{ XK_End,           ControlMask,    "\033[J",       -1,    0},
	{ XK_End,           ControlMask,    "\033[1;5F",    +1,    0},
	{ XK_End,           ShiftMask,      "\033[K",       -1,    0},
	{ XK_End,           ShiftMask,      "\033[1;2F",    +1,    0},
	{ XK_End,           XK_ANY_MOD,     "\033[4~",       0,    0},
	{ XK_Prior,         ControlMask,    "\033[5;5~",     0,    0},
	{ XK_Prior,         ShiftMask,      "\033[5;2~",     0,    0},
	{ XK_Prior,         XK_ANY_MOD,     "\033[5~",       0,    0},
	{ XK_Next,          ControlMask,    "\033[6;5~",     0,    0},
	{ XK_Next,          ShiftMask,      "\033[6;2~",     0,    0},
	{ XK_Next,          XK_ANY_MOD,     "\033[6~",       0,    0},
	{ XK_F1,            XK_NO_MOD,      "\033OP" ,       0,    0},
	{ XK_F1,            ShiftMask,      "\033[1;2P",     0,    0},
	{ XK_F1,            ControlMask,    "\033[1;5P",     0,    0},
	{ XK_F1,            Mod4Mask,       "\033[1;6P",     0,    0},
	{ XK_F1,            Mod1Mask,       "\033[1;3P",     0,    0},
	{ XK_F1,            Mod3Mask,       "\033[1;4P",     0,    0},
	{ XK_F2,            XK_NO_MOD,      "\033OQ" ,       0,    0},
	{ XK_F2,            ShiftMask,      "\033[1;2Q",     0,    0},
	{ XK_F2,            ControlMask,    "\033[1;5Q",     0,    0},
	{ XK_F2,            Mod4Mask,       "\033[1;6Q",     0,    0},
	{ XK_F2,            Mod1Mask,       "\033[1;3Q",     0,    0},
	{ XK_F2,            Mod3Mask,       "\033[1;4Q",     0,    0},
	{ XK_F3,            XK_NO_MOD,      "\033OR" ,       0,    0},
	{ XK_F3,            ShiftMask,      "\033[1;2R",     0,    0},
	{ XK_F3,            ControlMask,    "\033[1;5R",     0,    0},
	{ XK_F3,            Mod4Mask,       "\033[1;6R",     0,    0},
	{ XK_F3,            Mod1Mask,       "\033[1;3R",     0,    0},
	{ XK_F3,            Mod3Mask,       "\033[1;4R",     0,    0},
	{ XK_F4,            XK_NO_MOD,      "\033OS" ,       0,    0},
	{ XK_F4,            ShiftMask,      "\033[1;2S",     0,    0},
	{ XK_F4,            ControlMask,    "\033[1;5S",     0,    0},
	{ XK_F4,            Mod4Mask,       "\033[1;6S",     0,    0},
	{ XK_F4,            Mod1Mask,       "\033[1;3S",     0,    0},
	{ XK_F5,            XK_NO_MOD,      "\033[15~",      0,    0},
	{ XK_F5,            ShiftMask,      "\033[15;2~",    0,    0},
	{ XK_F5,            ControlMask,    "\033[15;5~",    0,    0},
	{ XK_F5,            Mod4Mask,       "\033[15;6~",    0,    0},
	{ XK_F5,            Mod1Mask,       "\033[15;3~",    0,    0},
	{ XK_F6,            XK_NO_MOD,      "\033[17~",      0,    0},
	{ XK_F6,            ShiftMask,      "\033[17;2~",    0,    0},
	{ XK_F6,            ControlMask,    "\033[17;5~",    0,    0},
	{ XK_F6,            Mod4Mask,       "\033[17;6~",    0,    0},
	{ XK_F6,            Mod1Mask,       "\033[17;3~",    0,    0},
	{ XK_F7,            XK_NO_MOD,      "\033[18~",      0,    0},
	{ XK_F7,            ShiftMask,      "\033[18;2~",    0,    0},
	{ XK_F7,            ControlMask,    "\033[18;5~",    0,    0},
	{ XK_F7,            Mod4Mask,       "\033[18;6~",    0,    0},
	{ XK_F7,            Mod1Mask,       "\033[18;3~",    0,    0},
	{ XK_F8,            XK_NO_MOD,      "\033[19~",      0,    0},
	{ XK_F8,            ShiftMask,      "\033[19;2~",    0,    0},
	{ XK_F8,            ControlMask,    "\033[19;5~",    0,    0},
	{ XK_F8,            Mod4Mask,       "\033[19;6~",    0,    0},
	{ XK_F8,            Mod1Mask,       "\033[19;3~",    0,    0},
	{ XK_F9,            XK_NO_MOD,      "\033[20~",      0,    0},
	{ XK_F9,            ShiftMask,      "\033[20;2~",    0,    0},
	{ XK_F9,            ControlMask,    "\033[20;5~",    0,    0},
	{ XK_F9,            Mod4Mask,       "\033[20;6~",    0,    0},
	{ XK_F9,            Mod1Mask,       "\033[20;3~",    0,    0},
	{ XK_F10,           XK_NO_MOD,      "\033[21~",      0,    0},
	{ XK_F10,           ShiftMask,      "\033[21;2~",    0,    0},
	{ XK_F10,           ControlMask,    "\033[21;5~",    0,    0},
	{ XK_F10,           Mod4Mask,       "\033[21;6~",    0,    0},
	{ XK_F10,           Mod1Mask,       "\033[21;3~",    0,    0},
	{ XK_F11,           XK_NO_MOD,      "\033[23~",      0,    0},
	{ XK_F11,           ShiftMask,      "\033[23;2~",    0,    0},
	{ XK_F11,           ControlMask,    "\033[23;5~",    0,    0},
	{ XK_F11,           Mod4Mask,       "\033[23;6~",    0,    0},
	{ XK_F11,           Mod1Mask,       "\033[23;3~",    0,    0},
	{ XK_F12,           XK_NO_MOD,      "\033[24~",      0,    0},
	{ XK_F12,           ShiftMask,      "\033[24;2~",    0,    0},
	{ XK_F12,           ControlMask,    "\033[24;5~",    0,    0},
	{ XK_F12,           Mod4Mask,       "\033[24;6~",    0,    0},
	{ XK_F12,           Mod1Mask,       "\033[24;3~",    0,    0},
};

/*
 * Selection types' masks.
 * Use the same masks as usual.
 * Button1Mask is always unset, to make masks match between ButtonPress.
 * ButtonRelease and MotionNotify.
 * If no match is found, regular selection is used.
 */
static unsigned int selmasks[] __attribute__((unused)) = { [SEL_RECTANGULAR] = Mod1Mask };

#endif /* XCONFIG_H */
