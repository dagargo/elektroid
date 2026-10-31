#!/usr/bin/env bash

err=0

echo "Cleaning up sample..."
elektroid-cli elektron:sample:rm $TEST_DEVICE:/auto-test/square
elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/auto-test

echo "Preparing tests..."
elektroid-cli elektron:project:ul $SRCDIR/res/connectors/elektron_project.data $TEST_DEVICE:/128
elektroid-cli elektron:project:dl $TEST_DEVICE:/128

src_content=$(unzip -l $SRCDIR/res/connectors/elektron_project.data | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
dst_content=$(unzip -l $SRCDIR/PROJECT.dtprj | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
echo "Comparing zip files..."
echo "$src_content"
echo "---"
echo "$dst_content"
cksum1=$(echo "$src_content" | cksum)
cksum2=$(echo "$dst_content" | cksum)
[ "$cksum1" != "$cksum2" ] && err=1

if [ $err -eq 0 ]; then
  echo "Looking for sample..."
  elektroid-cli elektron:sample:dl $TEST_DEVICE:/auto-test/square
  [ $? -ne 0 ] && err=1
fi

if [ $err -eq 0 ]; then
  $SRCDIR/integration/generic_fs_tests.sh --no-download elektron project / 128 "/0 /129" /128 ""
  [ $? -ne 0 ] && err=1
fi

echo "Cleaning up..."
rm -f $SRCDIR/PROJECT.dtprj
rm -f $SRCDIR/square.wav
elektroid-cli elektron:project:rm $TEST_DEVICE:/H/256
elektroid-cli elektron:sample:rm $TEST_DEVICE:/auto-test/square
elektroid-cli elektron:sample:rmdir $TEST_DEVICE:/auto-test

exit $err
