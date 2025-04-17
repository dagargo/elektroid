---
layout: base
title: Installation
permalink: /installation/
---

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

For OSX (Homebrew), run `homebrew install automake bltool pkg-config gtk+3 libsndfile libsamplerate gettext zlib json-glib libzip rtaudio rtmidi`.

For MSYS2 (UCRT64), run `pacman -S mingw-w64-x86_64-toolchain gettext gettext-devel libtool pkg-config mingw-w64-x86_64-autotools mingw-w64-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gtk3 mingw-w64-x86_64-json-glib mingw-w64-ucrt-x86_64-libsndfile mingw-w64-ucrt-x86_64-rtmidi mingw-w64-ucrt-x86_64-rtaudio mingw-w64-ucrt-x86_64-libsamplerate` to install the build dependencies.

#### Additional notes on MSYS2 (UCRT64)

* Repository must be cloned with `-c core.symlinks=true` option for the included symbolic links to work.
* `LANG` environmente variable must be set as in `LANG=es_ES.UTF-8` depending on your installed locales.
* A shortcut could be manually created and use the `ico` file included in the installation for this purpose. It is recommended to set the `target` property to `C:\msys64\usr\bin\mintty.exe -w hide /bin/env MSYSTEM=UCRT64 LANG=es_ES.UTF-8 /bin/bash -l -i -c /ucrt64/bin/elektroid.exe` and the `start in` property to `C:\msys64\usr\bin` or the equivalent directories in your installation.

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
