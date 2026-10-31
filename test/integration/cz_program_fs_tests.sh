#!/usr/bin/env bash

#ls /
[ $($BINDIR/elektroid-cli cz:program:ls $TEST_DEVICE:/ | wc -l) -ne 3 ] && exit 1

#panel id (1 based)
$BINDIR/elektroid-cli cz:program:dl $TEST_DEVICE:/97
err=$?
rm *.syx
[ $err -ne 0 ] && exit $err

$SRCDIR/integration/generic_fs_tests.sh cz program /internal 16 "/internal/0 /internal/17" /internal/16 ""

exit $?
