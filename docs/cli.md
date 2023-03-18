---
layout: default
title: CLI
permalink: /cli/
---

## CLI

`elektroid-cli` brings the same filesystem related functionality to the terminal.

### Lists the available devices

```
$ elektroid-cli ld
0: hw:1,0,0 hw:1,0,0: Elektron Digitakt, Elektron Digitakt MIDI 1
```

### Show information about the device, including the connector and the available filesystems

```
$ elektroid-cli info 0
Elektron Digitakt; version: 1.50; description: Digitakt; connector: elektron; filesystems: sample,data,project,sound
```

### Upload a sample to an Elektron device

```
$ elektroid-cli elektron-sample-ul square.wav 0:/waveforms
```

### Download a mono 16 bits sample from an SDS sampler

```
$ elektroid-cli sds-mono16-dl 0:/1
```

### List Novation Summit single patches in bank A.

```
$ elektroid-cli summit-single-ls 0:/A
```
