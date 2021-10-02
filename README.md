# Elektroid

Elektroid is a GNU/Linux transfer application for Elektron devices. It includes the `elektroid` GUI application and the `elektroid-cli` CLI application.
Elektroid has been reported to work with Model:Samples, Digitakt and Analog Rytm mk1 and mk2.

## Installation

As with other autotools project, you need to run the following commands.

```
autoreconf --install
./configure
make
sudo make install
```

The package dependencies for Debian based distributions are:
- automake
- libasound2-dev
- libgtk-3-dev
- libpulse-dev
- libsndfile1-dev
- libsamplerate0-dev
- libtool
- autopoint
- gettext
- zlib1g-dev
- libjson-glib-dev

You can easily install them by running `sudo apt install automake libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev libtool autopoint gettext zlib1g-dev libjson-glib-dev`.

## CLI

`elektroid-cli` provides the same funcionality than `elektroid`. Provided paths must always be prepended with the device id and a colon (':'), e.g. `0:/samples`.

Here are the available commands.

### Global commands

* `ld` or `list-devices`, list compatible devices

```
$ elektroid-cli ld
0 Elektron Digitakt MIDI 1
```

* `info` or `info-device`, show device info

```
$ elektroid-cli info 0
Digitakt 1.11
```

* `df` or `info-storage`, show size and use of +Drive and RAM

```
$ elektroid-cli df 0
Storage               Size            Used       Available       Use%
+Drive           959.50MiB       198.20MiB       761.30MiB     20.66%
RAM               64.00MiB        13.43MiB        50.57MiB     20.98%
```

### Sample commands

* `ls` or `list-samples`

It only works for directories. Notice that the first column is the file type, the second is the size, the third is an internal cksum and the last one is the sample name.

```
$ elektroid-cli ls 0:/
D              0B 00000000 drum machines
F       630.34KiB f8711cd9 saw
F         1.29MiB 0bbc22bd square
```

* `mkdir` or `mkdir-samples`

```
$ elektroid-cli mkdir 0:/samples
```

* `rmdir` or `rmdir-samples`

```
$ elektroid-cli rmdir 0:/samples
```

* `upload` or `upload-sample`

```
$ elektroid-cli upload square.wav 0:/
```

* `download` or `download-sample`

```
$ elektroid-cli download 0:/square
```

* `mv` or `mv-sample`

```
$ elektroid-cli mv 0:/square 0:/sample
```

* `rm` or `rm-sample`

```
$ elektroid-cli rm 0:/sample
```

### Data commands

There are a few things to clarify first.

* All data commands are valid for both projects and sounds although the examples use just sounds.

* All data commands that use paths to items and not directories use the item index instead the item name.

* While `elektroid-cli` offers a single API for both projects and sounds, `elektroid` will treat them as different types of data because they are. The reason behind this difference is that the underlying MIDI API is not only the same but treats them as if they were the same kind of objects, and that is exaclty what `elektroid-cli` provides. `elektroid` just tries to be more user friendly by splitting this layer.

Here are the commands.

* `list-data`

It only works for directories. Notice that the first column is the file type, the second is the index, the third is the permissons in hexadecimal, the fourth indicates if the data in valid, the fifth indicates if it has metadatam, the sixth is the size and the last one is the item name.

Permissions are 16 bits values but only 6 are used from bit 2 to bit 7 both included. From LSB to MSB, this permissions are read, write, clear, copy, swap, and move.

```
$ elektroid-cli list-data 0:/
D  -1 0000 0 0         0B projects
D  -1 0000 0 0         0B soundbanks
```

```
$ elektroid-cli list-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
```

* `copy-data`

```
$ elektroid-cli copy-data 0:/soundbanks/D/1 0:/soundbanks/D/3
$ elektroid-cli list-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
F   3 0012 1 1       160B KICK
```

* `swap-data`

```
$ elektroid-cli swap-data 0:/soundbanks/D/2 0:/soundbanks/D/3
$ elektroid-cli list-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B KICK
F   3 0012 1 1       160B SNARE
```

* `move-data`

```
$ elektroid-cli move-data 0:/soundbanks/D/3 0:/soundbanks/D/1
$ elektroid-cli list-data 0:/soundbanks/D
F   1 0012 1 1       160B SNARE
F   2 0012 1 1       160B KICK
```

* `clear-data`

```
$ elektroid-cli clear-data 0:/soundbanks/D/1
$ elektroid-cli list-data 0:/soundbanks/D
F   2 0012 1 1       160B KICK
```

* `download-data`

```
$ elektroid-cli download-data 0:/soundbanks/D/1
```

* `upload-data`

```
$ elektroid-cli upload-data sound 0:/soundbanks/D
```
