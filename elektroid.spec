Name:		elektroid
Version:	2.0
Release:	2%{?dist}
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
Elektroid is an transfer application for Elektron devices.
With Elektroid you can easily upload and download audio files, projects, sounds
and presets to and from Elektron devices. It can also be used to send and
receive MIDI SysEx files.
Elektroid has been reported to work with Model:Samples, Model:Cycles, Digitakt,
Digitone and Analog Rytm MKI and MKII.

%package cli
Summary: Transfer application for Elektron devices

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
%{_datadir}/%{name}/res/gui.css
%{_datadir}/%{name}/res/gui.glade
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
%{_datadir}/locale/*/LC_MESSAGES/%{name}.mo
%{_mandir}/man1/elektroid-cli.1.gz
%license COPYING


%changelog
* Wed Jun 08 2022 Jonathan Wakely <jwakely@redhat.com> - 2.0-2
- Add subpackage for elektroid-cli

* Mon Feb 07 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.0-1
- RPM package for Fedora
