Configuring `saned` as a service
================================

To launch `saned` automatically in response to an incoming connection,
configure it as a service managed by `inetd`, `xinetd`, or `systemd`.

First verify that /etc/services contains a line of the following form:

    sane-port 6566/tcp # SANE network scanner daemon

If not, then add it. (The name "sane-port" has been assigned by IANA.)


The examples below assume there is a "saned" group and a "saned" user.
Make sure that the ACLs on the scanner device files are set such that
`saned` can access them for reading and writing.


`inetd` Configuration
---------------------

Configure `inetd` if your platform does not use `systemd` or `xinetd`.
Not all `inetd` implementations support IPv6; check its documentation.

Add a line in /etc/inetd.conf like the following:

    sane-port stream tcp nowait saned.saned /usr/local/sbin/saned saned


`xinetd` Configuration
----------------------

Copy frontend/saned.xinetd.conf into the `xinetd` configuration files
directory (as /etc/xinetd.d/saned.conf).


`systemd` Configuration
-----------------------

Copy frontend/saned.socket and frontend/saned@.service into one of the
system-wide directories for unit files (/etc/systemd/system/).

The recommended way to adjust the service settings is with the command
`systemctl edit saned@`. This opens a file inside a text editor where
overrides may be entered. As an example, environment variables can be
set which are used to control debug logging in individual backends:

    [Service]
    Environment=SANE_DEBUG_DLL=255
    Environment=SANE_DEBUG_BJNP=5

(Refer to the man pages of the appropriate backends for more details.)

It is recommended to build SANE with explicit `systemd` support, which
allows log messages from `saned` to be written to the journal. If SANE
is built without this support, the following override must be used:

    [Service]
    StandardInput=socket
