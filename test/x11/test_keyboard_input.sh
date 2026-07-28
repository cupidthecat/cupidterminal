#!/usr/bin/env bash

set -euo pipefail

for command in Xvfb xdotool od; do
    command -v "$command" >/dev/null || {
        echo "SKIP: x11/keyboard_input ($command unavailable)"
        exit 0
    }
done

test_dir=$(mktemp -d)
display_name=:97
xvfb_pid=
terminal_pid=

cleanup() {
    if [[ -n "$terminal_pid" ]]; then kill "$terminal_pid" 2>/dev/null || true; fi
    if [[ -n "$xvfb_pid" ]]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT

Xvfb "$display_name" -screen 0 1024x768x24 >"$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
export DISPLAY=$display_name
sleep 0.2

CUPID_KEY_OUT="$test_dir/keys.bin" ./cupidterminal \
    -T cupid-key-test -e sh -c \
    'stty raw -echo; printf "\033]2;cupid-key-ready\007"; dd bs=1 count=24 status=none >"$CUPID_KEY_OUT"; sleep 0.3' \
    >"$test_dir/terminal.log" 2>&1 &
terminal_pid=$!

cupid_window=
for _ in $(seq 1 50); do
    cupid_window=$(xdotool search --name cupid-key-ready 2>/dev/null | head -1 || true)
    if [[ -n "$cupid_window" ]]; then break; fi
    sleep 0.1
done
[[ -n "$cupid_window" ]] || {
    echo "TEST FAILURE: keyboard fixture did not map" >&2
    exit 1
}

xdotool windowfocus "$cupid_window"
xdotool type --window "$cupid_window" --clearmodifiers --delay 5 'Ab1![];,.?/'
xdotool key --window "$cupid_window" --clearmodifiers BackSpace Return Home Up F1
xdotool key --window "$cupid_window" --clearmodifiers dead_acute e

for _ in $(seq 1 50); do
    if ! kill -0 "$terminal_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if kill -0 "$terminal_pid" 2>/dev/null; then
    echo "TEST FAILURE: terminal did not deliver the expected key bytes" >&2
    exit 1
fi
wait "$terminal_pid"
terminal_pid=

actual=$(od -An -tx1 "$test_dir/keys.bin" | tr -d ' \n')
expected=416231215b5d3b2c2e3f2f7f0d1b5b481b5b411b4f50c3a9
[[ "$actual" == "$expected" ]] || {
    echo "TEST FAILURE: expected $expected, got $actual" >&2
    exit 1
}

echo "PASS: x11/keyboard_input"
