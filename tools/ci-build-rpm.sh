#!/usr/bin/env bash
# Build the Fedora RPM the way CI does. Run as root inside a Fedora container
# or on a Fedora machine you do not mind installing build dependencies on.
#
# Installs the build dependencies, computes the version (tools/rpm-version.sh),
# builds with `make rpm`, and copies the packages worth publishing (the driver
# RPM and its source RPM, not the debuginfo ones) into ./dist.
set -euo pipefail
cd "$(dirname "$0")/.."

dnf -y install rpm-build redhat-rpm-config dnf-plugins-core git patch tar gzip \
    findutils diffutils which
dnf -y builddep packaging/rpm/sane-backends-plustek8200i.spec

eval "$(tools/rpm-version.sh)"
echo "Building sane-backends-plustek8200i $version-$release"

topdir="$PWD/rpmbuild"
make rpm RPM_VERSION="$version" RPM_RELEASE="$release" RPM_TOPDIR="$topdir"

mkdir -p dist
cp "$topdir"/RPMS/*/sane-backends-plustek8200i-[0-9]*.rpm dist/
cp "$topdir"/SRPMS/sane-backends-plustek8200i-[0-9]*.src.rpm dist/

# Check what was built before calling it done.
for f in dist/*.rpm; do
    echo "== $f"
    rpm -qp --qf '   %{NAME}-%{VERSION}-%{RELEASE}  %{LICENSE}\n' "$f"
done
ls dist/sane-backends-plustek8200i-"$version"-*.x86_64.rpm >/dev/null

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    { echo "version=$version"; echo "release=$release"; } >> "$GITHUB_OUTPUT"
fi
