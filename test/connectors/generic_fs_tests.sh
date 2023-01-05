#!/usr/bin/env bash

CONN=$1
FS=$2
BAD_ID=$3
ID=$4
FILE=$srcdir/$5
FILE_BACKUP="$FILE.backup"
FILE_TO_UPLOAD=$srcdir/res/connectors/${CONN}_${FS}.data
FILE_UPLOADED_BACK="$FILE_TO_UPLOAD.back"

function exitWithError() {
  echo "Restoring..."
  $ecli ${CONN}-${FS}-ul "$FILE_BACKUP" $TEST_DEVICE:/:$ID
  rm -f "$FILE"
  rm -f "$FILE_BACKUP"
  exit $1
}

echo "Using device $TEST_DEVICE..."

echo "Testing ls..."
$ecli ${CONN}-${FS}-ls $TEST_DEVICE:/
files=$($ecli ${CONN}-${FS}-ls $TEST_DEVICE:/ | wc -l)
[ $? -ne 0 ] && exit 1
[ $files -ne 8 ] && exit 1

echo "Testing download with bad id ($BAD_ID)..."
$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$BAD_ID
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$ID
[ $? -ne 0 ] && exit 1
[ ! -f "$FILE" ] && exit 1
mv "$FILE" "$FILE_BACKUP"

echo "Testing upload with bad id ($BAD_ID)..."
$ecli ${CONN}-${FS}-ul $FILE_TO_UPLOAD $TEST_DEVICE:/$BAD_ID
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload bad file..."
$ecli ${CONN}-${FS}-ul foo $TEST_DEVICE:/$ID
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload..."
$ecli ${CONN}-${FS}-ul $FILE_TO_UPLOAD $TEST_DEVICE:/$ID
[ $? -ne 0 ] && exitWithError 1

$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$ID
[ $? -ne 0 ] && exitWithError 1
[ $(cksum "$FILE" | awk '{print $1}') != $(cksum $FILE_UPLOADED_BACK | awk '{print $1}') ] && exitWithError 1

exitWithError 0
