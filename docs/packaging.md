---
layout: default
title: Packaging
permalink: /packaging/
---

## Packaging

This is a quick glance at the instructions needed to build some distribution packages.

### Debian

```
$ dpkg-buildpackage -b -rfakeroot -us -uc
```

### Fedora

```
$ rel=35
$ mock -r fedora-$rel-x86_64 --buildsrpm --spec rpm/elektroid.spec --sources .
$ mock -r fedora-$rel-x86_64 --no-clean --rebuild /var/lib/mock/fedora-$rel-x86_64/result/elektroid-*.src.rpm
```

### Flatpack

To build a standalone Flatpak application, run `flatpak-builder --user --install --force-clean flatpak/build flatpak/io.github.dagargo.Elektroid.yaml`
and then you can use `flatpak run io.github.dagargo.Elektroid` (add `--cli` and extra arguments for the CLI utility).
