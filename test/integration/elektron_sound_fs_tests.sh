#!/usr/bin/env bash

$ecli elektron-data-ul $srcdir/res/connectors/SOUND.dtdata $TEST_DEVICE:/soundbanks/H/256

$srcdir/integration/generic_fs_tests.sh elektron sound /H 256 "/H/0 /H/257" /H/256 "" ""

$ecli elektron-data-rm $TEST_DEVICE:/soundbanks/H/256

rm $srcdir/SOUND.dtsnd

exit $?
