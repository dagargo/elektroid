---
layout: base
title: Packaging
permalink: /packaging/
order: 3
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

### Flatpak

There is an official Flathub repository in https://github.com/flathub/io.github.dagargo.Elektroid so installing the Flatpak is as easy as running `flatpak install flathub io.github.dagargo.Elektroid`.

From that repository, building and installing the Flatpak can be done with `flatpak-builder --user --install --force-clean flatpak/build io.github.dagargo.Elektroid.yaml`.
