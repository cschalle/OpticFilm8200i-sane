SANE · Infrared handling · A proposal · rev 4

# The Fourth Channel

Eight SANE backends can read a film scanner's infrared channel: seven upstream, and our out-of-tree `plustek8200i`. They ask for it in six ways and deliver it in four. Three are compiled out of every stock build, and one reports a format it is not sending. A frontend cannot use infrared across scanners without a table of per-device quirks.

We propose a convention that needs no change to `sane.h`, no version bump and no new frame code: **a boolean `infra-red` option; with it set, the image frame (`last_frame` false), then a GRAY infrared frame of the same area (`last_frame` true).** The case for it, in short: the hardware that matters cannot do anything else (§3); our out-of-tree backend already works this way and `coolscan2` is one line from it; we have a second implementation, for `genesys` (§5); and the one frontend we know consumes infrared needs exactly this. The explicit-constant route stays open (§1, §5).

21 October 2026 · SANE backends master `982deaef` · `plustek8200i` `69103f0` · Negative Vibes `ce57ac6` · Ticket #886 · MR !937 · sane-devel July 2026 · The evidence for every claim is in the [review notes](the-fourth-channel-review-notes.md); the genesys implementation, with before and after scans, is on the [patches page](genesys-patches.html).

## 1. The problem

| | |
|---|---|
| **8** | backends with infrared code: seven upstream, one ours |
| **6** | spellings to request it: bool `infrared`, bool `infra-red`, mode `RGBI`, mode `Infrared`, mode `Color+Infrared`, a source value |
| **4** | ways to deliver it (§2) |
| **3** | compiled out of every stock build: `coolscan`, `coolscan3`, `epson2` |
| **1** | shared helper library (`sanei_ir`), linked by `pieusb` only |

*The blocker is narrow.* `SANE_FRAME_IR`, `RGBI` and `GRAYI` are already in `sane.h`, inside an `#if 0` ("these are to be exposed in a later version of SANE"). m. allan noah's July 2026 post says they are commented out because the project "could not agree on bumping the library version number". Earlier proposals (a May 2007 post for `SANE_FRAME_JPEG` and `SANE_FRAME_RGBI`; a February 2008 version handshake) never reached `sane.h`. The July 2026 `-sane-` proposal is about MIME-type negotiation for JPEG and PDF passthrough, not infrared.

*The deadlock is paid for in the wrong place.* Because the constants are unavailable, `coolscan3` marks its `infrared` option inactive, `epson2` drops *Infrared* from its mode list and `coolscan` ships with `HAS_IRED` commented out. Three backends carry code no distribution user can reach. And `saneopts.h` already defines `SANE_NAME_INFRARED` as `"infra-red"`, yet only `avision` uses it.

*The project's stated preference is the explicit constant.* In the July thread Ralph Little says `SANE_FRAME_IR` "(or some variant) is the better one to explicitly support it", at the cost of ratifying a standard and asking frontends to follow; grey in the next frame "has the advantage of being supported right now". We propose the second as the step that can be taken now. It does not close the first (§5).

## 2. What the backends do today

| Backend | How you ask | Delivery and reported format | Stock build | Against the convention (§4) |
|---|---|---|---|---|
| `plustek8200i` (ours) | mode `Color+Infrared`; bool `ir-clean` repairs | RGB (`last_frame` false), then GRAY (true) | out of tree | conforms; needs the `infra-red` bool and man-page lines |
| `coolscan2` | bool `infrared` | RGB, then GRAY on the next `sane_start`; **`last_frame` true on both**; the man page calls it a second scan, `--batch-count=2` | works as two images | one line from conforming; see §3 |
| `coolscan3` | bool `infrared`, inactive | four channels as `SANE_FRAME_RGBI` under `#ifdef`, else RGB | compiled out | would conform if it took the pair, MR !937's alternative |
| `coolscan` | mode `RGBI` | four channels, private `SANE_FRAME_RGBA` defined in no header | compiled out | leave |
| `pieusb` | mode `RGBI`; `clean-image` | four channels declared **RGB**; repair inside `sane_start` | misreports | the largest change: repair mandatory, raw plane unreachable |
| `epson2` | mode `Infrared` (GT-X700, X800, X900, X980) | `SANE_FRAME_IR`, standalone | compiled out | standalone only (clause 10) |
| `genesys` | source `Transparency Adapter Infrared`; **our patch 7 adds `infra-red`** | standalone GRAY; with patch 7, the pair | works | conforms with patch 7 (tested on the 1825 only) |
| `avision` | bool `infra-red` (the standard name) | rides the interlaced-duplex path | works, support undetected | name conforms; delivery unverified |

The four ways to deliver: paired frames (`plustek8200i`, `coolscan2`); four channels in one frame, honestly declared (`coolscan3`, `coolscan`, both compiled out); four channels declared as RGB (`pieusb`, MR !937); a standalone infrared scan (`epson2`, `genesys`, `avision`).

