#!/usr/bin/env bash

export ELEKTROID_ELEKTRON_JSON=$srcdir/res/devices.json

TEST_NAME=auto-test

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

echo "Testing info..."
$ecli info $DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing df..."
$ecli df $DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing ls..."
$ecli elektron-sample-ls $DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing mkdir..."
$ecli elektron-sample-mkdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

$ecli elektron-sample-ls $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
$ecli elektron-sample-ul $srcdir/res/square.wav $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload (loop)..."
$ecli elektron-sample-ul $srcdir/res/square_loop.wav $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

output=$($ecli elektron-sample-ls $DEVICE:/$TEST_NAME)
type=$(echo "$output" | head -n 1 | awk '{print $1}')
size=$(echo "$output" | head -n 1 | awk '{print $2}')
name=$(echo "$output" | head -n 1 | awk '{print $4}')
[ "$type" != "F" ] || [ "$size" != "93.81KiB" ] || [ "$name" != "square" ] && exit 1

echo "Testing upload (nonexistent source)..."
$ecli elektron-sample-ul-sample $srcdir/res/foo $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$ecli elektron-sample-download $DEVICE:/$TEST_NAME/square
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square.wav | awk '{print $1}')"
rm square.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/square.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (loop)..."
$ecli elektron-sample-dl $DEVICE:/$TEST_NAME/square_loop
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square_loop.wav | awk '{print $1}')"
rm square_loop.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/square_loop.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (nonexistent source)..."
$ecli elektron-sample-dl $DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

echo "Testing mv..."
$ecli elektron-sample-mv $DEVICE:/$TEST_NAME/square $DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing mv..."
$ecli elektron-sample-mv $DEVICE:/$TEST_NAME/foo $DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rm..."
$ecli elektron-sample-rm $DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing rm (nonexistent file)..."
$ecli elektron-sample-rm $DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rmdir..."
$ecli elektron-sample-rmdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing rmdir (nonexistent dir)..."
$ecli elektron-sample-rmdir $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing recursive mkdir..."
$ecli elektron-sample-mkdir $DEVICE:/$TEST_NAME/foo
[ $? -ne 0 ] && exit 1

echo "Testing recursive rmdir..."
$ecli elektron-sample-rmdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing ls (nonexistent dir)..."
$ecli elektron-sample-ls $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing ls (nonexistent dir inside nonexistent dir)..."
$ecli elektron-sample-ls $DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

exit 0
