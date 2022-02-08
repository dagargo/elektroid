Name:		elektroid
Version:	2.0
Release:	1%{?dist}
Summary:	Sample transfer application for Elektron devices

License:	GPLv3+
URL:		https://github.com/dagargo/elektroid
Source0:	https://github.com/dagargo/elektroid/archive/elektroid-%{version}.tar.gz

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
Elektroid is a GNU/Linux sample transfer application for Elektron devices.
It includes the elektroid GUI application and the elektroid-cli CLI application.
Elektroid has been reported to work with Model:Samples, Digitakt and
Analog Rytm mk1 and mk2.


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
%{_bindir}/elektroid-cli
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
%{_mandir}/man1/elektroid-cli.1.gz
%{_mandir}/man1/elektroid.1.gz
%{_metainfodir}/%{name}.appdata.xml
%license COPYING


%changelog
* Mon Feb 07 2022 Jonathan Wakely <jwakely@fedoraproject.org> - 2.0-1
- RPM package for Fedora