```
paired frames      frame 1 (RGB):  R0 G0 B0  R1 G1 B1  ...      last_frame = FALSE
                   frame 2 (GRAY): I0        I1        ...      last_frame = TRUE
RGBI, declared     R0 G0 B0 I0  R1 G1 B1 I1  ...                format = RGBI
RGBI as RGB        R0 G0 B0 I0  R1 G1 B1 I1  ...                format = RGB
                   read as:  [R0 G0 B0] [I0 R1 G1] [B1 I1 R2] ...   (channels rotate)
standalone         I0 I1 ...                                    format = GRAY or IR
```

A frontend cuts a line into pixels from the `format` it was told, so the third case is wrong for every conforming frontend. MR !937 (opened 12 July as a draft) would make `coolscan3` do it, following `pieusb`; its description says `scanimage` "wrote a P6 RGB header followed by the complete four-channel raster" and that this API question is why it remains a draft. It names the `coolscan2` approach as the alternative.

## 3. Why paired frames

**The hardware decides.** Reading the backends for how the scanners behave gives three classes:

| Class | Examples | Four samples on one line? | Registration |
|---|---|---|---|
| single pass | `coolscan2`, `coolscan3`, `coolscan`, `pieusb` (Nikon LS, PIE, Reflecta) | yes; `coolscan2` captures four colours and splits the infrared out itself | exact by construction |
| two pass | `plustek8200i`; the `genesys` film scanners (`epson2` and `avision` also scan separately; whether their hardware could do otherwise is unknown) | no | measured, below |

On the OpticFilm 8200i the infrared pass needs the infrared LED on, the white lamp off and the front end at its lowest gain, so it is a separate physical pass, and the CCD has no fourth line: infrared is read through the three filtered lines under infrared light. The same is true of the `genesys` film scanners, which also scan the infrared plane separately. Interleaved RGBI is therefore possible on one class; paired frames work on both, since a single-pass backend can split the plane out as `coolscan2` does.

**Registration is measured, not promised.** `plustek8200i`, infrared against colour in one acquisition: dy 0, dx 0 at 600 dpi. `genesys` with patch 7, one two-frame scan at 3600 dpi: scratches and dust in the infrared frame line up with the visible ones to within 2 pixels (0.015 mm) in five regions. Separate scans of that scanner repeat less well (about 0.1 mm), and Negative Vibes' own repeated passes jitter up to 1.8 pixels at 1200 dpi. The residual is fixed in millimetres, so a "within one pixel" promise fails somewhere between 300 and 7200 dpi. That is why the convention below asks backends to declare it.

**The alternatives each cost more.**
- *Interleaved RGBI* needs the constants, a negotiation to expose them, and a second frontend path anyway for the two-pass scanners. It remains right for single-pass hardware (§5).
- *A standalone infrared scan* carries no registration claim, and a frontend that wants the plane of the picture it just scanned has to switch source, scan again and align the two itself. `genesys` offers this; patch 7 builds the pair on top of it.
- *Backend repair only* (`pieusb`) leaves a frontend nothing to work with: no mask, no choice of algorithm, no way to undo it.

**What a frontend sees.** Negative Vibes (a GTK film-scanning application over libsane) keeps the unmodified scan as the archive and re-derives every edit from it, so it needs the raw plane, a way to know it is aligned, and each plane as a contiguous raster for its tiled store. It had to invent the taxonomy SANE lacks (`IrStyle`: multi-frame, RGBI, separate scan, none) and a quirks file that carries what the descriptors cannot, including whether the planes are registered. Four findings from reading it against the backends above:
- `coolscan2`'s plane is unreachable to a frontend that stops at `last_frame`, as Negative Vibes does. This is a deviation from the convention, not a bug against `coolscan2`'s man page, and changing it alters `scanimage` (it loops `while (!last_frame)`, so it would append the raw plane to the RGB output). It needs gating on `infrared=yes` and a man-page change.
- It detects neither `genesys` (a source value) nor `avision` (a bool named `infra-red`; it tests `"infrared"`). Matching the hyphen would file `avision` as paired frames, which it is not.
- Its separate-scan path is declared but never executed; nothing in a stock build selects it.
- Its RGBI path cannot trigger: the detector never returns it, and nothing checks `bytes_per_line` against the declared format, which is the only reliable tell.

## 4. The convention

*Infrared delivery by paired frames.* It adds no frame codes, changes no ABI and bumps no soversion; every clause is implementable against SANE as it stands.

