#!/usr/bin/env bash

$SRCDIR/integration/generic_fs_tests.sh microfreak ppreset / 512 "/0 /513" /512 "New Name"

exit $?
