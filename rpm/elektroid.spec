# set this to bcond_with to merge the 2 packages
%bcond_without split_cli

Name:           elektroid
Version:        3.4
Release:        0
Summary:        A sample and MIDI device manager
# FIXME: Select a correct license from https://github.com/openSUSE/spec-cleaner#spdx-licenses
License:        GPL-3.0-only
URL:            https://dagargo.github.io/elektroid/
Source0:        https://github.com/dagargo/elektroid/releases/download/%{version}/elektroid-%{version}.tar.gz
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(alsa) >= 1.1.3
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(json-glib-1.0)
BuildRequires:  pkgconfig(libzip) >= 1.1.2
BuildRequires:  pkgconfig(rtaudio)
BuildRequires:  pkgconfig(rtmidi)
BuildRequires:  pkgconfig(rubberband) >= 3.3
BuildRequires:  pkgconfig(samplerate) >= 0.1.8
BuildRequires:  pkgconfig(sndfile) >= 1.0.2
BuildRequires:  pkgconfig(zlib) >= 1.1.8
BuildRequires:  pkgconfig(libpulse-mainloop-glib)
%if %{with split_cli}
Requires:       elektroid-cli = %{version}
%else
Provides:       elektroid-cli = %{version}-%{release}
Obsoletes:      elektroid-cli < %{version}-%{release}
%endif

%description
Elektroid started as a FLOSS Elektron Transfer alternative and it has ended up
supporting other devices from different vendors in the same fashion.

These are the supported devices:

- Arturia MicroBrute
- Arturia MicroFreak
- Casio CZ-101
- Elektron Analog Four MKI, MKII and Keys
- Elektron Analog Heat MKI, MKII and +FX
- Elektron Analog Rytm MKI and MKII
- Elektron Digitakt I and II
- Elektron Digitone I and II and Digitone Keys
- Elektron Model:Cycles
- Elektron Model:Samples
- Elektron Monomachine MKII
- Elektron Syntakt
- Eventide ModFactor, PitchFactor, TimeFactor, Space and H9
- KORG padKONTROL
- KORG prologue, minilogue xd and NTS-1 (units only)
- KORG Volca Sample and Volca Sample 2
- MFB Tanzmaus
- Moog Little Phatty and Slim Phatty
- Novation Summit and Peak
- Samplers implementing MIDI SDS

Other interesting features are:
- Autosampler with SFZ file generation
- Sample playback, recording and basic edition
- Sample loop points edition with zero crossing detection
- Sample tagging
- Sample zoom
- Search within devices
- SysEx transmission and reception

%if %{with split_cli}
%package cli
Summary:        A sample and MIDI device manager - commandline tools

%description cli
Elektroid started as a FLOSS Elektron Transfer alternative and it has ended up
supporting other devices from different vendors in the same fashion.

This package holds the commandline tool.

%endif

%prep
%autosetup -p1

%build
%configure
%make_build

%install
%make_install
install -D -m 0644 -t %{buildroot}%{_mandir}/man1/ man/*.1

%find_lang %{name}

%if %{with split_cli}
%files
%else
%files -f %{name}.lang
%endif
%license COPYING
#doc README.md THANKS CONTRIBUTING.md AUTHORS
%{_bindir}/elektroid
%{_datadir}/elektroid/
# avoid duplicated files between the 2 packages
%if %{with split_cli}
%exclude %{_datadir}/%{name}/elektron/devices.json
%endif
%{_datadir}/icons/hicolor/scalable/apps/elektroid-wavetable-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-wave-loop-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-wave-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-track-loop-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-track-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-slice-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-settings-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-sequence-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-project-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-preset-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-oscillator-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-keys-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-fx-reverb-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-fx-modulation-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-fx-delay-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-folder-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-file-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/elektroid-chip-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/io.github.dagargo.Elektroid-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/io.github.dagargo.Elektroid.svg
%{_datadir}/metainfo/io.github.dagargo.Elektroid.appdata.xml
%{_datadir}/applications/io.github.dagargo.Elektroid.desktop
%{_mandir}/man1/elektroid.1*

%if %{with split_cli}
%files cli -f %{name}.lang
%license COPYING
%{_datadir}/%{name}/elektron/devices.json
%endif
%{_bindir}/elektroid-cli
%{_mandir}/man1/elektroid-cli.1*


%changelog
* Sat Mar 11 2023 David García Goñi <dagargo@gmail.com> - 2.5-1
- Update to 2.5 release

* Wed Jun 08 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.1-1
- Update to 2.1 release

* Wed Jun 08 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.0-2
- Add subpackage for elektroid-cli

* Mon Feb 07 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.0-1
- RPM package for Fedora
