%{!?driver_version:%global driver_version 0.1.0}
%{!?driver_release:%global driver_release 1}
# Version of the bundled SANE build infrastructure, independent of this RPM.
%global sane_version 1.4.0

Name:           sane-backends-plustek8200i
Version:        %{driver_version}
Release:        %{driver_release}%{?dist}
Summary:        SANE backend for the GL128 Plustek OpticFilm 8200i
# The backend itself is LGPL-2.1-or-later. The library also links SANE's own support
# code (sanei), which is GPL-2.0-or-later with the SANE exception.
License:        LGPL-2.1-or-later AND GPL-2.0-or-later WITH SANE-exception
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  autoconf-archive
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  gettext-devel
BuildRequires:  pkgconf-pkg-config
BuildRequires:  libusb1-devel
BuildRequires:  libxml2-devel
BuildRequires:  python3
BuildRequires:  systemd-rpm-macros
Requires:       sane-backends%{?_isa}
Requires:       sane-backends-libs%{?_isa}

%description
An additional SANE backend for the Plustek OpticFilm 8200i with the
Genesys GL128 controller (USB 07b3:1825). It works with Fedora's standard
SANE library and scanning applications. The older GL845 model
(USB 07b3:130d) is supported by the standard genesys backend.

%prep
%autosetup
# Both LICENSE files would install under one name; give ours its own.
cp -p LICENSE LICENSE.LGPL-2.1
cd backends
# The source snapshot has no Git metadata or generated Autotools files.
printf '%%s\n' '%{sane_version}' > .tarball-version
bash -e ./autogen.sh

%build
cd backends
BACKENDS=plustek8200i %configure \
    --disable-static \
    --disable-preload \
    --with-usb=yes \
    --without-avahi \
    --without-libcurl \
    --without-poppler-glib \
    --without-snmp
%make_build -C lib
%make_build -C sanei sanei_init_debug.lo sanei_constrain_value.lo sanei_config.lo sanei_usb.lo
%make_build -C backend libsane-plustek8200i.la plustek8200i.conf
%make_build -C doc sane-plustek8200i.5

%check
cd backends
%make_build -C testsuite/backend/plustek8200i check
# Resolve every symbol without opening or moving a connected scanner.
python3 -c 'import ctypes, os; ctypes.CDLL("backend/.libs/libsane-plustek8200i.so.1", mode=os.RTLD_NOW)'

%install
# Stage only this backend: a full SANE install would replace system files.
install -Dpm0755 backends/backend/.libs/libsane-plustek8200i.so.%{sane_version} \
    %{buildroot}%{_libdir}/sane/libsane-plustek8200i.so.%{sane_version}
ln -s libsane-plustek8200i.so.%{sane_version} \
    %{buildroot}%{_libdir}/sane/libsane-plustek8200i.so.1
ln -s libsane-plustek8200i.so.1 \
    %{buildroot}%{_libdir}/sane/libsane-plustek8200i.so
install -Dpm0644 backends/backend/plustek8200i.conf \
    %{buildroot}%{_sysconfdir}/sane.d/plustek8200i.conf
install -Dpm0644 packaging/rpm/plustek8200i \
    %{buildroot}%{_sysconfdir}/sane.d/dll.d/plustek8200i
install -Dpm0644 packaging/rpm/64-plustek8200i.rules \
    %{buildroot}%{_udevrulesdir}/64-plustek8200i.rules
install -Dpm0644 backends/doc/sane-plustek8200i.5 \
    %{buildroot}%{_mandir}/man5/sane-plustek8200i.5

%files
%license LICENSE.LGPL-2.1 backends/COPYING backends/LICENSE
%doc README.rpm.md
%config(noreplace) %{_sysconfdir}/sane.d/plustek8200i.conf
%config(noreplace) %{_sysconfdir}/sane.d/dll.d/plustek8200i
%{_libdir}/sane/libsane-plustek8200i.so*
%{_udevrulesdir}/64-plustek8200i.rules
%{_mandir}/man5/sane-plustek8200i.5*

%changelog
* Wed Oct 07 2026 Christian Schaller <cschalle@redhat.com> - 0.1.0-1
- Package the GL128 OpticFilm 8200i backend alongside Fedora SANE.
