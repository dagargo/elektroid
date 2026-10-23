#!/bin/sh
# Offline argument validation; never pass a device index.
set -eu
CLI="${1:-../src/elektroid-cli}"
output=$("$CLI" 2>&1 || true)
echo "$output" | grep -F 'dt-session' >/dev/null
for arguments in '' '0 extra'; do
    if output=$("$CLI" dt-session $arguments 2>&1); then
        echo 'Expected invalid session arguments to fail' >&2
        exit 1
    fi
    echo "$output" | grep -F 'Exactly one device is required' >/dev/null
done
