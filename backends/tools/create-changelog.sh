#!/bin/sh -u
# tools/create-changelog.sh -- for inclusion in source tarballs
# Copyright (C) 2019  Olaf Meeuwissen
#
# License: GPL-3.0+

git log --date=iso8601 --no-decorate --topo-order --abbrev=12 \
        $(git describe --tags --abbrev=0)..HEAD \
    | sed 's/[[:space:]]*$//' \
    > ChangeLog

cat << EOF >> ChangeLog

----------------------------------------------------------------------
Older entries are located in the ChangeLogs/ directory, which contains
a separate file for each release.  (Please note: 1.0.26 and 1.1.0 were
skipped as release numbers.)
EOF
