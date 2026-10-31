#!/usr/bin/env bash

TEST_NAME=auto-test

echo "Using device $TEST_DEVICE..."

echo "Testing info..."
elektroid-cli info $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing df..."
elektroid-cli df $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing ls..."
elektroid-cli elektron:sample:ls $TEST_DEVICE:/
[ $? -ne 0 ] && exit 1

echo "Testing mkdir..."
elektroid-cli elektron:sample:mkdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
elektroid-cli elektron:sample:ul $SRCDIR/res/connectors/square.wav $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

output=$(elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME)
type=$(echo "$output" | head -n 1 | awk '{print $1}')
size=$(echo "$output" | head -n 1 | awk '{print $2}')
name=$(echo "$output" | head -n 1 | awk '{print $4}')
[ "$type" != "F" ] || [ "$size" != "93.81KiB" ] || [ "$name" != "square" ] && exit 1

echo "Testing upload (nonexistent source)..."
elektroid-cli elektron:sample:upload $SRCDIR/res/connectors/foo $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing download..."
elektroid-cli elektron:sample:download $TEST_DEVICE:/$TEST_NAME/square
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum square.wav | awk '{print $1}')"
rm square.wav
[ "$actual_cksum" != "$(cksum $SRCDIR/res/connectors/square.wav | awk '{print $1}')" ] && exit 1

echo "Testing download (nonexistent source)..."
elektroid-cli elektron:sample:dl $TEST_DEVICE:/$TEST_NAME/foo
[ $? -eq 0 ] && exit 1

echo "Testing mv..."
elektroid-cli elektron:sample:mv $TEST_DEVICE:/$TEST_NAME/square $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing mv (nonexistent file)..."
elektroid-cli elektron:sample:mv $TEST_DEVICE:/$TEST_NAME/foo $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rm..."
elektroid-cli elektron:sample:rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -ne 0 ] && exit 1

echo "Testing rm (nonexistent file)..."
elektroid-cli elektron:sample:rm $TEST_DEVICE:/$TEST_NAME/sample
[ $? -eq 0 ] && exit 1

echo "Testing rmdir..."
elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

echo "Testing rmdir (nonexistent dir)..."
elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -eq 0 ] && exit 1

echo "Testing recursive mkdir..."
elektroid-cli elektron:sample:mkdir $TEST_DEVICE:/$TEST_NAME/foo
[ $? -ne 0 ] && exit 1

echo "Testing recursive rmdir..."
elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/$TEST_NAME
[ $? -ne 0 ] && exit 1

#echo "Testing ls (nonexistent dir) [fails on versions newer than 1.40B]..."
#elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME
#[ $? -eq 0 ] && exit 1

#echo "Testing ls (nonexistent dir inside nonexistent dir) [fails on Digitakt versions newer than 1.40B]..."
#elektroid-cli elektron:sample:ls $TEST_DEVICE:/$TEST_NAME/foo
#[ $? -eq 0 ] && exit 1

exit 0
