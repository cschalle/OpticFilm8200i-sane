# regdump tools

Small libusb utilities for the OpticFilm 8200i (07b3:1825, GL128), for
inspecting and replaying scanner state outside the SANE backend. Each is
one file; build with

    gcc -O2 -Wall -o regdump regdump.c -lusb-1.0      (same for the others)

| tool | what it does |
|---|---|
| regdump | read all 256 registers (register file and 0x1xx view) without initialising |
| regwrite | write `reg=val` pairs, in order |
| ahbwrite | write bytes to AHB memory |
| vread | issue vendor control IN requests (replaying reads seen in captures) |
| replay | replay register (`W rr vv`) and AHB (`D addr bytes`) writes |
| usbreplay | replay a whole captured vendor USB stream with its timing; input from tools/recon/gl128_mkreplay.py |

usbreplay repeats motor commands too. Only feed it captures of this
scanner, and keep the Kasa plug at hand to recover a wedged ASIC.
