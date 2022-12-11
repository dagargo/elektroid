#!/usr/bin/env bash

TEST_PRESET=99

function exitWithError() {
  echo "Restoring preset..."
  $ecli efactor-preset-ul "$f" $TEST_DEVICE:/$TEST_PRESET
  rm "$f"
  exit $1
}

echo "Using device $TEST_DEVICE..."

echo "Backing up preset..."
$ecli efactor-preset-dl $TEST_DEVICE:/$TEST_PRESET
f=$(echo Eventide\ Factor\ *)

echo "Testing ls..."
files=$($ecli efactor-preset-ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exitWithError 1

echo "Testing upload..."
$ecli efactor-preset-ul "$srcdir/res/Eventide Factor Preset.syx" $TEST_DEVICE:/$TEST_PRESET
[ $? -ne 0 ] && exitWithError 1

#Uploading with name makes no sense for factor pedals as the file includes the name.

echo "Testing mv..."
$ecli efactor-preset-mv $TEST_DEVICE:/$TEST_PRESET "New Name"
[ $? -ne 0 ] && exitWithError 1

echo "Testing download..."
$ecli efactor-preset-dl $TEST_DEVICE:/$TEST_PRESET
[ $? -ne 0 ] && exitWithError 1
actual_cksum="$(cksum "Eventide Factor $TEST_PRESET New Name.syx" | awk '{print $1}')"
rm "Eventide Factor $TEST_PRESET New Name.syx"
[ "$actual_cksum" != $(cksum "$srcdir/res/Eventide Factor New Name.syx" | awk '{print $1}') ] && exitWithError 1

exitWithError 0
