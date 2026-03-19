#!/usr/bin/env bash
# Compatibility wrapper for manual suite location.

set -euo pipefail

exec "$(dirname "$0")/manual/color-test.sh" "$@"
