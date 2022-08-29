#!/usr/bin/env bash

PROGRAM_FILE="$srcdir/res/CZ-101 program.syx"
PANEL_FILE="CZ-101 panel.syx"
PRESET_FILE="CZ-101 preset 01.syx"

function exitWithError() {
  rm -f "$PANEL_FILE" "$PRESET_FILE"
  exit $1
}

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

echo "Testing ls..."
files=$($ecli cz-program-ls $DEVICE:/)
[ $? -ne 0 ] && exit 1
expected="D   -1B preset
D   -1B internal
F  264B panel"
[ "$files" != "$expected" ] && echo "Tests will fail an inserted cartridge" && exit 1

echo "Testing upload bad file..."
$ecli cz-program-ul foo $DEVICE:/1:a
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload bad destination..."
$ecli cz-program-ul foo $DEVICE:/1
[ $? -eq 0 ] && exitWithError 1

echo "Testing panel upload..."
$ecli cz-program-ul "$PROGRAM_FILE" $DEVICE:/panel:panel
[ $? -ne 0 ] && exitWithError 1

echo "Testing panel download..."
$ecli cz-program-download $DEVICE:/panel
[ $? -ne 0 ] && exit 1
[ ! -f "$PANEL_FILE" ] && exit 1
[ $(cksum "$PANEL_FILE" | awk '{print $1}') != $(cksum "$PROGRAM_FILE" | awk '{print $1}') ] && exitWithError 1

echo "Testing preset upload..."
$ecli cz-program-ul "$PROGRAM_FILE" $DEVICE:/preset/1:panel
[ $? -ne 0 ] && exitWithError 1

echo "Testing preset download..."
$ecli cz-program-download $DEVICE:/preset/1
[ $? -ne 0 ] && exit 1
[ ! -f "$PRESET_FILE" ] && exit 1

exitWithError 0