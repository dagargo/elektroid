# Elektroid

Elektroid is a GNU/Linux transfer application for Elektron devices. It includes the `elektroid` GUI application and the `elektroid-cli` CLI application.
Elektroid has been reported to work with Model:Samples, Digitakt and Analog Rytm mk1 and mk2.

## Installation

As with other autotools project, you need to run the following commands. If you just want to compile `elektroid-cli`, pass `CLI_ONLY=yes` to `/configure`.

```
autoreconf --install
./configure
make
sudo make install
```

The package dependencies for Debian based distributions are:
- automake
- libtool
- build-essential
- libasound2-dev
- libgtk-3-dev
- libpulse-dev
- libsndfile1-dev
- libsamplerate0-dev
- autopoint
- gettext
- zlib1g-dev
- libjson-glib-dev

You can easily install them by running `sudo apt install automake libtool build-essential libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev autopoint gettext zlib1g-dev libjson-glib-dev`. If you are only compiling the CLI, install the dependencies with `sudo apt install automake libtool build-essential libasound2-dev libglib2.0-dev libsndfile1-dev libsamplerate0-dev libtool zlib1g-dev libjson-glib-dev`.

## CLI

`elektroid-cli` brings the same funcionality than `elektroid` to the command line.

There are global -or machine- commands and data oriented commands. The latter have the form `a-b` where `a` is a command and `b` is a filesystem, (e.g., `ls-project`, `download-sound`, `mkdir-sample`). Notice that the filesystem is always in the singular form and some older commands are deprecated but kept for compatibility reasons although there are not documented here.

These are the available filesystems:

* `sample`
* `raw`
* `preset`
* `data`
* `project`
* `sound`

Raw and data are intended to interface directly with the filesystems provided by the devices so the downloaded or uploaded files are **not** compatible with Elektron Transfer formats. Preset is a particular instance of raw and so are project and sound but regarding data. Thus, raw and data filesystems should be used only for testing and are **not** available in the GUI.

These are the available commands:

* `ls` or `list`
* `mkdir`
* `rmdir` or `rm` (both behave as `rm -rf`)
* `mv`
* `cp`
* `cl`, clear item
* `sw`, swap items
* `ul` or `upload`
* `dl` or `download`

Keep in mind that not every filesystem implements all the commands. For instance, samples can not be swapped.

Provided paths must always be prepended with the device id and a colon (':') e.g., `0:/incoming`.

### Global commands

* `ld` or `ls-devices`, list compatible devices

```
$ elektroid-cli ld
0 Elektron Digitakt MIDI 1
```

* `info` or `info-device`, show device info including device filesystems

```
$ elektroid-cli info 0
Digitakt 1.30 (Digitakt) filesystems=sample,data,project,sound
```

* `df` or `info-storage`, show size and use of +Drive and RAM

```
$ elektroid-cli df 0
Storage               Size            Used       Available       Use%
+Drive           959.50MiB       198.20MiB       761.30MiB     20.66%
RAM               64.00MiB        13.43MiB        50.57MiB     20.98%
```

### Sample, raw and preset commands

* `ls-sample`

It only works for directories. Notice that the first column is the file type, the second is the size, the third is an internal cksum and the last one is the sample name.

```
$ elektroid-cli ls-sample 0:/
D              0B 00000000 drum machines
F       630.34KiB f8711cd9 saw
F         1.29MiB 0bbc22bd square
```

* `mkdir-sample`

```
$ elektroid-cli mkdir-sample 0:/samples
```

* `rmdir-sample`

```
$ elektroid-cli rmdir-sample 0:/samples
```

* `ul-sample`

```
$ elektroid-cli ul-sample square.wav 0:/
```

* `dl-sample`

```
$ elektroid-cli dl-sample 0:/square
```

* `mv-sample`

```
$ elektroid-cli mv-sample 0:/square 0:/sample
```

* `rm-sample`

```
$ elektroid-cli rm-sample 0:/sample
```

### Data, sound and project commands

There are a few things to clarify first.

* All data commands are valid for both projects and sounds although the examples use just sounds.

* All data commands that use paths to items and not directories use the item index instead the item name.

Here are the commands.

* `ls-data`

It only works for directories. Notice that the first column is the file type, the second is the index, the third is the permissons in hexadecimal, the fourth indicates if the data in valid, the fifth indicates if it has metadatam, the sixth is the size and the last one is the item name.

Permissions are 16 bits values but only 6 are used from bit 2 to bit 7 both included. From LSB to MSB, this permissions are read, write, clear, copy, swap, and move.

```
$ elektroid-cli ls-data 0:/
D  -1 0000 0 0         0B projects
D  -1 0000 0 0         0B soundbanks
```

```
$ elektroid-cli ls-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
```

* `cp-data`

```
$ elektroid-cli cp-data 0:/soundbanks/D/1 0:/soundbanks/D/3
$ elektroid-cli ls-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
F   3 0012 1 1       160B KICK
```

* `sw-data`

```
$ elektroid-cli sw-data 0:/soundbanks/D/2 0:/soundbanks/D/3
$ elektroid-cli ls-data 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B KICK
F   3 0012 1 1       160B SNARE
```

* `mv-data`

```
$ elektroid-cli mv-data 0:/soundbanks/D/3 0:/soundbanks/D/1
$ elektroid-cli ls-data 0:/soundbanks/D
F   1 0012 1 1       160B SNARE
F   2 0012 1 1       160B KICK
```

* `cl-data`

```
$ elektroid-cli cl-data 0:/soundbanks/D/1
$ elektroid-cli ls-data 0:/soundbanks/D
F   2 0012 1 1       160B KICK
```

* `dl-data`

```
$ elektroid-cli dl-data 0:/soundbanks/D/1
```

* `ul-data`

```
$ elektroid-cli ul-data sound 0:/soundbanks/D
```
