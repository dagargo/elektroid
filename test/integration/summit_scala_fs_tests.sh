#!/usr/bin/env bash

FILE="$srcdir/Novation Summit tuning 016.syx"

rm "$FILE"

$srcdir/integration/generic_fs_tests.sh --no-download summit scala / 17 /17 /16 ""

echo "Testing download scala as tuning..."
$ecli summit-tuning-dl $TEST_DEVICE:/16
[ $? -ne 0 ] && exit 1
[ ! -f "$FILE" ] && exit 1
[ $(cksum "$FILE" | awk '{print $1}') != $(cksum $srcdir/res/connectors/summit_tuning.data.back | awk '{print $1}') ] && exitWithError 1
rm "$FILE"

exit $?
