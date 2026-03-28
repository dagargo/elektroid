---
layout: base
title: Contributing
permalink: /contributing/
order: 6
---

## Contributing

### Code formatting

Elektroid uses the default indenting style provided by `indent`, which is the GNU style. It should be applied when submitting contributions by running `make format`.

### Tests

Elektroid includes automated integration tests for the supported devices and filesystems that ideally should be implemented when new devices are added.

In order to run a test, proceed as follows. The variable `TEST_DEVICE` must contain the device id and variable `TEST_CONNECTOR_FILESYSTEM` must contain the connector name, an underscore char (`_`) and the filesystem name.

```
$ TEST_DEVICE=0 TEST_CONNECTOR_FILESYSTEM=efactor_preset make check
```

Running `make check` without setting any of these variables will run some system integration tests together with a few unit tests.

### Documentation

`README.md` file is generated from the `docs` dir, which contains the web page of the project in Jekyll format, so modify the required page and update the `README.md` by running `make clean; make` from the `docs` directory.
