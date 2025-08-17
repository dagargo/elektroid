#!/usr/bin/env bash

FILE="$srcdir/Novation Summit tuning 16.syx"
BACKUP="$FILE.bak"

function exitWithError () {
  echo "Restoring..."
  $ecli summit:tuning:ul "$BACKUP" $TEST_DEVICE:/16
  rm -f "$FILE" "$BACKUP"
  exit $1
}

$srcdir/integration/generic_fs_tests.sh summit tuning / 17 /17 /16 ""
err=$?
[ $err -ne 0 ] && exit $err

echo "Creating backup..."
$ecli summit:tuning:dl $TEST_DEVICE:/16
err=$?
[ $err -ne 0 ] && exit $err
[ ! -f "$FILE" ] && exitWithError 1
mv "$FILE" "$BACKUP"

echo "Uploading scala file..."
$ecli summit:tuning:ul $srcdir/res/scala/TET.scl $TEST_DEVICE:/16
err=$?
[ $err -ne 0 ] && exitWithError $err

echo "Testing download scala as tuning..."
$ecli summit:tuning:dl $TEST_DEVICE:/16
[ $? -ne 0 ] && exitWithError 1
[ ! -f "$FILE" ] && exitWithError 1
cksum_act=$(cksum "$FILE" | awk '{print $1}')
cksum_exp=$(cksum $srcdir/res/connectors/summit_tunning_scale.data.back | awk '{print $1}')
echo "Actual cksum: $cksum_act"
echo "Expected cksum: $cksum_exp"
[ $cksum_act != $cksum_exp ] && exitWithError 1

exitWithError 0
