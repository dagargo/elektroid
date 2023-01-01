#!/usr/bin/env bash

TEST_PRESET=99

function exitWithError() {
  echo "Restoring preset..."
  $ecli phatty-preset-ul "$f" $TEST_DEVICE:/$TEST_PRESET
  rm "$f"
  exit $1
}

echo "Using device $TEST_DEVICE..."

echo "Backing up preset..."
$ecli phatty-preset-dl $TEST_DEVICE:/$TEST_PRESET
f=$(echo Moog\ Little\ Phatty\ *)

echo "Testing ls..."
files=$($ecli phatty-preset-ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exitWithError 1

echo "Testing upload..."
$ecli phatty-preset-ul "$srcdir/res/connectors/Moog Little Phatty Preset.syx" $TEST_DEVICE:/$TEST_PRESET
[ $? -ne 0 ] && exitWithError 1

#Uploading with name makes no sense for Little Phatty as the file includes the name.

echo "Testing mv..."
$ecli phatty-preset-mv $TEST_DEVICE:/$TEST_PRESET "New Name"
[ $? -ne 0 ] && exitWithError 1

echo "Testing download..."
$ecli phatty-preset-dl $TEST_DEVICE:/$TEST_PRESET
[ $? -ne 0 ] && exitWithError 1
actual_cksum="$(cksum "Moog Little Phatty $TEST_PRESET New Name.syx" | awk '{print $1}')"
rm "Moog Little Phatty $TEST_PRESET New Name.syx"
[ "$actual_cksum" != $(cksum "$srcdir/res/connectors/Moog Little Phatty New Name.syx" | awk '{print $1}') ] && exitWithError 1

exitWithError 0
