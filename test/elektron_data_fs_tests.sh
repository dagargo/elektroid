#!/usr/bin/env bash

export ELEKTROID_ELEKTRON_JSON=$srcdir/res/devices.json

function cleanupAndExitWithError () {
  $ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/1
  $ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/62
  $ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/63
  $ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/64
  $ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/256
  exit 1
}

function get_sound_n_with_id () {
  s="sound$1"
  echo "${!s}" | sed "s/^F   $1 0012/F  $2 007e/"
}

echo "Using device $TEST_DEVICE..."

sound1=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/A | grep "^F   1")
nsound1=$(get_sound_n_with_id 1 64)

sound2=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/A | grep "^F   2")

echo "Testing data copy..."
$ecli elektron-data-cp $TEST_DEVICE:/soundbanks/A/1 $TEST_DEVICE:/soundbanks/H/64
[ $? -ne 0 ] && cleanupAndExitWithError
$ecli elektron-data-cp $TEST_DEVICE:/soundbanks/A/2 $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExitWithError
output=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  64")
expected=$(get_sound_n_with_id 1 64)
[ "$actual" != "$expected" ] && cleanupAndExitWithError
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 2 63)
[ "$actual" != "$expected" ] && cleanupAndExitWithError

echo "Testing data move..."
$ecli elektron-data-mv $TEST_DEVICE:/soundbanks/H/64 $TEST_DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && cleanupAndExitWithError
output=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 1 62)
[ "$actual" != "$expected" ] && cleanupAndExitWithError
actual=$(echo "$output" | grep "^F  64")
[ -n "$actual" ] && cleanupAndExitWithError

echo "Testing data swap..."
$ecli elektron-data-sw $TEST_DEVICE:/soundbanks/H/62 $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExitWithError
output=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 2 62)
[ "$actual" != "$expected" ] && cleanupAndExitWithError
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 1 63)
[ "$actual" != "$expected" ] && cleanupAndExitWithError

echo "Testing data clear..."
$ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExitWithError
$ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && cleanupAndExitWithError
output=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H)
[ $(echo "$output" | grep "^F  62" | wc -l) -ne 0 ] && cleanupAndExitWithError
[ $(echo "$output" | grep "^F  63" | wc -l) -ne 0 ] && cleanupAndExitWithError

echo "Testing upload..."
$ecli elektron-data-ul $srcdir/res/SOUND.dtdata $TEST_DEVICE:/soundbanks/H
[ $? -ne 0 ] && cleanupAndExitWithError
id=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H | grep 'SOUND$' | awk '{print $2}')

echo "Testing download..."
$ecli elektron-data-dl $TEST_DEVICE:/soundbanks/H/$id
[ $? -ne 0 ] && cleanupAndExitWithError
ls "SOUND.dtdata"
cksum SOUND.dtdata
cksum $srcdir/res/SOUND.dtdata
actual_cksum="$(cksum SOUND.dtdata | awk '{print $1}')"
[ "$actual_cksum" != "$(cksum $srcdir/res/SOUND.dtdata | awk '{print $1}')" ] && cleanupAndExitWithError
rm SOUND.dtdata
[ $? -ne 0 ] && cleanupAndExitWithError
$ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/$id
[ $? -ne 0 ] && cleanupAndExitWithError

echo "Testing upload..."
$ecli elektron-data-ul $srcdir/res/SOUND.dtdata $TEST_DEVICE:/soundbanks/H/256
[ $? -ne 0 ] && cleanupAndExitWithError
id=$($ecli elektron-data-ls $TEST_DEVICE:/soundbanks/H | grep 'SOUND$' | awk '{print $2}')
[ $id != 256 ] && cleanupAndExitWithError

echo "Testing data clear..."
$ecli elektron-data-cl $TEST_DEVICE:/soundbanks/H/256
[ $? -ne 0 ] && cleanupAndExitWithError

exit 0
