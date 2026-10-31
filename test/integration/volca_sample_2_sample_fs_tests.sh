#!/usr/bin/env bash

echo "*******************************************"
echo "* This test requires a sample in slot 199 *"
echo "*******************************************"

$SRCDIR/integration/generic_fs_tests.sh --download-ext wav volca-sample-2 sample / 200 /200 /199 ""

exit $?
