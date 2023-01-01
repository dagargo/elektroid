#!/usr/bin/env bash

#There are different files because of the ID stored in them.
#We overwrite these IDs while uploading but it's easier to have different files to run cksum on them.

PANEL_SRC_FILE="$srcdir/res/connectors/CZ-101 panel src.syx"
INTERNAL_SRC_FILE="$srcdir/res/connectors/CZ-101 internal 01 src.syx"
PANEL_FILE="CZ-101 panel.syx"
PRESET_FILE="CZ-101 preset 01.syx"
INTERNAL_FILE="CZ-101 internal 01.syx"
INTERNAL_FILE_BACKUP="CZ-101 internal 01 backup.syx"

function exitWithError() {
  echo "Restoring..."
  $ecli cz-program-ul "$INTERNAL_FILE_BACKUP" $TEST_DEVICE:/internal/1:foo
  rm -f "$PANEL_FILE" "$PRESET_FILE" "$INTERNAL_FILE" "$INTERNAL_FILE_BACKUP"
  exit $1
}

echo "Testing ls..."
files=$($ecli cz-program-ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
expected="D   -1B       4096 preset
D   -1B       4097 internal
F  264B         96 panel"
[ "$files" != "$expected" ] && echo "Tests will fail with a cartridge inserted" && exit 1

echo "Testing internal download..."
$ecli cz-program-download $TEST_DEVICE:/internal/1 # No name is allowed in the CZ filesystem (slot mode)
[ $? -ne 0 ] && exit 1
[ ! -f "$INTERNAL_FILE" ] && exit 1
mv "$INTERNAL_FILE" "$INTERNAL_FILE_BACKUP"

echo "Testing upload bad file..."
$ecli cz-program-ul foo $TEST_DEVICE:/1:a
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload bad destination..."
$ecli cz-program-ul foo $TEST_DEVICE:/1
[ $? -eq 0 ] && exitWithError 1

echo "Testing panel upload..."
$ecli cz-program-ul "$PANEL_SRC_FILE" $TEST_DEVICE:/panel:panel
[ $? -ne 0 ] && exitWithError 1

echo "Testing panel download..."
$ecli cz-program-download $TEST_DEVICE:/panel
[ $? -ne 0 ] && exitWithError 1
[ ! -f "$PANEL_FILE" ] && exitWithError 1
[ $(cksum "$PANEL_FILE" | awk '{print $1}') != $(cksum "$PANEL_SRC_FILE" | awk '{print $1}') ] && exitWithError 1

echo "Testing preset download..."
$ecli cz-program-download $TEST_DEVICE:/preset/1 # No name is allowed in the CZ filesystem (slot mode)
[ $? -ne 0 ] && exitWithError 1
[ ! -f "$PRESET_FILE" ] && exitWithError 1

echo "Testing internal upload..."
$ecli cz-program-ul "$INTERNAL_SRC_FILE" $TEST_DEVICE:/internal/1:bar
[ $? -ne 0 ] && exitWithError 1

$ecli cz-program-download $TEST_DEVICE:/internal/1
[ $? -ne 0 ] && exitWithError 1
[ $(cksum "$INTERNAL_FILE" | awk '{print $1}') != $(cksum "$INTERNAL_SRC_FILE" | awk '{print $1}') ] && exitWithError 1

exitWithError 0
