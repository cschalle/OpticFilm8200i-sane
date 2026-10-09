#!/usr/bin/env python3
# Copyright (C) 2026 Christian Schaller
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, see
# <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the standalone backend RPM from the current working tree."""

import argparse
from pathlib import Path
import re
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("rpm", "srpm", "rpm-source"))
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--release", default="1")
    parser.add_argument("--topdir", type=Path)
    args = parser.parse_args()
    for value in (args.version, args.release):
        if not re.fullmatch(r"[0-9][A-Za-z0-9._+]*", value):
            parser.error("version and release must start with a digit and contain "
                         "only letters, digits, '.', '_' or '+'")

    project = Path(__file__).resolve().parent.parent
    topdir = (args.topdir or project / "build/rpm").resolve()
    for directory in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (topdir / directory).mkdir(parents=True, exist_ok=True)

    spec_relative = Path("packaging/rpm/sane-backends-plustek8200i.spec")
    spec = topdir / "SPECS" / spec_relative.name
    spec_text = (project / spec_relative).read_text()
    # Persist overrides in the packaged spec so an SRPM rebuild uses the
    # same version and release without needing the original command line.
    spec_text = spec_text.replace("%global driver_version 0.1.0}",
                                  f"%global driver_version {args.version}}}")
    spec_text = spec_text.replace("%global driver_release 1}",
                                  f"%global driver_release {args.release}}}")
    spec.write_text(spec_text)

    # Git's ignore rules omit configured Makefiles, libraries, objects and
    # caches. Read files from disk so uncommitted driver changes are included.
    result = subprocess.run(
        ["git", "-C", str(project), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", "backends"],
        check=True, stdout=subprocess.PIPE,
    )
    sources = {Path(name.decode()) for name in result.stdout.split(b"\0") if name}
    sources.update(path.relative_to(project)
                   for path in (project / "packaging/rpm").iterdir()
                   if path.is_file())
    sources.update((Path("Makefile"), Path("README.rpm.md"), Path("LICENSE"),
                    Path("tools/build-rpm.py")))
    name = f"sane-backends-plustek8200i-{args.version}"
    archive = topdir / "SOURCES" / f"{name}.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        for relative in sorted(sources):
            source = spec if relative == spec_relative else project / relative
            # Deleted working-tree files must stay deleted in the snapshot.
            if source.is_file() or source.is_symlink():
                output.add(source, arcname=f"{name}/{relative}", recursive=False)
    print(f"Source archive: {archive}", flush=True)
    if args.target != "rpm-source":
        subprocess.run(
            ["rpmbuild", "-ba" if args.target == "rpm" else "-bs",
             "--define", f"_topdir {topdir}",
             str(spec)],
            check=True,
        )
        print(f"Source RPMs: {topdir / 'SRPMS'}", flush=True)
        if args.target == "rpm":
            print(f"Binary RPMs: {topdir / 'RPMS'}", flush=True)


if __name__ == "__main__":
    main()
