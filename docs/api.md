---
layout: base
title: API
permalink: /api/
order: 4
---

## API

Elektroid, although statically compiled, is extensible through three extension points: connector, menu action and preference.

### Connector

Connectors are a set of filesystems, each providing operations over MIDI and the computer native filesystem to implement uploading, downloading, renaming and the like. The API is defined in [src/connector.h](https://github.com/dagargo/elektroid/tree/master/src/connector.h). Connectors are defined in the [src/connectors](https://github.com/dagargo/elektroid/tree/master/src/connectors) directory and need to be configured in the connector registry in [src/regconn.c](https://github.com/dagargo/elektroid/tree/master/src/regconn.c).

A simple example of this extension can be seen in [src/connectors/padkontrol.c](https://github.com/dagargo/elektroid/tree/master/src/connectors/padkontrol.c).

### Menu action

Menu actions (GUI only) are device related buttons in the application menu that provide the user with some configuration window or launch device configuration processes. The API is defined in [src/maction.h](https://github.com/dagargo/elektroid/tree/master/src/maction.h). Menu actions are defined in the [src/mactions](https://github.com/dagargo/elektroid/tree/master/src/mactions) directory and need to be configured in the menu action registry in [src/regma.c](https://github.com/dagargo/elektroid/tree/master/src/regma.c).

### Preference

Preferences are single configuration elements that are stored in the configuration JSON file and can be recalled from anywhere in the code. The API is defined in [src/preferences.h](https://github.com/dagargo/elektroid/tree/master/src/preferences.h). Preferences can be defined anywhere but need to be configured in the preferences registry in [src/regpref.c](https://github.com/dagargo/elektroid/tree/master/src/regpref.c).
