# Elektroid

Elektroid is a Linux sample transfer application for Elektron devices. It includes the `elektroid` GUI application and the `elektroid-cli` CLI application.

## Installation

As with other autotools project, you need to run the following commands.

```
automake --add-missing
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

You can easily install them by running `sudo apt install automake libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev libtool`.

## CLI

`elektroid-cli` provides the same funcionality than `elektroid`. Provided paths must always be prepended with the card id and a colon (':') , e.g. `0:/samples`.
Here are the available commands.

### ld, list compatible devices

```
$ elektroid-cli ld
0 Elektron Digitakt MIDI 1
```

### mkdir

```
$ elektroid-cli mkdir 0:/samples
```

### rmdir

```
$ elektroid-cli rmdir 0:/samples
```

### upload

```
$ elektroid-cli upload square.wav 0:/
```

### download

```
$ elektroid-cli download 0:/square
```

### ls

```
$ elektroid-cli ls 0:/
F 0.09MB 3d71644d square
```

### mv

```
$ elektroid-cli mv 0:/square 0:/Square
```

### rm

```
$ elektroid-cli rm 0:/Square
```
