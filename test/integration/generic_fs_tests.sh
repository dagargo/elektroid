#!/usr/bin/env bash

download=true
download_ext=
[ "$1" == "--no-download" ] && shift && download=false
# Used to search the download file by its extension. Needed for files that don't use the connector name, such as wav files.
[ "$1" == "--download-ext" ] && shift && download_ext=".$1" && shift

CONN=$1
FS=$2
DIR_PATH=$3
LS_ROWS=$4
BAD_FILE_PATHS=$5
FILE_PATH=$6
FILE_NEW_NAME=$7
FILE_TO_UPLOAD=$srcdir/res/connectors/${CONN}_${FS}.data
FILE_UPLOADED_BACK="$FILE_TO_UPLOAD.back"
if [ ! -f "$FILE_UPLOADED_BACK" ]; then
  FILE_UPLOADED_BACK="$FILE_TO_UPLOAD"
fi

BACKUP_PREFIX="Backup - "

function exitWithError() {
  if [ -n "$FILE_BACKUP" ]; then
    echo "Restoring..."
    $ecli ${CONN}:${FS}:ul "$FILE_BACKUP" $TEST_DEVICE:$FILE_PATH
  fi
  rm -f "$FILE"
  rm -f "$FILE_BACKUP"
  exit $1
}

function getDownloadName() {
  if [ -z "$download_ext" ]; then
    echo "$srcdir/$DEVICE_NAME $FS"*
  else
    echo "$srcdir/"*$download_ext
  fi
}

echo "Using device $TEST_DEVICE..."
DEVICE_NAME=$(elektroid-cli info $TEST_DEVICE | grep "Device name:" | awk -F': ' '{print $2}')
echo "Device name: $DEVICE_NAME"

echo "Cleaning up previous executions..."
rm -f "$(getDownloadName)"
rm -f "$srcdir/$BACKUP_PREFIX$(getDownloadName)"

echo "Testing ls..."
files=$($ecli ${CONN}:${FS}:ls $TEST_DEVICE:$DIR_PATH)
[ $? -ne 0 ] && exit 1
echo "$files" | head
[ $(echo "$files" | wc -l) -ne $LS_ROWS ] && exit 1

if $download; then
  for p in $BAD_FILE_PATHS; do
    echo "Testing download with bad path $p..."
    $ecli ${CONN}:${FS}:dl $TEST_DEVICE:$p
    [ $? -eq 0 ] && exit 1
  done

  echo "Testing download with path $FILE_PATH..."
  $ecli ${CONN}:${FS}:dl $TEST_DEVICE:$FILE_PATH
  [ $? -ne 0 ] && exit 1
  FILE=$(getDownloadName)
  echo "Using file $FILE as the download file..."
  [ ! -f "$FILE" ] && exit 1
  FILE_BACKUP=/tmp/$BACKUP_PREFIX$(basename "$FILE")
  mv "$FILE" "$FILE_BACKUP"
fi

for p in $BAD_FILE_PATHS; do
  echo "Testing upload with bad path $p..."
  $ecli ${CONN}:${FS}:ul $FILE_TO_UPLOAD $TEST_DEVICE:$p
  [ $? -eq 0 ] && exitWithError 1
done

echo "Testing upload with non existing file to $FILE_PATH..."
$ecli ${CONN}:${FS}:ul foo $TEST_DEVICE:$FILE_PATH
[ $? -eq 0 ] && exitWithError 1

echo "Testing upload with path $FILE_PATH..."
$ecli ${CONN}:${FS}:ul $FILE_TO_UPLOAD $TEST_DEVICE:$FILE_PATH
[ $? -ne 0 ] && exitWithError 1

if [ -n "$FILE_NEW_NAME" ]; then
  echo "Testing mv of $FILE_PATH to $FILE_NEW_NAME..."
  $ecli ${CONN}:${FS}:mv $TEST_DEVICE:$FILE_PATH "$FILE_NEW_NAME"
  [ $? -ne 0 ] && exitWithError 1
fi

if $download; then
  echo "Testing data changes..."
  $ecli ${CONN}:${FS}:dl $TEST_DEVICE:$FILE_PATH
  [ $? -ne 0 ] && exitWithError 1
  FILE=$(getDownloadName)
  [ ! -f "$FILE" ] && exitWithError 1
  echo "Comparing $FILE to $FILE_UPLOADED_BACK..."
  cksum_act=$(cksum "$FILE" | awk '{print $1}')
  cksum_exp=$(cksum "$FILE_UPLOADED_BACK" | awk '{print $1}')
  echo "Actual cksum: $cksum_act"
  echo "Expected cksum: $cksum_exp"
  [ $cksum_act != $cksum_exp ] && exitWithError 1
fi

exitWithError 0
