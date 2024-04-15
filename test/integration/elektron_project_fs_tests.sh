#!/usr/bin/env bash

err=0

echo "Cleaning up sample..."
$ecli elektron-sample-rm $TEST_DEVICE:/auto-test/square
$ecli elektron-sample-rmdir $TEST_DEVICE:/auto-test

echo "Preparing tests..."
$ecli elektron-project-ul $srcdir/res/connectors/elektron_project.data $TEST_DEVICE:/128
$ecli elektron-project-dl $TEST_DEVICE:/128

src_content=$(unzip -l $srcdir/res/connectors/elektron_project.data | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
dst_content=$(unzip -l $srcdir/PROJECT.dtprj | tail -n +4 | head -n -2 | awk '{print $1" "$4}')
echo "Comparing zip files..."
echo "$src_content"
echo "---"
echo "$dst_content"
cksum1=$(echo "$src_content" | cksum)
cksum2=$(echo "$dst_content" | cksum)
[ "$cksum1" != "$cksum2" ] && err=1

if [ $err -eq 0 ]; then
  echo "Looking for sample..."
  $ecli elektron-sample-dl $TEST_DEVICE:/auto-test/square
  [ $? -ne 0 ] && err=1
fi

if [ $err -eq 0 ]; then
  $srcdir/integration/generic_fs_tests.sh --no-download elektron project / 128 "/0 /129" /128 "" ""
  [ $? -ne 0 ] && err=1
fi

echo "Cleaning up..."
rm -f $srcdir/PROJECT.dtprj
rm -f $srcdir/square.wav
$ecli elektron-project-rm $TEST_DEVICE:/H/256
$ecli elektron-sample-rm $TEST_DEVICE:/auto-test/square
$ecli elektron-sample-rmdir $TEST_DEVICE:/auto-test

exit $err
