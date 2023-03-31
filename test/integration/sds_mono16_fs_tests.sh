#!/usr/bin/env bash

echo "Using device $TEST_DEVICE..."

echo "Testing ls..."
files=$($ecli sds-mono16-ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
echo '[...]'
[ $(echo "$files" | wc -l) -ne 1000 ] && exit 1

echo "Testing upload..."
$ecli sds-mono16-ul $srcdir/res/connectors/silence.wav $TEST_DEVICE:/1
[ $? -ne 1 ] && exit 1

echo "Testing upload with name..."
$ecli sds-mono16-ul $srcdir/res/connectors/silence.wav $TEST_DEVICE:/1:silence
[ $? -ne 0 ] && exit 1

# If renaming is not implemented, this will fail.
echo "Testing mv..."
$ecli sds-mono16-ul $TEST_DEVICE:/1 "Foo"
[ $? -ne 1 ] && exit 1

echo "Testing download..."
$ecli sds-mono16-download $TEST_DEVICE:/1
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum 001.wav | awk '{print $1}')"
rm 001.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/connectors/silence.wav | awk '{print $1}')" ] && exit 1

exit 0
