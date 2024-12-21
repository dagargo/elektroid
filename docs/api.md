---
layout: base
title: API
permalink: /api/
---

## API

Elektroid, although statically compiled, is extensible through three extension points.

* Connectors, which are a set of filesystems, each providing operations over MIDI and the computer native filesystem to implement uploading, downloading, renaming and the like. The API is defined in `src/connector.h`. Connectors are defined in the `src/connectors` directory and need to be configured in the connector registry in `src/regconn.c`.
* Menu actions (GUI only), which are device related buttons in the application menu that provide the user with some configuration window or launch device configuration processes. The API is defined in `src/maction.h`. Menu actions are defined in the `src/mactions` directory and need to be configured in the menu action registry in `src/regma.c`.
* Preferences, which are single configuration elements that are stored in the configuration JSON file and can be recalled from anywhere in the code. The API is defined in `src/preferences.h`. Preferences can be defined anywhere but need to be configured in the preferences registry in `src/regpref.c`.
