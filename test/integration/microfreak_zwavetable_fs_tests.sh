#!/usr/bin/env bash

#Zip files include date information, hence generic testing is not possible.

echo "Backing up wavetable..."
$ecli microfreak-pwavetable-dl $TEST_DEVICE:/16
[ $? -ne 0 ] && exit 1
FILE=$(echo "$srcdir/Arturia MicroFreak pwavetable 16"*.mfw)
BACKUP=$srcdir/backup.mfw
mv "$FILE" $srcdir/backup.mfw

$srcdir/integration/generic_fs_tests.sh --no-download microfreak zwavetable / 16 "/0 /17" /16 ""
[ $? -ne 0 ] && exit 1

echo "Testing download..."
$ecli microfreak-zwavetable-dl $TEST_DEVICE:/16
[ $? -ne 0 ] && exit 1
FILE=$(echo "$srcdir/Arturia MicroFreak zwavetable 16"*.mfwz)
[ ! -f "$FILE" ] && exit 1
exp=$(cksum "$srcdir/res/connectors/microfreak_pwavetable.data" | awk '{print $1}')
act=$(unzip -p "$FILE" "0_wavetable" | cksum | awk '{print $1}')
[ "$exp" != "$act" ] && exit 1
rm "$FILE"

echo "Restoring wavetable..."
$ecli microfreak-pwavetable-ul $BACKUP $TEST_DEVICE:/16
rm "$BACKUP"

exit $?
