#!/usr/bin/env bash

$srcdir/integration/generic_fs_tests.sh microfreak preset / 512 "/0 /513" /512 "" "New Name"

exit $?
