---
layout: base
title: CLI
permalink: /cli/
order: 4
---

## CLI

`elektroid-cli` brings the same filesystem related functionality to the terminal.

There are filesystem commands and non-filesystem commands. The former have the form `a:b:c` where `a` is a connector, `b` is a filesystem and `c` is the command, (e.g., `elektron:project:ls`, `cz:program:upload`, `sds:sample:download`). Notice that the filesystem is always in the singular form. Using a hyphen (`-`) instead of a semicolon (`:`) is allowed but should be avoided.

These are the available commands:

* `ls` or `list`
* `mkdir` (behave as `mkdir -p`)
* `rmdir` or `rm` (both behave as `rm -rf`)
* `mv` (in slot mode, the second path is just the name of the file)
* `cp`
* `cl`, clear item (same as `rm`)
* `sw`, swap items
* `ul` or `upload`
* `dl` or `download`
* `rdl` or `rdownload` or `backup`

Keep in mind that not every filesystem implements all the commands. For instance, Elektron samples can not be swapped.

Provided paths must always be prepended with the device id and a colon (e.g., `0:/incoming`).

### Non-filesystem commands

* `ld` or `ls-devices`, list all MIDI devices with input and output

```
$ elektroid-cli ld
0: id: SYSTEM_ID; name: computer
1: id: hw:2,0,0; name: hw:2,0,0: Elektron Digitakt, Elektron Digitakt MIDI 1
2: id: hw:1,0,0; name: hw:1,0,0: M-Audio MIDISPORT Uno, M-Audio MIDISPORT Uno MIDI 1
3: id: hw:3,0,0; name: hw:3,0,0: MicroBrute, MicroBrute MicroBrute
4: id: hw:3,0,1; name: hw:3,0,1: MicroBrute, MicroBrute MicroBrute MIDI Inte
5: id: hw:4,0,0; name: hw:4,0,0: Little Phatty SE II, Little Phatty SE II MIDI 1
6: id: hw:5,0,0; name: hw:5,0,0: Summit, Summit MIDI 1
7: id: hw:3,0,0; name: hw:3,0,0: Arturia MicroFreak, Arturia MicroFreak Arturia Micr
```

* `info` or `info-device`, show device info including the compatible filesystems (filesystems implemented in the connector but not compatible with the  device are not shown). Notice that some filesystems are not meant to be used from the GUI so they are shown as `CLI only`.

```
$ elektroid-cli info 1
Type: MIDI
Device name: Elektron Digitakt
Device version: 1.51A
Device description: Digitakt
Connector name: elektron
Filesystems: sample, data (CLI only), project, sound
```

* `df` or `info-storage`, show size and use of +Drive and RAM

```
$ elektroid-cli df 1:/
Storage                         Size            Used       Available       Use%
+Drive                      959.5MiB        285.9MiB        673.6MiB     29.80%
RAM                            64MiB              0B           64MiB      0.00%
```

* `send` and `receive` work with a batch of SysEx messages. These are useful when working with generic devices, which have no filesystems implemented buf offer options to receive or send data.

```
$ elektroid-cli send file.syx 1
$ elektroid-cli receive 1 file.syx
```

* `upgrade`, upgrade firmware

```
$ elektroid-cli upgrade Digitakt_OS1.30.syx 1
```

* `play` and `record` work with stereo audio, the native sampling rate and the configured sample format.

```
$ elektroid-cli play audio.wav
$ elektroid-cli record audio.wav
```

### System connector

The first connector is always a system (local computer) one used to convert sample formats. It can be used like any other connector.

```
$ elektroid-cli system:wav-stereo-48k-16b:ul square.wav 0:/home/user/samples
```

### Elektron connector

These are the available filesystems for the elektron connector:

* `sample`
* `raw`
* `preset`
* `data`
* `project`
* `sound`

Raw and data are intended to interface directly with the filesystems provided by the devices so the downloaded or uploaded files are **not** compatible with Elektron Transfer formats. Preset is a particular instance of raw and so are project and sound but regarding data. Thus, raw and data filesystems should be used only for testing and are **not** available in the GUI.

#### Sample, raw and preset commands

* `elektron:sample:ls`

It only works for directories. Notice that the first column is the file type, the second is the size, the third is an internal cksum and the last one is the sample name.

```
$ elektroid-cli elektron:sample:ls 0:/
D              0B 00000000 drum machines
F       630.34KiB f8711cd9 saw
F         1.29MiB 0bbc22bd square
```

* `elektron:sample:mkdir`

```
$ elektroid-cli elektron:sample:mkdir 0:/samples
```

* `elektron:sample:rmdir`

```
$ elektroid-cli elektron:sample:rmdir 0:/samples
```

* `elektron:sample:ul`

```
$ elektroid-cli elektron:sample:ul square.wav 0:/
```

* `elektron:sample:dl`

```
$ elektroid-cli elektron:sample:dl 0:/square
```

* `elektron:sample:mv`

```
$ elektroid-cli elektron:sample:mv 0:/square 0:/sample
```

* `elektron:sample:rm`

```
$ elektroid-cli elektron:sample:rm 0:/sample
```

#### Data, sound and project commands

There are a few things to clarify first.

* All data commands are valid for both projects and sounds although the examples use just sounds.

* All data commands that use paths to items and not directories use the item index instead the item name.

Here are the commands.

* `elektron:data:ls`

It only works for directories. Notice that the first column is the file type, the second is the index, the third is the permissons in hexadecimal, the fourth indicates if the data in valid, the fifth indicates if it has metadatam, the sixth is the size and the last one is the item name.

Permissions are 16 bits values but only 6 are used from bit 2 to bit 7 both included. From LSB to MSB, this permissions are read, write, clear, copy, swap, and move.

```
$ elektroid-cli elektron:data:ls 0:/
D  -1 0000 0 0         0B projects
D  -1 0000 0 0         0B soundbanks
```

```
$ elektroid-cli elektron:data:ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
```

* `elektron:data:cp`

```
$ elektroid-cli elektron:data:cp 0:/soundbanks/D/1 0:/soundbanks/D/3
$ elektroid-cli elektron:data:ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B SNARE
F   3 0012 1 1       160B KICK
```

* `elektron:data:sw`

```
$ elektroid-cli elektron:data:sw 0:/soundbanks/D/2 0:/soundbanks/D/3
$ elektroid-cli elektron:data:ls 0:/soundbanks/D
F   1 0012 1 1       160B KICK
F   2 0012 1 1       160B KICK
F   3 0012 1 1       160B SNARE
```

* `elektron:data:mv`

```
$ elektroid-cli elektron:data:mv 0:/soundbanks/D/3 0:/soundbanks/D/1
$ elektroid-cli elektron:data:ls 0:/soundbanks/D
F   1 0012 1 1       160B SNARE
F   2 0012 1 1       160B KICK
```

* `elektron:data:cl`

```
$ elektroid-cli elektron:data:cl 0:/soundbanks/D/1
$ elektroid-cli elektron:data:ls 0:/soundbanks/D
F   2 0012 1 1       160B KICK
```

* `elektron:data:dl`

```
$ elektroid-cli elektron:data:dl 0:/soundbanks/D/1

$ elektroid-cli elektron:data:dl 0:/soundbanks/D/1 dir
```

* `elektron:data:ul`

```
$ elektroid-cli elektron:data:ul sound 0:/soundbanks/D
```
