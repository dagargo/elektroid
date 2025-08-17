#!/usr/bin/env bash

err=0

tmpdir=$(mktemp -d)

echo "Runnning tests on $tmpdir..."

actf=$tmpdir/square.wav
for f in $($ecli info 0 | grep Filesystems | awk '{print $2}' | sed 's/,/ /g'); do
  expf=$srcdir/res/connectors/square-$f.wav
  [ ! -f $expf ] && echo "$expf test file not found" && err=1 && continue
  $ecli system:$f:ul $srcdir/res/connectors/square.wav 0:$tmpdir
  [ $? -ne 0 ] && err=1 && continue
  cksum $actf
  cksum $expf
  ls -l $tmpdir
  act=$(cksum $actf | awk '{print $1 " " $2}')
  exp=$(cksum $expf | awk '{print $1 " " $2}')
  [ "$act" != "$exp" ] && echo "Unexpected cksum for $f" && err=1
done

echo "Runnning test from 1 to 2 channels on $tmpdir..."

actf=$tmpdir/square-wav48k16b1c.wav
expf=$srcdir/res/connectors/square-wav48k16b2c.wav
$ecli system:wav48k16b2c:ul $srcdir/res/connectors/square-wav48k16b1c.wav 0:$tmpdir
if [ $? -eq 0 ]; then
  cksum $actf
  cksum $expf
  act=$(cksum $actf | awk '{print $1 " " $2}')
  exp=$(cksum $expf | awk '{print $1 " " $2}')
  [ "$act" != "$exp" ] && echo "Unexpected cksum for $f" && err=1
else
  err=1
fi

echo "Runnning test from 2 to 1 channels on $tmpdir..."

act=$tmpdir/square-wav48k16b2c.wav
exp=$srcdir/res/connectors/square-wav48k16b1c.wav
$ecli system:wav48k16b1c:ul $srcdir/res/connectors/square-wav48k16b2c.wav 0:$tmpdir
if [ $? -eq 0 ]; then
  cksum $actf
  cksum $expf
  act=$(cksum $actf | awk '{print $1 " " $2}')
  exp=$(cksum $expf | awk '{print $1 " " $2}')
  [ "$act" != "$exp" ] && echo "Unexpected cksum for $f" && err=1
else
  err=1
fi

rm -rf $tmpdir

exit $err
