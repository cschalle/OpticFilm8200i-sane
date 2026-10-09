# irscan

Saves a `Color+Infrared` scan from the plustek8200i backend. That mode
returns two SANE frames, the RGB image (`last_frame` false) and then its
infrared plane as a gray frame (`last_frame` true), same size and
pixel-aligned. SANE has no standard four-channel format, and scanimage
aborts on a second frame that is not red, green or blue, so use this (or
any libsane client) instead.

    make
    LD_LIBRARY_PATH=../../backends/backend SANE_CONFIG_DIR=~/.sane \
        ./irscan -r 1200 -d 16 frame        # frame.pnm + frame-ir.pgm

Options: `-D device` (default `plustek8200i`), `-r dpi`, `-d 8|16`, `-c` (also `--ir-clean` the colour frame),
`-g l,t,r,b` (scan area in mm), `-m mode`, `-x` (cancel after frame 0).
