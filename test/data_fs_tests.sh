#!/usr/bin/env bash

function get_sound_n_with_id () {
  s="sound$1"
  echo "${!s}" | sed "s/^F   $1 0012/F  $2 007e/"
}

echo "Getting devices..."
DEVICE=$($ecli ld | head -n 1 | awk '{print $1}')
sleep 1
[ -z "$DEVICE" ] && echo "No device found" && exit 0
echo "Using device $DEVICE..."

sound1=$($ecli list-data $DEVICE:/soundbanks/A | grep "^F   1")
sleep 1
nsound1=$(get_sound_n_with_id 1 64)

sound2=$($ecli list-data $DEVICE:/soundbanks/A | grep "^F   2")
sleep 1

echo "Testing data copy..."
$ecli copy-data $DEVICE:/soundbanks/A/1 $DEVICE:/soundbanks/H/64
[ $? -ne 0 ] && exit 1
sleep 1
$ecli copy-data $DEVICE:/soundbanks/A/2 $DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && exit 1
sleep 1
output=$($ecli list-data $DEVICE:/soundbanks/H)
sleep 1
actual=$(echo "$output" | grep "^F  64")
expected=$(get_sound_n_with_id 1 64)
[ "$actual" != "$expected" ] && exit 1
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 2 63)
[ "$actual" != "$expected" ] && exit 1

echo "Testing data move..."
$ecli move-data $DEVICE:/soundbanks/H/64 $DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && exit 1
sleep 1
output=$($ecli list-data $DEVICE:/soundbanks/H)
sleep 1
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 1 62)
[ "$actual" != "$expected" ] && exit 1
actual=$(echo "$output" | grep "^F  64")
[ -n "$actual" ] && exit 1

echo "Testing data swap..."
$ecli swap-data $DEVICE:/soundbanks/H/62 $DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && exit 1
sleep 1
output=$($ecli list-data $DEVICE:/soundbanks/H)
sleep 1
actual=$(echo "$output" | grep "^F  62")
expected=$(get_sound_n_with_id 2 62)
[ "$actual" != "$expected" ] && exit 1
actual=$(echo "$output" | grep "^F  63")
expected=$(get_sound_n_with_id 1 63)
[ "$actual" != "$expected" ] && exit 1

echo "Testing data clear..."
$ecli clear-data $DEVICE:/soundbanks/H/63
[ $? -ne 0 ] && exit 1
sleep 1
$ecli clear-data $DEVICE:/soundbanks/H/62
[ $? -ne 0 ] && exit 1
sleep 1
output=$($ecli list-data $DEVICE:/soundbanks/H)
[ $(echo "$output" | grep "^F  62" | wc -l) -ne 0 ] && exit 1
[ $(echo "$output" | grep "^F  63" | wc -l) -ne 0 ] && exit 1

echo "Testing download..."
$ecli download-data $DEVICE:/soundbanks/A/1
[ $? -ne 0 ] && exit 1
sleep 1
name=$($ecli list-data $DEVICE:/soundbanks/A | grep "^F   1" | awk -F'[ ]' '{for(i=9;i<=NF-1;i++){printf "%s ", $i}; printf "%s\n", $i}').data
ls "$name"
[ $? -ne 0 ] && exit 1
rm "$name"
[ $? -ne 0 ] && exit 1

exit 0
