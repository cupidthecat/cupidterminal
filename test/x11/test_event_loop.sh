#!/usr/bin/env bash
set -euo pipefail
for command in Xvfb xdpyinfo xdotool python3; do
    command -v "$command" >/dev/null || {
        echo "SKIP: x11/event_loop ($command unavailable)"; exit 0;
    }
done
test_dir=$(mktemp -d)
xvfb_pid=
terminal_pid=
cleanup() {
    if [[ -n "$terminal_pid" ]]; then kill "$terminal_pid" 2>/dev/null || true; wait "$terminal_pid" 2>/dev/null || true; fi
    if [[ -n "$xvfb_pid" ]]; then kill "$xvfb_pid" 2>/dev/null || true; wait "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
export DISPLAY=:95
Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
for _ in $(seq 1 50); do
    if xdpyinfo >/dev/null 2>&1; then break; fi
    sleep 0.1
done
xdpyinfo >/dev/null 2>&1
wait_window() {
    local title=$1 win=
    for _ in $(seq 1 50); do
        win=$(xdotool search --onlyvisible --name "$title" 2>/dev/null | head -1 || true)
        if [[ -n "$win" ]]; then printf '%s\n' "$win"; return; fi
        sleep 0.1
    done
    cat "$test_dir/terminal.log" >&2
    echo "TEST FAILURE: $title did not map" >&2
    return 1
}
"${CC:-cc}" -Wall -Wextra -Werror -std=c99 test/x11/blink_pixels.c -o "$test_dir/blink_pixels" -lX11
./cupidterminal -g 20x4 -e sh -c 'printf "\033[?25l\033[5mBLINK\033[0m\033]2;cupid-idle-blink\007"; sleep 15' >"$test_dir/terminal.log" 2>&1 &
terminal_pid=$!
win=$(wait_window cupid-idle-blink)
sleep 0.5
"$test_dir/blink_pixels" "$win" || blink_failed=1
kill "$terminal_pid"; wait "$terminal_pid" || true
terminal_pid=
# Force both X11 and PTY descriptors beyond select's fd_set capacity.
CUPID_KEY_OUT="$test_dir/keys" python3 - <<'PY' >"$test_dir/terminal.log" 2>&1 &
import os
fds = []
while not fds or fds[-1] < 1100:
    fd = os.open('/dev/null', os.O_RDONLY)
    os.set_inheritable(fd, True)
    fds.append(fd)
os.execl('./cupidterminal', './cupidterminal', '-g', '20x4', '-e', 'sh', '-c',
         'stty raw -echo; printf "\033]2;cupid-high-fds\007"; dd bs=1 count=1 status=none >"$CUPID_KEY_OUT"; sleep 0.2')
PY
terminal_pid=$!
win=$(wait_window cupid-high-fds)
xdotool windowfocus "$win"
xdotool type --window "$win" --clearmodifiers x
for _ in $(seq 1 50); do
    if ! kill -0 "$terminal_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if kill -0 "$terminal_pid" 2>/dev/null; then echo 'TEST FAILURE: high-fd terminal hung' >&2; exit 1; fi
wait "$terminal_pid"
terminal_pid=
[[ $(cat "$test_dir/keys") == x ]]
echo 'PASS: x11/high_descriptors'
[[ ${blink_failed:-0} == 0 ]]
