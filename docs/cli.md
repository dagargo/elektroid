---
layout: default
title: CLI
permalink: /cli/
---

## CLI

`elektroid-cli` brings the same filesystem related functionality to the terminal.

There are device commands and filesystem commands. The latter have the form `a-b-c` where `a` is a connector, `b` is a filesystem and `c` is the command, (e.g., `elektron-project-ls`, `cz-program-upload`, `sds-sample-download`). Notice that the filesystem is always in the singular form. As of version 2.2, **older command forms have been removed**.

These are the available commands:

* `ls` or `list`
* `mkdir` (behave as `mkdir -p`)
* `rmdir` or `rm` (both behave as `rm -rf`)
* `mv` (in slot mode, the second path is just the name of the file)
* `cp`
* `cl`, clear item
* `sw`, swap items
* `ul` or `upload`
* `dl` or `download`

Keep in mind that not every filesystem implements all the commands. For instance, Elektron samples can not be swapped.

Provided paths must always be prepended with the device id and a colon (e.g., `0:/incoming`). In slot mode filesystems, (these are the most typically used), items are addressed by number and destination paths take the form `path:name` (e.g., `0:/0:bass`) when uploading.

### Device commands

* `ld` or `ls-devices`, list all MIDI devices with input and output

```
$ elektroid-cli ld
0: hw:2,0,0 Elektron Digitakt MIDI 1
1: hw:4,0,0 padKONTROL MIDI 1
2: hw:4,0,1 padKONTROL MIDI 2
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

* `send` and `receive` work with a batch of SysEx messages. These are useful when working with generic devices, which have no filesystems implemented buf offer options to receive or send data.

```
$ elektroid-cli send file.syx 0:/
$ elektroid-cli receive 0:/
```

* `upgrade`, upgrade firmware

```
$ elektroid-cli upgrade Digitakt_OS1.30.syx 0
```

### Elektron conector

These are the available filesystems for the elektron connector:

* `sample`
* `raw`
* `preset`
* `data`
* `project`
* `sound`

Raw and data are intended to interface directly with the filesystems provided by the devices so the downloaded or uploaded files are **not** compatible with Elektron Transfer formats. Preset is a particular instance of raw and so are project and sound but regarding data. Thus, raw and data filesystems should be used only for testing and are **not** available in the GUI.

#### Sample, raw and preset commands

* `elektron-sample-ls`

It only works for directories. Notice that the first column is the file type, the second is the size, the third is an internal cksum and the last one is the sample name.

```
$ elektroid-cli elektron-sample-ls 0:/
D              0B 00000000 drum machines
F       630.34KiB f8711cd9 saw
F         1.29MiB 0bbc22bd square
```

* `elektron-sample-mkdir`

```
$ elektroid-cli elektron-sample-mkdir 0:/samples
```

* `elektron-sample-rmdir`

```
$ elektroid-cli elektron-sample-rmdir 0:/samples
```

* `elektron-sample-ul`

```
$ elektroid-cli elektron-sample-ul square.wav 0:/
```

* `elektron-sample-dl`

```
$ elektroid-cli elektron-sample-dl 0:/square
```

* `elektron-sample-mv`

```
$ elektroid-cli elektron-sample-mv 0:/square 0:/sample
```

* `elektron-sample-rm`

```
$ elektroid-cli elektron-sample-rm 0:/sample
```

#### Data, sound and project commands

There are a few things to clarify first.

* All data commands are valid for both projects and sounds although the examples use just sounds.

* All data commands that use paths to items and not directories use the item index instead the item name.

Here are the commands.

* `elektron-data-ls`

It only works for directories. Notice that the first column is the file type, the second is the index, the third is the permissons in hexadecimal, the fourth indicates if the data in valid, the fifth indicates if it has metadatam, the sixth is the size and the last one is the item name.

Permissions are 16 bits values but only 6 are used from bit 2 to bit 7 both included. From LSB to MSB, this permissions are read, write, clear, copy, swap, and move.

```
$ elektroid-cli elektron-data-ls 0:/
D  -1 0000 0 0         0B projects
D  -1 0000 0 0         0B soundbanks
```

```
$ elektroid-cli elektron-data-ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
```

* `elektron-data-cp`

```
$ elektroid-cli elektron-data-cp 0:/soundbanks/D/1 0:/soundbanks/D/3
$ elektroid-cli elektron-data-ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
F   3 0012 1 1       160B KICK
```

* `elektron-data-sw`

```
$ elektroid-cli elektron-data-sw 0:/soundbanks/D/2 0:/soundbanks/D/3
$ elektroid-cli elektron-data-ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B KICK
F   3 0012 1 1       160B SNARE
```

* `elektron-data-mv`

```
$ elektroid-cli elektron-data-mv 0:/soundbanks/D/3 0:/soundbanks/D/1
$ elektroid-cli elektron-data-ls 0:/soundbanks/D
F   1 0012 1 1       160B SNARE
F   2 0012 1 1       160B KICK
```

* `elektron-data-cl`

```
$ elektroid-cli elektron-data-cl 0:/soundbanks/D/1
$ elektroid-cli elektron-data-ls 0:/soundbanks/D
F   2 0012 1 1       160B KICK
```

* `elektron-data-dl`

```
$ elektroid-cli elektron-data-dl 0:/soundbanks/D/1
```

* `elektron-data-ul`

```
$ elektroid-cli elektron-data-ul sound 0:/soundbanks/D
```
