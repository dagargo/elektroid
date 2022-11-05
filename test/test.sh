#!/usr/bin/env bash

[ -z "${TEST_CONNECTOR_FILESYSTEM}" ] && echo "Environment variable TEST_CONNECTOR_FILESYSTEM not set. Nothing to run." && exit 0

./${TEST_CONNECTOR_FILESYSTEM}_fs_tests.sh

exit $?
