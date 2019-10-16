# Elektroid

Elektroid is a Linux sample transfer application for Elektron devices.

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
