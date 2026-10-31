#!/usr/bin/env bash

TEST_NAME=auto-test

echo "Using device $TEST_DEVICE..."

echo "Testing info..."
$BINDIR/elektroid-cli info $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing df..."
$BINDIR/elektroid-cli df $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing ls..."
$BINDIR/elektroid-cli elektron:sample:ls $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing mkdir..."
$BINDIR/elektroid-cli elektron:sample:mkdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

$BINDIR/elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
$BINDIR/elektroid-cli elektron:sample:ul $SRCDIR/res/connectors/square.wav $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

output=$($BINDIR/elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME)
type=$(echo "$output" | head -n 1 | awk '{print $1}')
size=$(echo "$output" | head -n 1 | awk '{print $2}')
name=$(echo "$output" | head -n 1 | awk '{print $4}')
[ "$type" != "F" ] || [ "$size" != "93.81KiB" ] || [ "$name" != "square" ] && exit 1

echo "Testing upload (nonexistent source)..."
$BINDIR/elektroid-cli elektron:sample:upload $SRCDIR/res/connectors/foo $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$BINDIR/elektroid-cli elektron:sample:download $TEST_DEVICE:/$TEST_NAME/square
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square.wav | awk '{print $1}')"
rm square.wav
[ "$actual_cksum" != "$(cksum $SRCDIR/res/connectors/square.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (nonexistent source)..."
$BINDIR/elektroid-cli elektron:sample:dl $TEST_DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

echo "Testing mv..."
$BINDIR/elektroid-cli elektron:sample:mv $TEST_DEVICE:/$TEST_NAME/square $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing mv (nonexistent file)..."
$BINDIR/elektroid-cli elektron:sample:mv $TEST_DEVICE:/$TEST_NAME/foo $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rm..."
$BINDIR/elektroid-cli elektron:sample:rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing rm (nonexistent file)..."
$BINDIR/elektroid-cli elektron:sample:rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rmdir..."
$BINDIR/elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing rmdir (nonexistent dir)..."
$BINDIR/elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing recursive mkdir..."
$BINDIR/elektroid-cli elektron:sample:mkdir $TEST_DEVICE:/$TEST_NAME/foo
[ $? -ne 0 ] && exit 1

echo "Testing recursive rmdir..."
$BINDIR/elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

#echo "Testing ls (nonexistent dir) [fails on versions newer than 1.40B]..."
#$BINDIR/elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME
#[ $? -eq 0 ] && exit 1

#echo "Testing ls (nonexistent dir inside nonexistent dir) [fails on Digitakt versions newer than 1.40B]..."
#$BINDIR/elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME/foo
#[ $? -eq 0 ] && exit 1

exit 0
