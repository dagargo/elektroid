#!/usr/bin/env bash

CONN=$1
FS=$2
LS_ROWS=$3
BAD_ID=$4
ID=$5
FILE_UPLOAD_NAME=$6
FILE_NEW_NAME=$7
FILE_TO_UPLOAD=$srcdir/res/connectors/${CONN}_${FS}.data
FILE_UPLOADED_BACK="$FILE_TO_UPLOAD.back"

BACKUP_PREFIX="Backup - "

function exitWithError() {
  echo "Restoring..."
  $ecli ${CONN}-${FS}-ul "$FILE_BACKUP" $TEST_DEVICE:/:$ID
  rm -f "$FILE"
  rm -f "$FILE_BACKUP"
  exit $1
}

echo "Using device $TEST_DEVICE..."
DEVICE_NAME=$(elektroid-cli info $TEST_DEVICE | awk -F\; '{print $1}')

echo "Cleaning up previous executions..."
rm -f "$srcdir/$DEVICE_NAME ${FS}"*
rm -f "$srcdir/$BACKUP_PREFIX$DEVICE_NAME ${FS}"*

echo "Testing ls..."
files=$($ecli ${CONN}-${FS}-ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
[ $(echo "$files" | wc -l) -ne $LS_ROWS ] && exit 1

echo "Testing download with bad id ($BAD_ID)..."
$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$BAD_ID
[ $? -eq 0 ] && exit 1

echo "Testing download..."
$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$ID
[ $? -ne 0 ] && exit 1
FILE=$(echo "$srcdir/$DEVICE_NAME ${FS}"*)
[ ! -f "$FILE" ] && exit 1
FILE_BACKUP=$srcdir/$BACKUP_PREFIX$(basename "$FILE")
mv "$FILE" "$FILE_BACKUP"

echo "Testing upload with bad id ($BAD_ID)..."
$ecli ${CONN}-${FS}-ul $FILE_TO_UPLOAD $TEST_DEVICE:/$BAD_ID
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload bad file..."
$ecli ${CONN}-${FS}-ul foo $TEST_DEVICE:/$ID
[ $? -eq 0 ] && exitWithError 1

if [ -z "$FILE_UPLOAD_NAME" ]; then
  echo "Testing upload..."
  $ecli ${CONN}-${FS}-ul $FILE_TO_UPLOAD $TEST_DEVICE:/$ID
else
  echo "Testing upload with name ($FILE_UPLOAD_NAME)..."
  $ecli ${CONN}-${FS}-ul $FILE_TO_UPLOAD $TEST_DEVICE:/${ID}:${FILE_UPLOAD_NAME}
fi
[ $? -ne 0 ] && exitWithError 1

if [ -n "$FILE_NEW_NAME" ]; then
  echo "Testing mv..."
  $ecli ${CONN}-${FS}-mv $TEST_DEVICE:/${ID} "$FILE_NEW_NAME"
  [ $? -ne 0 ] && exitWithError 1
fi

echo "Testing data changes..."
$ecli ${CONN}-${FS}-dl $TEST_DEVICE:/$ID
[ $? -ne 0 ] && exitWithError 1
FILE=$(echo "$srcdir/$DEVICE_NAME ${FS}"*)
[ ! -f "$FILE" ] && exitWithError 1
[ $(cksum "$FILE" | awk '{print $1}') != $(cksum $FILE_UPLOADED_BACK | awk '{print $1}') ] && exitWithError 1

exitWithError 0
