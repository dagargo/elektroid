#!/usr/bin/env bash

#root and panel id
#The cksum will not match because the downloaded panel data will not match the preset used for tests
$srcdir/integration/generic_fs_tests.sh phatty preset / 2 "" /256 ""
#No error check

$srcdir/integration/generic_fs_tests.sh phatty preset /presets 100 /presets/127 /presets/99 "New Name"

exit $?
