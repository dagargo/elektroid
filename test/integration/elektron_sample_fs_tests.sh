#!/usr/bin/env bash

export ELEKTROID_ELEKTRON_JSON=$srcdir/res/connectors/devices.json

TEST_NAME=auto-test

echo "Using device $TEST_DEVICE..."

echo "Testing info..."
$ecli info $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing df..."
$ecli df $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing ls..."
$ecli elektron-sample-ls $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing mkdir..."
$ecli elektron-sample-mkdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

$ecli elektron-sample-ls $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
$ecli elektron-sample-ul $srcdir/res/connectors/square.wav $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

output=$($ecli elektron-sample-ls $TEST_DEVICE:/$TEST_NAME)
type=$(echo "$output" | head -n 1 | awk '{print $1}')
size=$(echo "$output" | head -n 1 | awk '{print $2}')
name=$(echo "$output" | head -n 1 | awk '{print $4}')
[ "$type" != "F" ] || [ "$size" != "93.81KiB" ] || [ "$name" != "square" ] && exit 1

echo "Testing upload (nonexistent source)..."
$ecli elektron-sample-upload $srcdir/res/connectors/foo $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$ecli elektron-sample-download $TEST_DEVICE:/$TEST_NAME/square
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square.wav | awk '{print $1}')"
rm square.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/connectors/square.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (nonexistent source)..."
$ecli elektron-sample-dl $TEST_DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

echo "Testing mv..."
$ecli elektron-sample-mv $TEST_DEVICE:/$TEST_NAME/square $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing mv..."
$ecli elektron-sample-mv $TEST_DEVICE:/$TEST_NAME/foo $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rm..."
$ecli elektron-sample-rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing rm (nonexistent file)..."
$ecli elektron-sample-rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rmdir..."
$ecli elektron-sample-rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing rmdir (nonexistent dir)..."
$ecli elektron-sample-rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing recursive mkdir..."
$ecli elektron-sample-mkdir $TEST_DEVICE:/$TEST_NAME/foo
[ $? -ne 0 ] && exit 1

echo "Testing recursive rmdir..."
$ecli elektron-sample-rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing ls (nonexistent dir)..."
$ecli elektron-sample-ls $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing ls (nonexistent dir inside nonexistent dir)..."
$ecli elektron-sample-ls $TEST_DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

exit 0
