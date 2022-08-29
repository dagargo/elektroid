#!/usr/bin/env bash

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

echo "Testing ls..."
files=$($ecli sds-sample16-ls $DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
echo '[...]'
[ $(echo "$files" | wc -l) -ne 1000 ] && exit 1

echo "Testing upload..."
$ecli sds-sample16-ul $srcdir/res/silence.wav $DEVICE:/1
[ $? -ne 1 ] && exit 1

echo "Testing upload with name..."
$ecli sds-sample16-ul $srcdir/res/silence.wav $DEVICE:/1:silence
[ $? -ne 0 ] && exit 1

echo "Testing download..."
$ecli sds-sample16-download $DEVICE:/1
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum 001.wav | awk '{print $1}')"
rm 001.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/silence.wav | awk '{print $1}')" ] && exit 1

exit 0
