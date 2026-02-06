#!/usr/bin/env bash

# Slots are in the range [ 1, 8 ].

err=0

echo "Testing ls..."
files=$($ecli elektron:track:ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
[ $(echo "$files" | wc -l) -ne 8 ] && exit 1

echo "Testing upload and download..."
src_file=$srcdir/res/connectors/square.wav
name=$(mktemp -u sample-test-XXXX.wav)
incoming_file=/incoming/${name:0:-4}
tmp_file=$srcdir/$name
cp $src_file $tmp_file
$ecli elektron:track:ul $tmp_file $TEST_DEVICE:/8
[ $? -ne 0 ] && exit 1
rm $tmp_file
$ecli elektron:track:dl $tmp_file $TEST_DEVICE:/8
[ $? -eq 0 ] && exit 1
$ecli elektron:sample:dl $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1
[ ! -f $tmp_file ] && err=1 && exit 1
echo "Testing cksum..."
cksum1=$(cksum $src_file | awk '{print $1}')
cksum2=$(cksum $tmp_file | awk '{print $1}')
[ "$cksum1" != "$cksum2" ] && err=1

rm $tmp_file

# Beware that uploading the same sample file with different names might cause issues with the file hash.

echo "Deleting uploaded sample..."
$ecli elektron:sample:rm $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1

exit $err
