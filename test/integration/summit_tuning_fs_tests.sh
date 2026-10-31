#!/usr/bin/env bash

FILE="$SRCDIR/Novation Summit tuning 16.syx"
BACKUP="$FILE.bak"

function exitWithError () {
  echo "Restoring..."
  $BINDIR/elektroid-cli summit:tuning:ul "$BACKUP" $TEST_DEVICE:/16
  rm -f "$FILE" "$BACKUP"
  exit $1
}

$SRCDIR/integration/generic_fs_tests.sh summit tuning / 17 /17 /16 ""
err=$?
[ $err -ne 0 ] && exit $err

echo "Creating backup..."
$BINDIR/elektroid-cli summit:tuning:dl $TEST_DEVICE:/16
err=$?
[ $err -ne 0 ] && exit $err
[ ! -f "$FILE" ] && exitWithError 1
mv "$FILE" "$BACKUP"

echo "Uploading scala file..."
$BINDIR/elektroid-cli summit:tuning:ul $SRCDIR/res/scala/TET.scl $TEST_DEVICE:/16
err=$?
[ $err -ne 0 ] && exitWithError $err

echo "Testing download scala as tuning..."
$BINDIR/elektroid-cli summit:tuning:dl $TEST_DEVICE:/16
[ $? -ne 0 ] && exitWithError 1
[ ! -f "$FILE" ] && exitWithError 1
cksum_act=$(cksum "$FILE" | awk '{print $1}')
cksum_exp=$(cksum $SRCDIR/res/connectors/summit_tunning_scale.data.back | awk '{print $1}')
echo "Actual cksum: $cksum_act"
echo "Expected cksum: $cksum_exp"
[ $cksum_act != $cksum_exp ] && exitWithError 1

exitWithError 0
