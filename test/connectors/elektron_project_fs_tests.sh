#!/usr/bin/env bash

$ecli elektron-data-ul $srcdir/res/connectors/PROJECT.dtdata $TEST_DEVICE:/projects/128

$srcdir/connectors/generic_fs_tests.sh elektron project / 128 "/0 /129" /128 "" ""

$ecli elektron-data-rm $TEST_DEVICE:/soundbanks/H/256

exit $?
