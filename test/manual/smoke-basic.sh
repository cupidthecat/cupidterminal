#!/usr/bin/env bash
# Manual smoke commands for terminal behavior checks.

set -euo pipefail

printf '\033[2J\033[Hhello\n'
printf '12345\rabc\n'
printf 'a\tb\n'
printf '\033[31mred\033[0m normal\n'
printf '\033[?25lHIDE\033[?25hSHOW\n'
printf '\033[?1049hALT SCREEN\033[?1049l\n'
printf '\033[?2004h\n'
