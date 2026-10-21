# Patches for SANE's genesys GL128 support

Seven patches against upstream SANE `master` (982deaef, October 2026) that bring the
genesys backend's Plustek OpticFilm 8200i SE (GL128, `07b3:1825`) support closer to
this repository's `plustek8200i` backend. Apply with `git am *.patch`.

| # | Patch | What it fixes |
|---|---|---|
| 1 | Cover the whole film gate | Default window clipped the film's right edge and left 0.2-0.4 mm above and below the frame. Now 864 x 640 px at 600 dpi, film centred, 1.4-1.6 mm margins top and bottom. |
| 2 | Choose the front-end gain from the lit gate | SilverFast's fixed gain codes put the white strip at the ADC ceiling on this unit, so the shading table normalised clipped data. Gain is now chosen per channel; G/R colour ratio 0.472 -> 0.406 (this backend: 0.409). |
| 3 | 8-bit output | `--depth 8` for Color and Gray. |
| 4 | Infrared source | `--source "Transparency Adapter Infrared"`, the genesys convention for IR on film scanners. |
| 5 | IntelliScan / QuickScan buttons | Sensor options `extra` and `scan`. **A physical press has not been verified through genesys yet.** |
| 6 | Docs | `genesys.desc` and a newsfragment. |
| 7 | `--infra-red`: the plane as a second frame | The standard option name (`SANE_NAME_INFRARED`). The image frame (`last_frame` false), then a GRAY frame of the same area, resolution and depth (`last_frame` true), from a second `sane_start` with no cancel between. This is the paired-frames convention in *The Fourth Channel* (rev 2); patch 4's source value stays as the standalone scan. Generic in `genesys.cpp`, but tested only on the 8200i SE. |

Tested on one unit with the repository's colour-negative frame: 600, 3600 and 7200 dpi
scans, IR at 600 and 1200 dpi, the two-frame sequence at 600 and 300 dpi (cancel after
frame one, and two images in one session), and upstream's genesys unit tests (updated in
patches 3 and 4). In one two-frame scan at 3600 dpi, scratches and dust in the IR frame lined up with
the image frame to within 2 pixels (0.015 mm) in five regions; separate scans of this
scanner repeat less well (about 0.1 mm).

## Not ported

- **Speed.** At 3600 dpi genesys takes about 53 s against 34.6 s for this backend. It
  samples Y twice per output line and averages, copying SilverFast's captured motor ramp,
  so the image pass takes twice as long. Halving LINCNT alone changed the vertical scale
  instead of the time, so the motor and line-period registers would have to be
  re-derived.
- **Exposure multiplier** (`--exposure`), for the same reason.
- **`--ir-clean` and `--auto-wb`.** Host-side processing that genesys has no place for.
  With patch 4 a frontend can do dust removal from the IR source.
- **Registration guarantee.** Patch 7 delivers the pair but does not promise one-pixel
  registration; the man page says to verify and correct it. Other models' IR methods are
  untested.
