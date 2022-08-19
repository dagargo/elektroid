#!/usr/bin/env bash

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

echo "Testing ls..."
$ecli sds-sample-ls $DEVICE:/ | head
echo '[...]'
files=$($ecli sds-sample-ls $DEVICE:/ | wc -l)
[ $? -ne 0 ] && exit 1
[ $files -ne 1000 ] && exit 0

echo "Testing upload..."
$ecli sds-sample-ul $srcdir/res/silence.wav $DEVICE:/1
[ $? -ne 1 ] && exit 1

echo "Testing upload with name..."
$ecli sds-sample-ul $srcdir/res/silence.wav $DEVICE:/1:silence
[ $? -ne 0 ] && exit 1

echo "Testing download..."
$ecli -v sds-sample-download $DEVICE:/1
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum 001.wav | awk '{print $1}')"
rm 001.wav
[ "$actual_cksum" != "$(cksum $srcdir/res/silence.wav | awk '{print $1}')" ] && exit 1

exit 0
