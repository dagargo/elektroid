#!/usr/bin/env bash

SEQ_FILE="MicroBrute sequence 1.mbseq"
SEQ_FILE_BACKUP="backup.mbseq"
SEQ_FILE_TEST="$srcdir/res/connectors/sequence.mbseq"
SEQ_FILE_TEST_BACK="$srcdir/res/connectors/sequence_back.mbseq"

function exitWithError() {
  echo "Restoring..."
  $ecli microbrute-sequence-upload "$SEQ_FILE_BACKUP" $TEST_DEVICE:/0:1
  rm -f "$SEQ_FILE"
  rm -f "$SEQ_FILE_BACKUP"
  exit $1
}

echo "Using device $TEST_DEVICE..."

echo "Testing ls..."
$ecli microbrute-sequence-ls $TEST_DEVICE:/
files=$($ecli microbrute-sequence-ls $TEST_DEVICE:/ | wc -l)
[ $? -ne 0 ] && exit 1
[ $files -ne 8 ] && exit 1

echo "Testing download..."
$ecli microbrute-sequence-download $TEST_DEVICE:/0
[ $? -ne 0 ] && exit 1
[ ! -f "$SEQ_FILE" ] && exit 1
mv "$SEQ_FILE" "$SEQ_FILE_BACKUP"

echo "Testing upload bad file..."
$ecli microbrute-sequence-ul foo $TEST_DEVICE:/0:a
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload bad destination..."
$ecli microbrute-sequence-ul foo $TEST_DEVICE:/9
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload..."
$ecli microbrute-sequence-upload $SEQ_FILE_TEST $TEST_DEVICE:/0:1
[ $? -ne 0 ] && exitWithError 1

$ecli microbrute-sequence-dl $TEST_DEVICE:/0
[ $? -ne 0 ] && exitWithError 1
[ $(cksum "$SEQ_FILE" | awk '{print $1}') != $(cksum $SEQ_FILE_TEST_BACK | awk '{print $1}') ] && exitWithError 1

exitWithError 0
