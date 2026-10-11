# Renderer color cache

Color overrides are resolved before the Xft cache lookup. Changing or resetting
an OSC palette, foreground, background, or cursor color therefore selects the
current RGB value instead of reusing a stale indexed entry. Window background
clears use the same lookup.

The normal and faint truecolor caches each retain at most 1,024 colors. When a
cache is full, a successful new allocation replaces an older entry and frees its
Xft allocation. Previously, colors beyond that limit fell back to the default
foreground or background. A failed allocation still uses the existing fallback.

Replacement preserves the immediately preceding lookup: background-run batching
and cursor drawing can hold that pointer while requesting the next color.

`make test-render` runs the real cache code with a deterministic color allocator.
It checks palette changes and resets, dynamic colors, both normal and faint
cache saturation, bounded allocations, and a forced hash collision while the
previous color is still held. It does not require a display server. The regular
X11 integration tests remain part of `make test`.
