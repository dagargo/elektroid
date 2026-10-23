---
layout: base
title: Experimental persistent Digitakt session
permalink: /digitakt-session/
---

## Experimental persistent Digitakt session

`elektroid-cli dt-session DEVICE_INDEX` connects once and accepts commands
on standard input until `QUIT` or EOF. It lets external clients such as
dt-soundbank reuse Elektroid's existing transfer code and sample metadata API
without reconnecting for every operation. No Python client is included here.

### Commands

Fields are separated by literal tabs, with one command per line. File and
device paths must not contain tabs or newlines. Device paths are filesystem
paths without a device-number prefix; the device is selected at startup.

| Command | Fields after command | Operation |
| --- | --- | --- |
| `SAMPLE_UL` | local file, remote sample path | Upload a sample |
| `META` | remote sample path | Retrieve sample metadata |
| `DATA_CL` | target sound slot | Clear one sound slot |
| `DATA_UL` | local sound file, target sound slot | Upload a sound |
| `QUIT` | none | Close the session |

After connecting, the command emits `DT_SESSION_READY`. Each operation ends
with `DT_OK<TAB>COMMAND` or `DT_ERROR<TAB>COMMAND<TAB>ERROR`. `META` also emits
`SAMPLE_INFO id=XXXXXXXX size=DECIMAL hash=XXXXXXXX`, with hexadecimal ID and
hash. `QUIT` emits `DT_SESSION_BYE`. Clients must tolerate ordinary transfer
output before the completion marker and flush commands when using pipes.

`DATA_CL` and `DATA_UL` accept only individual paths from `/soundbanks/A/1`
through `/soundbanks/H/256`. Bank-wide and project paths are rejected. Leading
zeros in slot numbers are accepted. Sample operations use existing filesystem
validation rather than these sound-slot restrictions.

### Safety and limitations

This is a low-level, experimental, synchronous protocol, not a transaction or
bank manager. It does not generate sounds, prompt for confirmation, back up
data, roll back changes or download uploaded sounds for verification. Clearing
a slot and uploading its replacement are separate operations; failure after a
successful clear can leave that slot empty. Clients must provide their own
preview, authorization and backup policy before sending write commands.

The session reports operation errors and can accept another command. Clients
that require stop-on-error must stop issuing commands themselves. The exit
status reflects the last operation, not an accumulated failure count. Lines
too long for the 16 KiB input buffer terminate the session with an error.
There is no additional protocol-level timeout; existing backend cancellation
and timeout behavior applies. Do not use this interface concurrently with
another MIDI transfer client for the same device.

The existing implementation was previously exercised on Digitakt I firmware
1.53 through an external generator, including sample upload, metadata retrieval,
targeted replacement and loading/playing the resulting sounds. Extracting this
contribution involved no new hardware writes. Other Elektron models are not
claimed as validated.

### Contribution dependencies

This contribution is based on
[#261: sample metadata retrieval](https://github.com/dagargo/elektroid/pull/261),
whose `get_sample_metadata` operation is used by `META`. It should be reviewed
and merged after that API. No batch metadata command is required or included.

[#260: untagged sound export](https://github.com/dagargo/elektroid/pull/260)
is the other related contribution. It fixes sample inclusion when exporting
untagged sounds, which complements external soundbank workflows, but is not a
technical dependency of this session protocol and is not included in this diff.

### Offline validation

`make check` includes argument validation before any device connection and a
compiled test of allowed/rejected sound-slot paths. These tests do not exercise
actual transfers, device disconnection or error recovery on hardware.
