---
layout: base
title: Tests
permalink: /tests/
---

## Tests

Elektroid includes automated integration tests for the supported devices and filesystems.

In order to run a test, proceed as follows. The variable `TEST_DEVICE` must contain the device id and variable `TEST_CONNECTOR_FILESYSTEM` must contain the connector name, an underscore char (`_`) and the filesystem name.

```
$ TEST_DEVICE=0 TEST_CONNECTOR_FILESYSTEM=efactor_preset make check
```

Running `make check` without setting any of these variables will run some system integration tests together with a few unit tests.
