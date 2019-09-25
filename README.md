# Elektroid

Elektroid is a Linux sample transfer application for Elektron devices.

## Installation

As with other autotools project, you need to run the following commands.

```
autoreconf
./configure
make
sudo make install
```

The package dependencies for Debian based distributions are:
- autotools
- libgtk-3-dev
- libpulse-dev
- libsndfile1-dev
- libsamplerate0-dev

You can easily install them by running `sudo apt-get install autotools libgtk-3-dev libpulse-dev libsndfile1-dev libsamplerate0-dev`.
