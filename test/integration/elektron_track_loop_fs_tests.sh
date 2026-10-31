#!/usr/bin/env bash

echo "****************************************************"
echo "* This test requires pattern tempo to be activated *"
echo "****************************************************"

# Same as the track filesystem

err=0

echo "Testing ls..."
files=$(elektroid-cli elektron:track-loop:ls $TEST_DEVICE:/)
[ $? -ne 0 ] && exit 1
echo "$files" | head
[ $(echo "$files" | wc -l) -ne 8 ] && exit 1

echo "Testing upload and download..."
src_file=$SRCDIR/res/connectors/square.wav
name=$(mktemp -u sample-test-XXXX.wav)
incoming_file=/incoming/${name:0:-4}
tmp_file=$SRCDIR/$name
cp $src_file $tmp_file
elektroid-cli elektron:track-loop:ul $tmp_file $TEST_DEVICE:/8
[ $? -ne 0 ] && exit 1
rm $tmp_file
elektroid-cli elektron:track:dl $tmp_file $TEST_DEVICE:/8
[ $? -eq 0 ] && exit 1
elektroid-cli elektron:sample:dl $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1
[ ! -f $tmp_file ] && err=1 && exit 1
echo "Testing cksum..."
cksum1=$(cksum $src_file | awk '{print $1}')
cksum2=$(cksum $tmp_file | awk '{print $1}')
[ "$cksum1" != "$cksum2" ] && err=1

rm $tmp_file

# Beware that uploading the same sample file with different names might cause issues with the file hash.

echo "Deleting uploaded sample..."
elektroid-cli elektron:sample:rm $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1

# This part requires pattern tempo to be activated.

echo "Sending pattern..."
elektroid-cli send $SRCDIR/res/connectors/elektron_pattern_86_bpm.syx $TEST_DEVICE

echo "Testing upload and download..."
src_file=$SRCDIR/res/connectors/drum_loop_74_bpm.wav
name=$(mktemp -u sample-test-XXXX.wav)
incoming_file=/incoming/${name:0:-4}
tmp_file=$SRCDIR/$name
cp $src_file $tmp_file
elektroid-cli elektron:track-loop:ul $tmp_file $TEST_DEVICE:/8
[ $? -ne 0 ] && exit 1
rm $tmp_file
elektroid-cli elektron:track:dl $tmp_file $TEST_DEVICE:/8
[ $? -eq 0 ] && exit 1
elektroid-cli elektron:sample:dl $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1
[ ! -f $tmp_file ] && err=1 && exit 1
echo "Testing cksum..."
cksum1=$(cksum $SRCDIR/res/connectors/drum_loop_86_bpm.wav | awk '{print $1}')
cksum2=$(cksum $tmp_file | awk '{print $1}')
[ "$cksum1" != "$cksum2" ] && err=1

rm $tmp_file

# Beware that uploading the same sample file with different names might cause issues with the file hash.

echo "Deleting uploaded sample..."
elektroid-cli elektron:sample:rm $TEST_DEVICE:$incoming_file
[ $? -ne 0 ] && err=1

exit $err
