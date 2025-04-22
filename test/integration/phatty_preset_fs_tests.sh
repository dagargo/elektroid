#!/usr/bin/env bash

#ls /
[ $($ecli phatty-preset-ls $TEST_DEVICE:/ | wc -l) -ne 2 ] && exit 1

#panel id
$ecli phatty-preset-dl $TEST_DEVICE:/256 && exit 1
err=$?
rm *.syx
[ $err -ne 0 ] && exit $err

$srcdir/integration/generic_fs_tests.sh phatty preset /presets 100 /presets/127 /presets/99 "New Name"

exit $?
