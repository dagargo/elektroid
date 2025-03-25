# Elektroid

[//]: # (Do not modify this file manually.)
[//]: # (This file is generated from the docs directory by executing `make`.)

Elektroid is a sample and MIDI device manager. It includes the `elektroid` GUI application and the `elektroid-cli` CLI application.

![Elektroid GUI screenshot](docs/images/screenshot.png "Elektroid GUI")

Elektroid started as a FLOSS Elektron Transfer alternative and it has ended up supporting other devices from different vendors in the same fashion.

These are the supported devices:

* Elektron Model:Samples
* Elektron Model:Cycles
* Elektron Digitakt I and II
* Elektron Digitone and Digitone Keys
* Elektron Syntakt
* Elektron Analog Rytm MKI and MKII
* Elektron Analog Four MKI, MKII and Keys
* Elektron Analog Heat MKI, MKII and +FX
* All samplers implementing MIDI SDS
* Casio CZ-101
* Arturia MicroBrute
* Arturia MicroFreak
* Eventide ModFactor, PitchFactor, TimeFactor, Space and H9
* Moog Little Phatty and Slim Phatty
* Novation Summit and Peak

While Elektroid is already available in some GNU/Linux distributions such as Debian or Ubuntu, it can also be easily installed on other distributions via Flatpak.

## Installation

As with other autotools project, you need to run the following commands. If you just want to compile `elektroid-cli`, pass `CLI_ONLY=yes` to `./configure`.

```
autoreconf --install
./configure
make
sudo make install
```

The package dependencies for Debian-based distributions are:
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
- libzip-dev

You can easily install them by running `sudo apt install automake libtool build-essential libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev autopoint gettext zlib1g-dev libjson-glib-dev libzip-dev`.

If you are only compiling the CLI, install the dependencies with `sudo apt install automake libtool build-essential libasound2-dev libglib2.0-dev libsndfile1-dev libsamplerate0-dev autopoint libtool zlib1g-dev libjson-glib-dev libzip-dev`.

For Fedora, run `sudo dnf install autoconf libtool alsa-lib-devel zlib-devel libzip-devel gtk3-devel libsndfile-devel gettext-devel libsamplerate-devel pulseaudio-libs-devel json-glib-devel` to install the build dependencies.

For Arch Linux, run `sudo pacman -S base-devel autoconf libtool alsa-lib zlib libzip gtk3 libsndfile gettext libsamplerate json-glib` to install the build dependencies.

For MSYS2 (UCRT64), run `pacman -S mingw-w64-x86_64-toolchain gettext gettext-devel libtool pkg-config mingw-w64-x86_64-autotools mingw-w64-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gtk3 mingw-w64-x86_64-json-glib mingw-w64-ucrt-x86_64-libsndfile mingw-w64-ucrt-x86_64-rtmidi mingw-w64-ucrt-x86_64-rtaudio mingw-w64-ucrt-x86_64-libsamplerate` to install the build dependencies.

For OSX (Homebrew), run `homebrew install automake bltool pkg-config gtk+3 libsndfile libsamplerate gettext zlib json-glib libzip rtaudio rtmidi`.

### MIDI backend

By default, Elektroid uses ALSA as the MIDI backend on Linux and RtMidi on other OSs. To use RtMidi on Linux, pass `RTMIDI=yes` to `./configure`. In this case, the RtMidi development package will be needed (`librtmidi-dev` on Debian).

### Audio server

By default, Elektroid uses PulseAudio as the audio server on Linux and RtAudio on other OSs. To use RtAudio on Linux, pass `RTAUDIO=yes` to `./configure`. In this case, the RtAudio development package will be needed (`librtaudio-dev` on Debian).

### Adding and reconfiguring Elektron devices

Since version 2.1, it is possible to add and reconfigure devices without recompiling as the device definitions are stored in a JSON file. Hopefully, this approach will make it easier for users to modify and add devices and new releases will only be needed if new funcionalities are actually added.

This is a device definition from `res/elektron/devices.json`.

```
}, {
        "id": 12,
        "name": "Digitakt",
        "alias": "dt",
        "filesystems": 57,
        "storage": 3
}, {
```

Properties `filesystems` and `storage` are based on the definitions found in `src/connectors/elektron.h` and are the bitwise OR result of all the supported filesystems and storage types.

```
enum connector_fs
{
  FS_SAMPLES = 0x1,
  FS_RAW_ALL = 0x2,
  FS_RAW_PRESETS = 0x4,
  FS_DATA_ALL = 0x8,
  FS_DATA_PRJ = 0x10,
  FS_DATA_SND = 0x20,
};
```

```
enum connector_storage
{
  STORAGE_PLUS_DRIVE = 0x1,
  STORAGE_RAM = 0x2
};
```

If the file `~/.config/elektroid/elektron/devices.json` is found, it will take precedence over the installed one.

## Packaging

This is a quick glance at the instructions needed to build some distribution packages.

### Debian

```
$ dpkg-buildpackage -b -rfakeroot -us -uc
```

### Fedora

```
$ rel=35
$ mock -r fedora-$rel-x86_64 --buildsrpm --spec rpm/elektroid.spec --sources .
$ mock -r fedora-$rel-x86_64 --no-clean --rebuild /var/lib/mock/fedora-$rel-x86_64/result/elektroid-*.src.rpm
```

### Flatpak

There is an official Flathub repository in https://github.com/flathub/io.github.dagargo.Elektroid so installing the Flatpak is as easy as running `flatpak install flathub io.github.dagargo.Elektroid`.

From that repository, building and installing the Flatpak can be done with `flatpak-builder --user --install --force-clean flatpak/build io.github.dagargo.Elektroid.yaml`.

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
* `rdl` or `rdownload` or `backup`

Keep in mind that not every filesystem implements all the commands. For instance, Elektron samples can not be swapped.

Provided paths must always be prepended with the device id and a colon (e.g., `0:/incoming`).

### Device commands

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

### System connector

The first connector is always a system (local computer) one used to convert sample formats. It can be used like any other connector.

```
$ elektroid-cli system-wav48k16b2c-ul square.wav 0:/home/user/samples
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

$ elektroid-cli elektron-data-dl 0:/soundbanks/D/1 dir
```

* `elektron-data-ul`

```
$ elektroid-cli elektron-data-ul sound 0:/soundbanks/D
```

## API

Elektroid, although statically compiled, is extensible through three extension points.

* Connectors, which are a set of filesystems, each providing operations over MIDI and the computer native filesystem to implement uploading, downloading, renaming and the like. The API is defined in `src/connector.h`. Connectors are defined in the `src/connectors` directory and need to be configured in the connector registry in `src/regconn.c`.
* Menu actions (GUI only), which are device related buttons in the application menu that provide the user with some configuration window or launch device configuration processes. The API is defined in `src/maction.h`. Menu actions are defined in the `src/mactions` directory and need to be configured in the menu action registry in `src/regma.c`.
* Preferences, which are single configuration elements that are stored in the configuration JSON file and can be recalled from anywhere in the code. The API is defined in `src/preferences.h`. Preferences can be defined anywhere but need to be configured in the preferences registry in `src/regpref.c`.

## Tests

Elektroid includes automated integration tests for the supported devices and filesystems.

In order to run a test, proceed as follows. The variable `TEST_DEVICE` must contain the device id and variable `TEST_CONNECTOR_FILESYSTEM` must contain the connector name, an underscore char (`_`) and the filesystem name.

```
$ TEST_DEVICE=0 TEST_CONNECTOR_FILESYSTEM=efactor_preset make check
```

Running `make check` without setting any of these variables will run some system integration tests together with a few unit tests.
