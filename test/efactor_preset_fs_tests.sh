#!/usr/bin/env bash

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

echo "Backing up preset..."
$ecli efactor-preset-dl $DEVICE:/99
f=Eventide\ Factor\ *

echo "Testing ls..."
files=$($ecli efactor-preset-ls $DEVICE:/)
[ $? -ne 0 ] && exit 1

echo "Testing upload..."
$ecli efactor-preset-ul "$srcdir/res/Eventide Factor Preset.syx" $DEVICE:/99
[ $? -ne 0 ] && exit 1

#Uploading with name makes no sense for factor pedals as the file includes the name.

echo "Testing mv..."
$ecli efactor-preset-mv $DEVICE:/99 "New Name"
[ $? -ne 0 ] && exit 1

echo "Testing download..."
$ecli efactor-preset-dl $DEVICE:/99
[ $? -ne 0 ] && exit 1
actual_cksum="$(cksum "Eventide Factor New Name.syx" | awk '{print $1}')"
rm "Eventide Factor New Name.syx"
[ "$actual_cksum" != $(cksum "$srcdir/res/Eventide Factor New Name.syx" | awk '{print $1}') ] && exit 1

echo "Restoring preset..."
$ecli efactor-preset-ul $f $DEVICE:/99
rm "$f"

exit 0
