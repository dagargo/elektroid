#!/usr/bin/env bash

#There are different files because of the ID stored in them.
#We overwrite these IDs while uploading but it's easier to have different files to run cksum on them.

PANEL_SRC_FILE="$srcdir/res/connectors/cz_program.data"
PANEL_FILE="Casio CZ-101 program panel.syx"

function exitWithError() {
  echo "Restoring..."
  $ecli cz-program-ul "$INTERNAL_FILE_BACKUP" $TEST_DEVICE:/internal/1
  rm -f "$PANEL_FILE" "$PRESET_FILE" "$INTERNAL_FILE" "$INTERNAL_FILE_BACKUP"
  exit $1
}

echo "Testing ls..."
items=$($ecli cz-program-ls $TEST_DEVICE:/ | wc -l)
[ $? -ne 0 ] && exit 1
[ "$items" != 3 ] && echo "Tests will fail with a cartridge inserted" && exit 1

echo "Testing panel upload..."
$ecli cz-program-ul "$PANEL_SRC_FILE" $TEST_DEVICE:/panel:panel
[ $? -ne 0 ] && exitWithError 1

echo "Testing panel download..."
$ecli cz-program-download $TEST_DEVICE:/panel
[ $? -ne 0 ] && exitWithError 1
[ ! -f "$PANEL_FILE" ] && exitWithError 1
[ $(cksum "$PANEL_FILE" | awk '{print $1}') != $(cksum "$PANEL_SRC_FILE" | awk '{print $1}') ] && exitWithError 1

$srcdir/integration/generic_fs_tests.sh cz program /internal 16 "/internal/0 /internal/17" /internal/16 ""

exit $?
