#!/usr/bin/env bash

#root and panel id (1 based)
#The cksum will not match because the downloaded panel data will not match the preset used for tests
$srcdir/integration/generic_fs_tests.sh cz program / 3 "" /97 ""
#No error check

$srcdir/integration/generic_fs_tests.sh cz program /internal 16 "/internal/0 /internal/17" /internal/16 ""

exit $?
