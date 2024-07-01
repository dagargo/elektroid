#!/usr/bin/env bash

FILE_RESTORE_NAME=$(elektroid-cli microfreak-wavetable-ls $TEST_DEVICE:/ | tail -n 1 | awk '{$1=$2=$3=""; print $0}' | xargs)

$srcdir/integration/generic_fs_tests.sh microfreak wavetable / 16 "/0 /17" /16 "Name" "New Name" "$FILE_RESTORE_NAME"

exit $?
