# Standalone OpticFilm 8200i RPM for Fedora

This package adds the `plustek8200i` backend for the newer GL128 scanner
(USB `07b3:1825`) to Fedora's standard SANE installation. The older GL845
OpticFilm 8200i (`07b3:130d`) uses SANE's existing `genesys` backend.

## Build

Install the build dependencies on Fedora:

```sh
sudo dnf install rpm-build redhat-rpm-config gcc gcc-c++ make autoconf \
  autoconf-archive automake libtool gettext-devel pkgconf-pkg-config \
  libusb1-devel libxml2-devel python3 systemd-rpm-macros
```

From the repository root, build without root privileges:

```sh
make rpm
```

Binary RPMs appear in `build/rpm/RPMS/<architecture>/`; the source RPM appears
in `build/rpm/SRPMS/`. The build runs the existing geometry unit tests and
checks that the shared backend loads with all symbols resolved, without
accessing the scanner. It builds in an isolated RPM directory and does not
use existing objects, libraries or configured Makefiles from `backends/`.

Use `make srpm` to create just the source RPM, or `make rpm-source` to create
the source archive and copy the spec into `build/rpm/SPECS/`. The snapshot
includes current tracked and unignored source files, including uncommitted
changes, and excludes deleted files and ignored build artifacts. This step
requires a Git checkout. The resulting source RPM builds without Git metadata.

For a new package revision or a different output directory:

```sh
make rpm RPM_VERSION=0.1.0 RPM_RELEASE=2 RPM_TOPDIR=/tmp/plustek-rpmbuild
```

Build for the Fedora release and architecture where you will install it.
The source RPM can also be rebuilt with `mock` for other Fedora targets.

## Install and use

Install the main package (the debuginfo and debugsource packages are optional):

```sh
sudo dnf install ./build/rpm/RPMS/$(rpm --eval '%{_arch}')/sane-backends-plustek8200i-0.1.0-1.*.rpm
```

DNF installs the standard SANE runtime dependencies if needed. The package
contains only the driver library, its configuration, a `dll.d` registration
file, a USB permission rule, documentation and licenses. It does not replace
Fedora's `libsane`, scanner tools, other backends or `/etc/sane.d/dll.conf`.
Configuration changes are preserved on upgrades with `%config(noreplace)`.

After installing, reload udev rules and reconnect the scanner:

```sh
sudo udevadm control --reload-rules
# Unplug and reconnect the scanner's USB cable, then:
scanimage -L
```

The USB rule grants access to the active local desktop user through
systemd-logind. For SSH or a `saned` service, configure permissions for the
intended account separately in `/etc/udev/rules.d/`. No username or custom
group is built into the RPM.

For driver options and debugging, see `man sane-plustek8200i`.
Uninstall with `sudo dnf remove sane-backends-plustek8200i`; the package's
`dll.d` registration is removed along with the backend.
