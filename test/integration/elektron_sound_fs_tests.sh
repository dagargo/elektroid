#!/usr/bin/env bash

err=0

echo "Cleaning up sample..."
$BINDIR/elektroid-cli elektron:sample:rm $TEST_DEVICE:/auto-test/square
$BINDIR/elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/auto-test

echo "Preparing tests..."
$BINDIR/elektroid-cli elektron:sound:ul $SRCDIR/res/connectors/elektron_sound.data $TEST_DEVICE:/H/256
$BINDIR/elektroid-cli elektron:sound:dl $TEST_DEVICE:/H/256

src_content=$(unzip -l $SRCDIR/res/connectors/elektron_sound.data | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
dst_content=$(unzip -l $SRCDIR/SOUND.dtsnd | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
echo "Comparing zip files..."
echo "$src_content"
echo "---"
echo "$dst_content"
cksum1=$(echo "$src_content" | cksum)
cksum2=$(echo "$dst_content" | cksum)
[ "$cksum1" != "$cksum2" ] && err=1

src_manifest_content=$(unzip -p $SRCDIR/res/connectors/elektron_sound.data manifest.json)
dst_manifest_content=$(unzip -p $SRCDIR/SOUND.dtsnd manifest.json)

echo "Checking manifest.json..."
echo "$src_manifest_content"
echo "---"
echo "$dst_manifest_content"

[ "$src_manifest_content" != "$dst_manifest_content" ] && err=1

if [ $err -eq 0 ]; then
  echo "Looking for sample..."
  $BINDIR/elektroid-cli elektron:sample:dl $TEST_DEVICE:/auto-test/square
  [ $? -ne 0 ] && err=1
fi

if [ $err -eq 0 ]; then
  $SRCDIR/integration/generic_fs_tests.sh --no-download elektron sound /H 256 "/H/0 /H/257" /H/256 ""
  [ $? -ne 0 ] && err=1
fi

echo "Cleaning up..."
rm -f $SRCDIR/SOUND.dtsnd
rm -f $SRCDIR/square.wav
$BINDIR/elektroid-cli elektron:sound:rm $TEST_DEVICE:/H/256
$BINDIR/elektroid-cli elektron:sample:rm $TEST_DEVICE:/auto-test/square
$BINDIR/elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/auto-test

exit $err
