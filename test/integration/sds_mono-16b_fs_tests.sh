#!/usr/bin/env bash

echo "Using device $TEST_DEVICE..."

echo "Testing ls..."
files=$(elektroid-cli sds:mono-16b:ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
echo '[...]'
[ $(echo "$files" | wc -l) -ne 1000 ] && exit 1

echo "Testing upload..."
elektroid-cli sds:mono-16b:ul $SRCDIR/res/connectors/silence.wav $TEST_DEVICE:/1
[ $? -ne 0 ] && exit 1

# If renaming is not implemented, this will fail.
echo "Testing mv..."
elektroid-cli sds:mono-16b:mv $TEST_DEVICE:/1 "Foo"
[ $? -ne 1 ] && exit 1

echo "Testing download..."
elektroid-cli sds:mono-16b:download $TEST_DEVICE:/1
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum 001.wav | awk '{print $1}')"
rm 001.wav
[ "$actual_cksum" != "$(cksum $SRCDIR/res/connectors/silence.wav | awk '{print $1}')" ] && exit 1

exit 0
