#!/usr/bin/env bash

set -euo pipefail

for command in Xvfb xdpyinfo xdotool od "${CC:-cc}"; do
    command -v "$command" >/dev/null || {
        echo "SKIP: x11/focus_reporting ($command unavailable)"
        exit 0
    }
done

test_dir=$(mktemp -d)
display_name=:98
xvfb_pid=
peer_pid=
terminal_pid=

cleanup() {
    if [[ -n "$terminal_pid" ]]; then kill "$terminal_pid" 2>/dev/null || true; fi
    if [[ -n "$peer_pid" ]]; then kill "$peer_pid" 2>/dev/null || true; fi
    if [[ -n "$xvfb_pid" ]]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT

Xvfb "$display_name" -screen 0 1024x768x24 >"$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
export DISPLAY=$display_name
for _ in $(seq 1 50); do
    if xdpyinfo >/dev/null 2>&1; then break; fi
    sleep 0.1
done
xdpyinfo >/dev/null 2>&1 || { echo "TEST FAILURE: Xvfb did not start" >&2; exit 1; }

"${CC:-cc}" -Wall -Wextra -Werror -std=c99 test/x11/focus_peer.c -o "$test_dir/focus_peer" -lX11
"$test_dir/focus_peer" >"$test_dir/peer.log" 2>&1 &
peer_pid=$!

CUPID_FOCUS_OUT="$test_dir/focus.bin" ./cupidterminal \
    -T cupid-focus-test -e sh -c \
    'stty raw -echo; printf "\033[?1004h\033]2;cupid-focus-ready\007"; dd bs=3 count=2 status=none >"$CUPID_FOCUS_OUT"' \
    >"$test_dir/terminal.log" 2>&1 &
terminal_pid=$!

cupid_window=
peer_window=
for _ in $(seq 1 50); do
    cupid_window=$(xdotool search --onlyvisible --name cupid-focus-ready 2>/dev/null | head -1 || true)
    peer_window=$(xdotool search --onlyvisible --name cupid-focus-peer 2>/dev/null | head -1 || true)
    if [[ -n "$cupid_window" && -n "$peer_window" ]]; then break; fi
    sleep 0.1
done
[[ -n "$cupid_window" && -n "$peer_window" ]] || {
    cat "$test_dir/xvfb.log" "$test_dir/peer.log" "$test_dir/terminal.log" >&2
    echo "TEST FAILURE: X11 focus fixtures did not map" >&2
    exit 1
}

xdotool windowfocus "$cupid_window"
sleep 0.2
xdotool windowfocus "$peer_window"

for _ in $(seq 1 50); do
    if ! kill -0 "$terminal_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if kill -0 "$terminal_pid" 2>/dev/null; then
    echo "TEST FAILURE: terminal did not receive both focus reports" >&2
    exit 1
fi
wait "$terminal_pid"
terminal_pid=

actual=$(od -An -tx1 "$test_dir/focus.bin" | tr -d ' \n')
[[ "$actual" == "1b5b491b5b4f" ]] || {
    echo "TEST FAILURE: expected focus in/out reports, got $actual" >&2
    exit 1
}

echo "PASS: x11/focus_reporting"
