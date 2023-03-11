Name:		elektroid
Version:	2.1
Release:	1%{?dist}
Summary:	Transfer application for Elektron devices

License:	GPLv3+
URL:		https://github.com/dagargo/elektroid
Source0:	https://github.com/dagargo/elektroid/releases/download/%{version}/%{name}-%{version}.tar.gz

BuildRequires:	autoconf
BuildRequires:	libtool
BuildRequires:	alsa-lib-devel
BuildRequires:	zlib-devel
BuildRequires:	libzip-devel
BuildRequires:	gtk3-devel
BuildRequires:	libsndfile-devel
BuildRequires:	libsamplerate-devel
%if 0%{?suse_version}
BuildRequires:	libpulse-devel
%else
%if 0%{?mgaversion}
BuildRequires:	libpulseaudio-devel
%else
# RHEL, CentOS and Fedora use this name:
BuildRequires:	pulseaudio-libs-devel
%endif
%endif
BuildRequires:	gettext-devel
BuildRequires:	json-glib-devel

%description
Elektroid is a sample and MIDI device manager.
With Elektroid you can easily upload and download audio files and manage
different types of data on different MIDI devices, such as presets, projects or
tunings. It can also be used to send and receive SysEx MIDI files.

%package cli
Summary: Sample and MIDI device manager

%description cli
This is the command-line client for Elektroid.


%prep
%autosetup -p1
sed -i s/^include_HEADERS/noinst_HEADERS/ src/Makefile.am
aclocal
automake


%build
%configure
%make_build


%install
%make_install


%files
%{_bindir}/elektroid
%{_datadir}/applications/%{name}.desktop
%{_datadir}/%{name}/elektron/devices.json
%{_datadir}/%{name}/microbrute/gui.glade
%{_datadir}/%{name}/gui.css
%{_datadir}/%{name}/gui.glade
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_datadir}/icons/hicolor/scalable/apps/%{name}-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/%{name}-data-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/%{name}-project-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/%{name}-sound-symbolic.svg
%{_datadir}/icons/hicolor/scalable/apps/%{name}-wave-symbolic.svg
%{_datadir}/locale/*/LC_MESSAGES/%{name}.mo
%{_mandir}/man1/elektroid.1.gz
%{_metainfodir}/%{name}.appdata.xml
%license COPYING

%files cli
%{_bindir}/elektroid-cli
%{_datadir}/%{name}/elektron/devices.json
%{_datadir}/locale/*/LC_MESSAGES/%{name}.mo
%{_mandir}/man1/elektroid-cli.1.gz
%license COPYING


%changelog
* Sat Mar 11 2023 David García Goñi <dagargo@gmail.com> - 2.5-1
- Update to 2.5 release

* Wed Jun 08 2022 Jonathan Wakely <jwakely@redhat.com> - 2.1-1
- Update to 2.1 release

* Wed Jun 08 2022 Jonathan Wakely <jwakely@redhat.com> - 2.0-2
- Add subpackage for elektroid-cli

* Mon Feb 07 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.0-1
- RPM package for Fedora
