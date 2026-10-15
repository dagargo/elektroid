# Elektron sample metadata

Elektroid can retrieve metadata for samples stored on supported
Elektron devices through a MIDI connection.

The CLI operation is named `sample-info`.

## Requirements

The device must expose the Elektron `sample` filesystem and support
the metadata requests used by the connector.

This feature has been validated on a Digitakt I running firmware 1.53.
Compatibility with other Elektron models and firmware versions has
not been established by this validation.

## Find the device

List available devices:

```sh
elektroid-cli ld
```

Identify the number corresponding to your Elektron instrument.

The following examples use device number `13`. This number depends
on the connected devices and may be different on your system.

## Find a sample

List the root of the sample filesystem:

```sh
elektroid-cli elektron:sample:ls '13:/'
```

List a sample directory:

```sh
elektroid-cli elektron:sample:ls '13:/01_KICKS'
```

Use the exact path reported by the device.

## Retrieve metadata

Command syntax:

```sh
elektroid-cli elektron:sample:sample-info \
  'DEVICE_NUMBER:/PATH/TO/SAMPLE'
```

Example validated on Digitakt I firmware 1.53:

```sh
elektroid-cli elektron:sample:sample-info \
  '13:/01_KICKS/0001-01w drums - AMB K'
```

Observed output:

```text
id=543
size=35172
hash=e3e6f3c1
```

The output contains three `key=value` lines:

- `id`: unsigned decimal sample identifier.
- `size`: unsigned decimal sample size, in bytes.
- `hash`: 32-bit sample hash, printed as eight hexadecimal digits.

The hash algorithm is not specified by this interface.

The values above belong to the sample used for validation.
Other samples will return different values.

## Shell scripting

The output can be captured and processed by shell scripts:

```sh
metadata=$(elektroid-cli elektron:sample:sample-info \
  '13:/01_KICKS/0001-01w drums - AMB K') || exit 1

printf '%s\n' "$metadata"
```

Check the exit status before processing the output.

## Error handling

The command returns a nonzero exit status if the operation fails.

Possible causes include:

- An invalid or unavailable device.
- A filesystem that does not expose metadata retrieval.
- A sample path that does not exist.
- A MIDI communication failure.
- A malformed or unsuccessful MIDI response.

For a missing sample, the CLI can report:

```text
Error: No such file or directory
```

Errors are printed to standard error.

## Protocol

The Elektron connector retrieves metadata using two requests:

- `0x22`: sample information, including size and identifier.
- `0x28`: sample hash information.

The connector validates the responses and returns the metadata
through the filesystem operation `get_sample_metadata`.

The CLI does not implement the MIDI protocol directly.

## Compatibility

A successful retrieval has been observed on a Digitakt I
running firmware 1.53.

This confirms the tested device, firmware and sample path.
It does not establish compatibility with every Elektron device.
