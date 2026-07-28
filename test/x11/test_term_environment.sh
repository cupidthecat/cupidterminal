#!/usr/bin/env bash

set -euo pipefail

for command in Xvfb infocmp; do
    command -v "$command" >/dev/null || {
        echo "SKIP: x11/term_environment ($command unavailable)"
        exit 0
    }
done

test_dir=$(mktemp -d)
display_name=:96
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

for _ in $(seq 1 50); do
    if xdpyinfo >/dev/null 2>&1; then break; fi
    sleep 0.1
done

CUPID_TERM_OUT="$test_dir/term.txt" ./cupidterminal \
    -e sh -c 'printf "%s" "$TERM" >"$CUPID_TERM_OUT"' \
    >"$test_dir/terminal.log" 2>&1 &
terminal_pid=$!

for _ in $(seq 1 50); do
    if ! kill -0 "$terminal_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if kill -0 "$terminal_pid" 2>/dev/null; then
    echo "TEST FAILURE: terminal child did not exit" >&2
    exit 1
fi
wait "$terminal_pid"
terminal_pid=

actual=$(<"$test_dir/term.txt")
[[ "$actual" == "st-256color" ]] || {
    echo "TEST FAILURE: expected TERM=st-256color, got TERM=$actual" >&2
    exit 1
}
infocmp "$actual" >/dev/null || {
    echo "TEST FAILURE: TERM=$actual has no installed terminfo entry" >&2
    exit 1
}

echo "PASS: x11/term_environment"
