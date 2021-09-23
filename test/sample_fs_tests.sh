#!/usr/bin/env bash

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
$ecli ls $DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing mkdir..."
$ecli mkdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

$ecli ls $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
$ecli upload $srcdir/res/square.wav $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

output=$($ecli ls $DEVICE:/$TEST_NAME)
echo $output
type=$(echo $output | awk '{print $1}')
size=$(echo $output | awk '{print $2}')
name=$(echo $output | awk '{print $4}')
[ "$type" != "F" ] || [ "$size" != "93.81KiB" ] || [ "$name" != "square" ] && exit 1

echo "Testing upload (nonexistent source)..."
$ecli upload $srcdir/res/foo $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$ecli download $DEVICE:/$TEST_NAME/square
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square.wav | awk '{print $1}')"
rm square.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/square.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (nonexistent source)..."
$ecli download $DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

echo "Testing mv..."
$ecli mv $DEVICE:/$TEST_NAME/square $DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing mv..."
$ecli mv $DEVICE:/$TEST_NAME/foo $DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rm..."
$ecli rm $DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing rm (nonexistent file)..."
$ecli rm $DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rmdir..."
$ecli rmdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing rmdir (nonexistent dir)..."
$ecli rmdir $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing recursive mkdir..."
$ecli mkdir $DEVICE:/$TEST_NAME/foo
[ $? -ne 0 ] && exit 1

echo "Testing recursive rmdir..."
$ecli rmdir $DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing ls (nonexistent dir)..."
$ecli ls $DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing ls (nonexistent dir inside nonexistent dir)..."
$ecli ls $DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

exit 0
