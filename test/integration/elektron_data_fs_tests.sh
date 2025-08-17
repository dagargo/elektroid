#!/usr/bin/env bash

function cleanupAndExit () {
  for s in 1 62 63 64 256; do
    if [ -f $s.dtdata.bak ]; then
      $ecli elektron:data:ul $s.dtdata.bak $TEST_DEVICE:/soundbanks/H/$s
      rm -f $s.dtdata.bak
    fi
  done
  exit $1
}

function get_sound_n_with_id () {
  s="sound$1"
  echo "${!s}" | sed "s/^F   $1 0012/F  $2 007e/"
}

echo "Using device $TEST_DEVICE..."

echo "Preparing tests..."
for s in 1 62 63 64 256; do
  $ecli elektron:data:dl $TEST_DEVICE:/soundbanks/H/$s
  mv *.dtdata $s.dtdata.bak > /dev/null 2>&1
done
$ecli elektron:data:cl $TEST_DEVICE:/soundbanks/H/1

sound1=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/A | grep "^F   1")
nsound1=$(get_sound_n_with_id 1 64)

sound2=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/A | grep "^F   2")

echo "Testing data copy..."
$ecli elektron:data:cp $TEST_DEVICE:/soundbanks/A/1 $TEST_DEVICE:/soundbanks/H/64
[ $? -ne 0 ] && cleanupAndExit 1
$ecli elektron:data:cp $TEST_DEVICE:/soundbanks/A/2 $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExit 1
output=$($ecli elektron_data:ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  64")
expected=$(get_sound_n_with_id 1 64)
[ "$actual" != "$expected" ] && cleanupAndExit 1
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 2 63)
[ "$actual" != "$expected" ] && cleanupAndExit 1

echo "Testing data move..."
$ecli elektron:data:mv $TEST_DEVICE:/soundbanks/H/64 $TEST_DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && cleanupAndExit 1
output=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 1 62)
[ "$actual" != "$expected" ] && cleanupAndExit 1
actual=$(echo "$output" | grep "^F  64")
[ -n "$actual" ] && cleanupAndExit 1

echo "Testing data swap..."
$ecli elektron:data:sw $TEST_DEVICE:/soundbanks/H/62 $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExit 1
output=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/H)
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 2 62)
[ "$actual" != "$expected" ] && cleanupAndExit 1
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 1 63)
[ "$actual" != "$expected" ] && cleanupAndExit 1

echo "Testing data clear..."
$ecli elektron:data:cl $TEST_DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && cleanupAndExit 1
$ecli elektron:data:cl $TEST_DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && cleanupAndExit 1
output=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/H)
[ $(echo "$output" | grep "^F  62" | wc -l) -ne 0 ] && cleanupAndExit 1
[ $(echo "$output" | grep "^F  63" | wc -l) -ne 0 ] && cleanupAndExit 1

echo "Testing upload without slot..."
$ecli elektron:data:ul $srcdir/res/connectors/SOUND.dtdata $TEST_DEVICE:/soundbanks/H
[ $? -eq 0 ] && cleanupAndExit 1

echo "Testing upload..."
$ecli elektron:data:ul $srcdir/res/connectors/SOUND.dtdata $TEST_DEVICE:/soundbanks/H/256
[ $? -ne 0 ] && cleanupAndExit 1
id=$($ecli elektron:data:ls $TEST_DEVICE:/soundbanks/H | tail -n 1 | grep SOUND | awk '{print $6}')
[ "$id" != 256 ] && cleanupAndExit 1

echo "Testing data clear..."
$ecli elektron:data:cl $TEST_DEVICE:/soundbanks/H/256
[ $? -ne 0 ] && cleanupAndExit 1

cleanupAndExit 0