1. **Scope.** A backend that can return an infrared transmission plane for the same scan window as the image, whether the sensor reads it in the same pass or the backend makes a second pass. Registration is declared under clause 4; it is not a condition of entry.
2. **Requesting it.** A boolean option named `SANE_NAME_INFRARED` (`"infra-red"`), `SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT`, default false. Existing spellings may stay as aliases. Where infrared is unavailable for the current settings the option is `SANE_CAP_INACTIVE`, never silently ignored.
3. **Frame sequence.** With the option set, one image is two frames: the image frame (`SANE_FRAME_RGB` or `GRAY`, `last_frame = SANE_FALSE`), then, from a further `sane_start`, the infrared frame (`SANE_FRAME_GRAY`, `last_frame = SANE_TRUE`). The image frame is delivered first, whatever order the backend acquired them in. `sane_get_parameters` describes the frame being delivered and reports `last_frame` truthfully.
4. **Geometry and registration.** The same field of view and bit depth, with the same `pixels_per_line` and `lines`, or smaller by the same integer factor in both (the frontend scales from the two parameter sets). The backend states its registration residual in millimetres in its man page; at or under 0.05 mm (a proposed figure, to be tuned) a frontend may skip alignment, and above it the frontend aligns. A frontend should always be able to verify.
5. **Content.** The measured infrared transmission, same byte order, higher meaning more transmitted. No detection, masking, thresholding, interpolation or repair. Illumination, shading and gain correction are permitted, and the man page says so.
6. **Backend repair.** A separate option, default off; enabling it neither suppresses nor alters the infrared frame.
7. **Early termination.** A frontend may stop after the image frame. `sane_cancel` after the first frame leaves the next `sane_start` to begin a new image; during either frame it ends the whole image.
8. **Discovery.** From the sequence itself, with the option set. No new frame code, capability bit or version negotiation. A frontend that never sets the option receives what it receives today.
9. **Documentation.** The man page gives the option name; the second-frame delivery; any normalisation; the registration residual in millimetres; the pass order; whether the plane may be at lower resolution; and the films it fails on. Kodachrome's dyes absorb infrared like the picture, so its plane must not be used for repair; silver black-and-white film blocks infrared; chromogenic negatives and E-6 slides behave as assumed.
10. **Standalone scans.** A backend may also offer an infrared-only scan (a mode or source value): one GRAY frame, no registration claim. It is not the frontend path; clause 2 is.

```
sane_control_option(h, opt_infra_red, SANE_ACTION_SET_VALUE, &yes, 0);
sane_start(h);                  /* frame 1: the image, RGB, last_frame = SANE_FALSE */
/* read to EOF */
sane_start(h);                  /* frame 2: the plane, GRAY, last_frame = SANE_TRUE */
/* read to EOF */
```

**Not in scope:** four-channel delivery and the `IR`, `RGBI` and `GRAYI` constants (they stay available); JPEG and PDF passthrough, a harder contract that infrared should not wait for, and bundling the two is how the 2007 proposal ended; what a frontend does with the plane.

## 5. What we will do, and what we ask

**Our drivers.**
- `genesys`: [seven patches](genesys-patches.html), of which patch 4 adds the standalone source and **patch 7 the `infra-red` option and the frame pair**, generic in `genesys.cpp` and tested on the OpticFilm 8200i SE. Upstream `genesys` offered no infrared for that scanner (the "8200i" it lists is the GL845 `130d`), so the convention is the choice, not a replacement. The man page has the clause 9 section. To do: measure more scans and the other models before declaring the pair "registered".
- `plustek8200i` (to be retired once `genesys` reaches parity, which this convention makes testable): add the `infra-red` bool, three man-page lines (normalisation, residual, order), and measure registration at 3600 and 7200 dpi. Colour-first ordering is possible only when `ir-clean` is off.
- Negative Vibes: detect the `infra-red` bool, accept an infrared frame scaled by an integer factor, check `bytes_per_line` against the format, run its registration step unless the residual is declared small, and offer `nv-cli probe --ir` as a conformance tester that works without owning the scanner.

**What we ask of the project, in this order.**
1. Discuss this convention in the thread and on !937.
2. Converge request spellings on `"infra-red"`, keeping existing values as aliases.
3. Fix `coolscan2` as described in §3, with its man page and NEWS.
4. Document infrared in each backend's man page (clause 9); expose the raw plane where a backend repairs (`pieusb`).
5. Take illumination flat-fielding and frame-interior detection into `sanei_ir`: the 8200i's infrared LED falls off about 29% toward one edge, and the dark border around a 35 mm frame defeats any global threshold. Both were found on hardware and `sanei_ir` has neither.
6. Later, un-`#if 0` the constants behind a negotiation opt-in, so single-pass hardware can declare RGBI honestly and two compiled-out backends work again, kept separate from passthrough.

## 6. Sources and method

Backend claims were read from a clone of the SANE backends tree at `982deaef`; our backend and Negative Vibes at the commits above. Mailing-list posts and the merge request were fetched from the Debian alioth archive and GitLab. What was and was not verified is in the [review notes](the-fourth-channel-review-notes.md).

[Work item #886](https://gitlab.com/sane-project/backends/-/work_items/886) · [MR !937](https://gitlab.com/sane-project/backends/-/merge_requests/937) · sane-devel July 2026: [040332](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040332.html), [040333](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040333.html), [040335](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040335.html), [040343](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040343.html) · [2007-May/019285](https://alioth-lists.debian.net/pipermail/sane-devel/2007-May/019285.html) · [2008-Feb/021145](https://alioth-lists.debian.net/pipermail/sane-devel/2008-February/021145.html) · [negative-vibes](https://github.com/cschalle/negative-vibes) · [OpticFilm8200i-sane](https://github.com/cschalle/OpticFilm8200i-sane)
