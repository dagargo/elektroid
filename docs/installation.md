---
layout: base
title: Installation
permalink: /installation/
order: 1
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

You can easily install them by running this.

```
sudo apt install automake libtool build-essential libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev autopoint gettext zlib1g-dev libjson-glib-dev libzip-dev`
```

If you are only compiling the CLI, install the dependencies with this.

```
sudo apt install automake libtool build-essential libasound2-dev libglib2.0-dev libsndfile1-dev libsamplerate0-dev autopoint libtool zlib1g-dev libjson-glib-dev libzip-dev
```

For Fedora, run this to install the build dependencies.

```
sudo dnf install autoconf libtool alsa-lib-devel zlib-devel libzip-devel gtk3-devel libsndfile-devel gettext-devel libsamplerate-devel pulseaudio-libs-devel json-glib-devel
```

For Arch Linux, run this to install the build dependencies.

```
sudo pacman -S base-devel autoconf libtool alsa-lib zlib libzip gtk3 libsndfile gettext libsamplerate json-glib`
```

For OSX (Homebrew), run this to install the build dependencies.

```
homebrew install automake bltool pkg-config gtk+3 libsndfile libsamplerate gettext zlib json-glib libzip rtaudio rtmidi
```

For MSYS2 (UCRT64), run this to install the build dependencies.

```
pacman -S mingw-w64-x86_64-toolchain gettext gettext-devel libtool pkg-config mingw-w64-x86_64-autotools mingw-w64-x86_64-gcc mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-gtk3 mingw-w64-x86_64-json-glib mingw-w64-ucrt-x86_64-libsndfile mingw-w64-ucrt-x86_64-rtmidi mingw-w64-ucrt-x86_64-rtaudio mingw-w64-ucrt-x86_64-libsamplerate
```

#### Additional notes on MSYS2 (UCRT64)

* Repository must be cloned with `-c core.symlinks=true` option for the included symbolic links to work.
* `LANG` environmente variable must be set as in `LANG=es_ES.UTF-8` depending on your installed locales.
* A shortcut could be manually created and use the `ico` file included in the installation for this purpose. It is recommended to set the `target` property to `C:\msys64\usr\bin\mintty.exe -w hide /bin/env MSYSTEM=UCRT64 LANG=es_ES.UTF-8 /bin/bash -l -i -c /ucrt64/bin/elektroid.exe` and the `start in` property to `C:\msys64\usr\bin` or the equivalent directories in your installation.

### MIDI backend

By default, Elektroid uses ALSA as the MIDI backend on Linux and RtMidi on other OSs. To use RtMidi on Linux, pass `RTMIDI=yes` to `./configure`. In this case, the RtMidi development package will be needed (`librtmidi-dev` on Debian).

### Audio backend

By default, Elektroid uses PulseAudio as the audio server on Linux and RtAudio on other OSs. To use RtAudio on Linux, pass `RTAUDIO=yes` to `./configure`. In this case, the RtAudio development package will be needed (`librtaudio-dev` on Debian).

### Adding and reconfiguring Elektron devices

It is possible to add and reconfigure Elektron devices without recompiling as the device definitions are first searched within the `~/.config/elektroid/elektron/devices.json` JSON file. If the file is not found, the installed one will be used. Hopefully, this approach will make it easier for users to modify and add devices and new releases will only be needed if new funcionalities are actually added.

This is a device definition from `res/elektron/devices.json`.

```
  {
    "id": 12,
    "name": "Elektron Digitakt",
    "filesystems": {
      "sample": null,
      "data": null,
      "project": [
        "dtprj"
      ],
      "sound": [
        "dtsnd"
      ]
    },
    "storage": [
      "+Drive",
      "RAM"
    ]
  }
```

The list represents the allowed file extensions for the given filesystem.
