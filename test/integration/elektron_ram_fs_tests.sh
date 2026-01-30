#!/usr/bin/env bash

# Slots are in the range [ 1, 127 ]. Slot 0 is not shown.

err=0

echo "Testing ls..."
files=$($ecli elektron:ram:ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
[ $(echo "$files" | wc -l) -ne 127 ] && exit 1

echo "Testing upload and download..."
src_file=$srcdir/res/connectors/square.wav
name=$(mktemp -u sample-test-XXXX.wav)
tmp_file=$srcdir/$name
cp $src_file $tmp_file
$ecli elektron:ram:ul $tmp_file $TEST_DEVICE:/127
[ $? -ne 0 ] && exit 1
rm $tmp_file
$ecli elektron:ram:dl $TEST_DEVICE:/127
[ $? -ne 0 ] && err=1
[ ! -f $tmp_file ] && err=1 && exit 1
echo "Testing cksum..."
cksum1=$(cksum $src_file | awk '{print $1}')
cksum2=$(cksum $tmp_file | awk '{print $1}')
[ "$cksum1" != "$cksum2" ] && err=1

rm $tmp_file

echo "Testing clear..."
$ecli elektron:ram:cl $TEST_DEVICE:/127
[ $? -ne 0 ] && err=1

# Beware that uploading the same sample file with different names might cause issues with the file hash.

echo "Deleting uploaded sample..."
$ecli elektron:sample:rm $TEST_DEVICE:/incoming/${name:0:-4}
[ $? -ne 0 ] && err=1

exit $err
