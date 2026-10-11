# Background colors on new blank rows

The terminfo entry advertises background color erase (`bce`). Blank rows created
by scrolling, reverse index, insert line, and delete line now use the current
foreground, background, and rendition attributes, as character erasure already
does. Previously these operations reset new rows to the default colors, leaving
visible stripes in applications with a colored background.

`test/screen/test_scroll_background.c` checks line feed at the bottom margin,
reverse index at the top, explicit scroll up/down, and insert/delete line. It
also checks that these operations leave the current drawing attributes intact.
