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

You can easily install them by running `sudo apt-get install automake libasound2-dev libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev`.

## CLI

`elektroid-cli` provides the same funcionality than `elektroid`. Provided paths must always be prepended with the card id and a colon (':') , e.g. `0:/samples`.
Here are some usage examples.

### List compatible devices

```
$ elektroid-cli ld
0 Elektron Digitakt MIDI 1
```

### List a directory

```
$ elektroid-cli ls 0:/
0.00MB samples
```

### Create a directory

```
$ elektroid-cli mkdir 0:/samples/new
```

### Rename a file

```
$ elektroid-cli mv 0:/samples/new 0:/samples/old
```
