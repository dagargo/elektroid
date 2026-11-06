#!/bin/sh
# CLI smoke tests for sample metadata.
# No MIDI device is required.
set -eu

CLI="${1:-./src/elektroid-cli}"

if [ ! -x "$CLI" ]; then
    echo "Executable not found: $CLI" >&2
    exit 1
fi

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

check_failure()
{
    expected="$1"
    shift

    if "$CLI" "$@" >"$tmpdir/output" 2>&1; then
        echo "Expected command to fail: $*" >&2
        exit 1
    fi

    if ! grep -F "$expected" "$tmpdir/output" >/dev/null; then
        echo "Expected error: $expected" >&2
        cat "$tmpdir/output" >&2
        exit 1
    fi
}

# A missing argument must fail before connecting to MIDI.
check_failure "Missing sample path" \
    elektron:sample:sample-info

# A malformed device path must fail before connecting to MIDI.
check_failure "Expected device_number:path_to_file" \
    elektron:sample:sample-info 0

# The command must appear in CLI help.
"$CLI" >"$tmpdir/help" 2>&1 || true

if ! grep -F "sample-info" "$tmpdir/help" >/dev/null; then
    echo "sample-info missing from CLI help" >&2
    exit 1
fi

echo "PASS: sample metadata CLI smoke tests"
