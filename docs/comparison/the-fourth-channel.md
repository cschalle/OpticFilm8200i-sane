SANE · Infrared handling · Review and recommendations · rev 3

# The Fourth Channel

Seven SANE backends can read a film scanner's infrared channel, and our out-of-tree `plustek8200i` makes eight. Between them they use six spellings to ask for it and four ways to deliver it; three of the seven are switched off in every stock build, and one reports a format it is not sending. This report says what each does, where the nineteen-year standards deadlock actually sits, and recommends one written convention that needs no change to the standard: **paired frames, one switch, registration declared rather than promised.** It then says what that means for our two drivers and for Negative Vibes.

21 October 2026 · SANE backends master `982deaef` · `plustek8200i` `69103f0` · Negative Vibes `ce57ac6` · Ticket #886 · MR !937 · sane-devel July 2026

**Changes since the 15 October version.** The survey gains an eighth backend (`coolscan`). Upstream `genesys` turns out to offer no infrared for the scanner we care about (§1). Our convention draft no longer excludes two-pass hardware and no longer promises one-pixel registration (§5). The Negative Vibes detection table is corrected (§3). **Rev 3** records that the genesys patch series now implements the convention for this scanner (`--infra-red`, patch 7) and adds a registration measurement from it (§4, §5, §6). What was checked, and how, is in the [review notes](the-fourth-channel-review-notes.md). The patches and their before/after scans are on the [genesys patches page](genesys-patches.html).

## 1. What the survey found

Infrared is the feature that separates a usable negative workflow from a day of manual spotting. SANE's problem is not that it is unsupported; it is that no two backends agree on how to ask for it or how to hand it back.

**Where this is written from.** `plustek8200i` is our own backend for the OpticFilm 8200i (`07b3:1825`, GL128). It is maintained out of tree and has never been part of a SANE release. It is a data point here, and the one whose source we can change freely. The plan is to retire it once `genesys` reaches parity for this scanner.

**What genesys offers for this scanner today: nothing.** `genesys` gained the 1825 in September 2026 as the "OpticFilm 8200i SE", with the comment "Infrared is not supported yet". The "OpticFilm 8200i" that `genesys` lists with infrared is a different machine, the GL845 `07b3:130d`. So infrared parity for the 1825 is not a comparison of two incompatible contracts; it is a missing feature, and the convention chosen for the patch that adds it is the decision that matters. That is the practical reason for this report. Our [patch series](genesys-patches.html) adds both the standalone source and the paired `--infra-red` option (§6).

| | |
|---|---|
| **8** | backends with infrared code: seven upstream, one ours |
| **6** | spellings to request it: bool `infrared`, bool `infra-red`, mode `RGBI`, mode `Infrared`, mode `Color+Infrared`, a source value |
| **4** | ways to deliver it (§2) |
| **3** | whose infrared path is compiled out of every stock build: `coolscan`, `coolscan3`, `epson2` |
| **1** | shared helper library (`sanei_ir`), linked by exactly one backend |
| **19 yrs** | since a four-channel RGBI frame constant was first proposed (May 2007) |

Three findings carry the weight.

*The blocker is narrow.* `SANE_FRAME_IR`, `SANE_FRAME_RGBI` and `SANE_FRAME_GRAYI` are already in `sane.h`, inside an `#if 0` under the comment "these are to be exposed in a later version of SANE". m. allan noah's July 2026 post says they are commented out because the project "could not agree on bumping the library version number". Two proposals touched this: a May 2007 post offering `SANE_FRAME_JPEG` and `SANE_FRAME_RGBI` for SANE 1, and a February 2008 version handshake. Neither reached `sane.h`, and neither was about infrared alone. The July 2026 `-sane-` proposal is a third, but it is about MIME-type negotiation for JPEG and PDF passthrough; extending it to an infrared opt-in would be a new proposal.

*The deadlock is paid for in the wrong place.* Because the constants are unavailable, `coolscan3` marks its own `infrared` option `SANE_CAP_INACTIVE`, `epson2` drops *Infrared* from its mode list, and `coolscan` ships with `HAS_IRED` commented out. Three backends carry infrared code that no distribution user can reach.

*The option vocabulary is fragmented where it need not be.* `saneopts.h` defines `SANE_NAME_INFRARED` as `"infra-red"`. One backend, `avision`, uses it. Fixing that needs no one's ratification.

The project's stated preference should be on the record: in the July thread Ralph Little says `SANE_FRAME_IR` "(or some variant) is the better one to explicitly support it", at the cost of ratifying a new standard and asking frontends to follow; grey in the next frame "has the advantage of being supported right now". The recommendation below is the second of those, as the step that can be taken now, and it leaves the first open (§6, tier 3).

## 2. Eight backends, six spellings

| Backend | How you ask | How it comes back | Reported format | Stock build | Repair |
|---|---|---|---|---|---|
| `plustek8200i` (ours) | mode `Color+Infrared` (paired) or `Infrared` (standalone); bool `ir-clean` repairs | RGB frame, then a GRAY frame; IR pass runs first | RGB, `last_frame` FALSE; then GRAY, TRUE | out of tree | opt-in; raw plane always delivered |
| `coolscan2` | bool `infrared` | RGB, then GRAY on the next `sane_start`; IR buffered during the colour scan | RGB then GRAY, **`last_frame` TRUE on both**; man page documents it as a second scan, `--batch-count=2` | works as two images | frontend |
| `coolscan3` | bool `infrared`, **inactive** | would send four interleaved channels | `SANE_FRAME_RGBI` under `#ifdef`; else RGB | compiled out | frontend |
| `coolscan` (original) | mode `RGBI` plus `IRED cor.` options | four channels, IR corrected in the backend | private **`SANE_FRAME_RGBA`**, defined in no SANE header | compiled out (`HAS_IRED`) | backend |
| `pieusb` | mode `RGBI`; `clean-image`, `correct-infrared` | one four-channel frame | **RGB**, `bytes_per_line` of four channels | misreports | backend, inside `sane_start` |
| `epson2` | mode `Infrared` (GT-X700, X800, X900, X980 only) | standalone IR scan | `SANE_FRAME_IR` under `#ifdef`, no fallback | compiled out | frontend |
| `genesys` | **source** value `Transparency Adapter Infrared` | standalone IR scan | GRAY | works (CanoScan 8400F, 8600F; OpticFilm 7200i, 7500i, 7600i v2, 8200i `130d`) | frontend |
| `avision` | bool `infra-red` (the standard name) | IR rides the interlaced-duplex path, deinterlaced by LINE on film scanners | per duplex handling | works, undetected capability | frontend |

Notes on the rows:

- `genesys` treats infrared as a scan source beside "Transparency Adapter", so a frontend that enumerates sources to find film also finds infrared there. Dark shading is explicitly unsupported for infrared scans (two `FIXME`s), and its man page does not mention infrared.
- `avision` reuses the code built for scanning both sides of a sheet; its man page does not mention infrared, and a `TODO` says there is no way to detect support, so any device with exposure control is assumed to have it.
- `pieusb` does its dust removal inside `sane_start`; its man page admits this makes a frontend appear blocked.
- `coolscan3`'s man page is copied from `coolscan2` and describes a second scan, which the code does not do.
- MR !937 (opened 12 July as a draft) would make `coolscan3` follow `pieusb`: RGB declared, four channels sent. Its description says `scanimage` "wrote a P6 RGB header followed by the complete four-channel raster" and that this API question is why it remains a draft. It names the `coolscan2` approach as the alternative.

**The four ways to deliver:** paired frames (`plustek8200i`, `coolscan2`); four channels in one frame, honestly declared (`coolscan3`, `coolscan`, both compiled out); four channels in one frame declared as RGB (`pieusb`, MR !937); a standalone IR scan (`epson2`, `genesys`, `avision`, our `Infrared` mode).

### The wire formats, two pixels

```
paired frames      frame 1 (RGB):  R0 G0 B0  R1 G1 B1  ...      last_frame = FALSE
                   frame 2 (GRAY): I0        I1        ...      last_frame = TRUE
RGBI, declared     R0 G0 B0 I0  R1 G1 B1 I1  ...                format = RGBI
RGBI as RGB        R0 G0 B0 I0  R1 G1 B1 I1  ...                format = RGB
                   read as:  [R0 G0 B0] [I0 R1 G1] [B1 I1 R2] ...   (channels rotate)
standalone         I0 I1 ...                                    format = GRAY or IR
```

A frontend cuts a line into pixels from the `format` it was told. The third case is the one blocking !937: nothing is wrong with `scanimage`; the backend declared RGB and sent RGBI, and every conforming frontend mis-slices it. In master, only `pieusb` does this.

### The library nobody uses

`sanei/sanei_ir.c` (1,204 lines) has spectral crosstalk cleaning, adaptive median and MAD defect detection, Otsu, Yen and maximum-entropy thresholds (adapted from the FOURIER library), mask dilation and inpainting, and crop detection. Only `pieusb` links it. `plustek8200i` implements the same algorithm family on its own: interior-bounds detection, median and MAD, a threshold at `median − k·MAD`, and run-length guards. It also contains two things `sanei_ir` lacks, both found on hardware and recorded in its source:

- **Illumination flat-fielding** before thresholding. The 8200i's infrared LED falls off about 29% toward one edge; with a single global threshold 80% of the 4.3% of pixels marked were in the leftmost 5% of the frame.
- **Frame-interior detection.** The dark border around a 35 mm frame defeats any global threshold: 13-14% of pixels were marked against a true defect rate near 0.1%.

Contributing both to `sanei_ir` needs no standards process and benefits every backend that can use a plane, whether or not `plustek8200i` is ever merged.

## 3. The view from Negative Vibes

Negative Vibes (a GTK film-scanning application in Rust over libsane) had to invent the taxonomy SANE lacks. In `nv-device/src/capabilities.rs`:

| `IrStyle` | Meaning | Detected by | Produced by, among the backends above |
|---|---|---|---|
| `MultiFrame` | RGB frame (`last_frame` false), then a GRAY frame | a mode value containing "Color" and "Infrared"; or a bool named exactly `infrared` | `plustek8200i`; `coolscan2` (and `coolscan3`'s inactive option, unless NV filters inactive options; not checked) |
| `Rgbi` | one four-channel frame, unofficial `0x10` | frame format `SANE_FRAME_RGBI`, or a quirks entry | nothing in a stock build |
| `SeparateScan` | a second, independent scan in `Infrared` mode | a mode value exactly `Infrared` | `epson2` only, and it is compiled out |
| `None` | no infrared | fallthrough | everything else, **including `genesys` and `avision`** |

`data/devices.toml` carries what the descriptors cannot: `ir = "multi-frame"` and `ir-registered = true` for the 8200i. Every field in it is something SANE could have told the frontend. The second matters because it decides whether NV must run its registration step before using the mask.

Four interoperability findings, corrected from the earlier version:

1. **`coolscan2`'s IR plane is unreachable to a frontend that stops at `last_frame`.** NV does exactly that, so the buffered plane is never fetched. This is a deviation from the proposed convention, not a bug against `coolscan2`'s documented contract (a second scan, `--batch-count=2`). Fixing it changes `scanimage`: it loops `while (!last_frame)`, so after the change `scanimage --infrared=yes` appends the raw IR plane to the RGB output, and the documented workflow breaks. Gate it on `infrared=yes`, update the man page and NEWS.
2. **`genesys` and `avision` are not detected at all.** `genesys` uses a source value NV does not look for; `avision` a bool named `infra-red`, which the exact-name test (`"infrared"`) misses. Matching the hyphen detects `avision`, but as `MultiFrame`, which it is not: its delivery goes through the duplex path. It needs its own style or a quirks entry.
3. **The `SeparateScan` path is declared but not executed.** `ir_follows` includes it, `ir_mode_string` returns `"Color"` for it, and the frame loop ends at `last_frame`, so no second pass starts. In stock builds nothing selects it.
4. **The `Rgbi` path cannot trigger.** The detector never returns it, no backend in a stock build reports the format, and `ir_mode_string` would ask `pieusb` for `"Color+Infrared"` where it wants `"RGBI"`. The reliable tell is arithmetic: `bytes_per_line` of four channels where the format says three. Nothing checks it.

NV's design already has the rest of the pipeline: a degree-4 illumination fit plus red-density ghost for crosstalk, MAD thresholding, grown and inpainted marks, off by default for silver black-and-white film, always on for Kodachrome. That is `sanei_ir`'s list, reimplemented in a second language, because a frontend can neither reach the library nor trust what a backend did to the data. NV also already registers separate passes (`DESIGN.md` §5.6: edge correlation to a fraction of a pixel, offsets under a quarter pixel ignored, search range 20 px).

### What each convention gives a frontend

| | Gains | Still has to do | What the user gets |
|---|---|---|---|
| **paired frames** | two self-describing frames the frame loop already handles; each plane a contiguous raster, the shape of the tiled store | one progress bar across two frames; trust or verify registration | depends on pass order, which nothing specifies. IR-first backends show no picture until the second pass; colour-first backends show it early, and the user can stop and keep it |
| **RGBI, honestly declared** | registration exact by construction; one frame, one bar; the colour image builds from the first line | split each line into two stores | best masks, but possible only on single-pass hardware |
| **separate scan** | the passes may differ: IR is a mask, not a picture, so a half-resolution scan costs a quarter and upsamples cleanly; the decision can be deferred until the colour frame shows it is dirty | register by correlation over two 16-bit images, with no advance sign it will work | potentially the fastest workflow; today the slowest |
| **backend repair only** | nothing NV can use | treat the device as having no infrared | dust removed invisibly, without control or undo; a loss for a non-destructive tool |

On architecture alone, interleaving wins; but §4 shows the hardware this report is about cannot do it. Note too that the half-resolution advantage belongs to the third row; the convention in §5 allows it explicitly, so paired frames keep it.

## 4. What the hardware can produce

Whether a convention is possible is decided before cost or politics. Reading the backends for how the hardware behaves gives three classes:

| Class | Examples | Four samples on one line? | Registration |
|---|---|---|---|
| single pass | `coolscan2`, `coolscan3`, `coolscan`, `pieusb` (Nikon LS series, PIE, Reflecta, CrystalScan) | yes; `coolscan2` captures four colours interleaved and splits the IR out itself | exact by construction |
| two pass, shared motion | `plustek8200i`, 1825 | no | measured, see below |
| two pass, no established registration | `genesys`, `epson2`, `avision` | no | unknown, see below |

On the 8200i the infrared pass needs the infrared LED on, the white lamp off and the front end at its lowest gain, so it is a separate physical pass; the CCD has no fourth line, and infrared is read through the three filtered lines under infrared light. You cannot interleave samples that were never captured together. The same holds for every `genesys` device, which is most of the infrared-capable film hardware in Linux hands.

**Registration, measured.** Plustek8200i, IR against colour in one acquisition: dy 0, dx 0 at 600 dpi (NV checks H1, H2 and H5: dy 0.00 to 0.06 rows). Only 600 dpi has been measured. Patched `genesys` on the 1825, one two-frame scan at 3600 dpi: scratches and dust in the infrared frame line up with the visible ones to within 2 pixels (0.015 mm) in five regions (dy −0.3 to +0.6, dx −0.9 to +1.8 pixels, by cross-correlation of the visible defects against the infrared defect map). That is a single scan. NV's own repeated passes jitter up to about 1.8 pixels at 1200 dpi (`DESIGN.md` §5.3). Upstream `genesys` on the 1825, separate scans (each re-homes): two visible scans of the same frame at 600 dpi: film top at row 40 and row 43 (about 0.13 mm), and 17 pixels' spread over three scans at 3600 dpi. The residual is fixed in millimetres, so any "within one pixel" promise fails somewhere in the 300 to 7200 dpi range.

Paired frames can be produced by two of the three classes (and by the third, if the backend de-interleaves, as `coolscan2` already does); interleaved RGBI by one. That, and not the cost, is why RGBI cannot be the general answer. It is still the right answer for single-pass hardware, and tier 3 below keeps it available.

## 5. The convention, rev 2

*Infrared delivery by paired frames.* Draft text for the thread, for MR !937, or for the frame section of the standard. It adds no frame codes, changes no ABI and bumps no soversion; every clause is implementable against SANE as it stands.

1. **Scope.** The convention applies to a backend that can return an infrared transmission plane for the same scan window as the image it returns, whether the sensor reads it in the same pass or the backend makes a second pass. A backend that cannot supply a plane for the same window is outside it. (Registration is declared under clause 4, not a condition of entry.)
2. **Requesting it.** The backend exposes a boolean option named `SANE_NAME_INFRARED` (`"infra-red"`) with `SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT`, default `SANE_FALSE`. Existing spellings (a mode value, a source value, a bool named `"infrared"`) may stay as aliases. Where infrared is unavailable for the current settings the option is `SANE_CAP_INACTIVE`, never silently ignored.
3. **Frame sequence.** With the option set, one image is exactly two frames: the image frame (`SANE_FRAME_RGB` or `SANE_FRAME_GRAY`, `last_frame = SANE_FALSE`), then, from a further `sane_start`, the infrared frame (`SANE_FRAME_GRAY`, `last_frame = SANE_TRUE`). The image frame is delivered first, whatever order the backend acquired them in. `sane_get_parameters` describes the frame being delivered and reports `last_frame` truthfully for both.
4. **Geometry and registration.** The infrared frame covers the same field of view as the image frame, at the same bit depth, with the same `pixels_per_line` and `lines`, or smaller by the same integer factor in both. The frontend scales from the two sets of parameters. The backend states its registration residual in millimetres in its man page. A residual at or under 0.05 mm (a proposed figure, to be tuned) is "registered": a frontend may skip alignment. Anything larger is "approximately registered": the frontend aligns. Frontends should always be able to verify, and must not assume better than declared.
5. **Content.** The plane is the measured infrared transmission of the medium, same byte order, higher values meaning more infrared transmitted. No detection, masking, thresholding, interpolation or repair is applied. Illumination, shading and gain correction are permitted and expected, and the man page says so.
6. **Backend-side repair.** If the backend can repair from the plane, that is a separate option defaulting to off, and enabling it neither suppresses nor alters the infrared frame. A frontend can always get both the unrepaired image and the unrepaired plane from one acquisition.
7. **Early termination.** A frontend may stop after the image frame. `sane_cancel` after the first frame returns the backend to a state where the next `sane_start` begins a new image. `sane_cancel` during either frame ends the whole image.
8. **Discovery.** A frontend recognises the convention from the sequence: an image frame with `last_frame = SANE_FALSE`, then a GRAY frame of matching geometry (clause 4), while the option is set. No new frame code, capability bit or version negotiation is needed. A frontend that never sets the option receives what it receives today.
9. **Documentation.** The man page states the option name; that the plane arrives as a second frame; any normalisation applied; the registration residual in millimetres; the pass order; whether the plane may be acquired at lower resolution; and which film it fails on. Kodachrome's dyes absorb infrared like the picture, so its plane must not be used for repair; silver black-and-white film blocks infrared; chromogenic negatives and E-6 slides behave as assumed.
10. **Standalone scans.** A backend may also offer an infrared-only scan (a mode or source value), delivered as one GRAY frame with no registration claim. It is not the frontend path; clause 2 is.

```
/* Clause 3, as a frontend sees it. */
sane_control_option(h, opt_infra_red, SANE_ACTION_SET_VALUE, &yes, 0);

sane_start(h);                    /* frame 1: the image */
sane_get_parameters(h, &p);       /* RGB, last_frame = SANE_FALSE */
while (sane_read(h, buf, n, &len) == SANE_STATUS_GOOD) { /* ... */ }

sane_start(h);                    /* frame 2: the infrared plane */
sane_get_parameters(h, &p);       /* GRAY, last_frame = SANE_TRUE, geometry per clause 4 */
while (sane_read(h, buf, n, &len) == SANE_STATUS_GOOD) { /* ... */ }
```

**Not in scope.** Four-channel delivery and the `SANE_FRAME_IR`, `RGBI` and `GRAYI` constants (they stay available; the `#ifdef` guards keep working either way). JPEG and PDF passthrough and MIME negotiation, a harder contract since image parameters may not be knowable without decoding; infrared should not wait for it, and bundling the two is how the 2007 proposal ended. What a frontend does with the plane.

**Differences from rev 1.** Clause 1 no longer excludes two-pass hardware (it excluded the 8200i, which the document listed as conforming). Registration is declared in millimetres instead of promised to one pixel. The infrared frame may be smaller by an integer factor, which lets a frontend scan it at lower resolution. The image-first delivery order is explicit. Clause 10 is new.

### Where the backends stand

| Backend | Against rev 2 | What it needs |
|---|---|---|
| `plustek8200i` | clauses 1, 3, 5, 6, 7, 8 hold; mode value is the existing spelling | the `infra-red` bool as clause 2's switch; man-page lines for 4, 5 and 9 (normalisation, residual, order); registration measured at 3600 and 7200 dpi before a residual is declared |
| `genesys`, GL128 1825 (our patches 4 and 7) | clauses 2, 3, 5, 7, 9 implemented: the `infra-red` bool, the image frame then a GRAY frame, the man page section; the source value is the standalone scan (clause 10); the frame geometry is equal by construction | the residual rests on one scan (0.015 mm within the pair); measure more scans and other models before declaring it; the generic code is untested on other genesys models |
| `coolscan2` | the pair exists; clause 3 fails on `last_frame` | gate `last_frame = FALSE` on `infrared=yes`; man page and NEWS; note the `scanimage` consequence |
| `coolscan3` | compiled out; the paired alternative in MR !937 would conform | adopt the pair instead of `pieusb`'s misreport |
| `coolscan` | compiled out, needs a patched `sane.h` | nothing; note it |
| `pieusb` | fails clause 3 (interleaved, declared RGB) and 6 (repair mandatory, raw plane unreachable) | the largest change; it could de-interleave as `coolscan2` does |
| `epson2` | standalone only | clause 2 on its own; compiled out today |
| `avision` | clause 2's name already; delivery through the duplex path, not verified against clause 3 | verification; clause 9 documentation from scratch |

## 6. What we recommend

The convention in §5 works for our drivers and for Negative Vibes because it asks each of them for something it can already do, and it makes one change in each.

**For the 1825 and the retirement plan: parity needs a definition, and this is it.** `genesys` offered nothing for this scanner, so the patch that adds infrared chose the convention. The series now has both: patch 4 adds the source value `Transparency Adapter Infrared` (a standalone GRAY scan, following genesys's convention for its other film scanners) and patch 7 adds `--infra-red`, which delivers the image frame (`last_frame` false) and then a GRAY frame of the same area from a second `sane_start`, with no cancel between. Patch 7 is generic in `genesys.cpp`: the second frame is the model's existing infrared scan method run with the same settings, so the CanoScan 8400F and 8600F and the other OpticFilm models get the same frontend path, though only the 8200i SE has been tested. What remains:

1. **Measure registration on more scans** and, if it holds under 0.05 mm, declare the pair "registered" in the man page; the current man page gives the one-scan figure and tells frontends to verify.
2. **Test the other genesys models**, or say in the man page that they are untested (it does).
3. **Post the convention and the patch to the thread**, with patch 7 as the working example.

**Parity then has a test**, run by one tool against both backends on the same frame: the `infra-red` bool exists and is detected; the frame sequence is the same; the frame geometry agrees; the IR plane's level and the dust marks it shows agree; the registration residual in millimetres is reported. NV's planned `nv-cli probe --ir` is that tool (tier 2); the two-frame scan on the [patches page](genesys-patches.html) is a manual version of it. Thresholds for "agree" are to be set by the first measurement, not before it.

**For `plustek8200i`.** Add the `infra-red` bool so the canonical switch exists beside `Color+Infrared`; add the three man-page lines; measure IR-to-colour registration at 3600 and 7200 dpi. Scan colour first in `Color+Infrared` **only when `ir-clean` is off**: with it on, the defect map must come first, so the order is a function of that option, not a change to make unconditionally.

**For Negative Vibes.**
- Detect the `infra-red` bool as the switch, keeping the existing mode and `infrared` detections as aliases; keep `avision` out of `MultiFrame`.
- Handle the pair by comparing the two sets of parameters: accept an infrared frame smaller by an integer factor and scale it; add the `bytes_per_line`-versus-format check so a misreported four-channel frame is caught.
- Treat registration as declared and verified: keep `ir-registered` as a hint to skip the correlation, and run the existing registration on the proxy otherwise.
- Execute the `SeparateScan` path (second scan from a source value, at reduced resolution) only as the fallback for devices that offer a standalone scan and nothing else; with the convention adopted, `genesys` stops needing it.
- Offer `nv-cli probe --ir` as the conformance tester, which also demonstrates parity.

**Upstream, in this order**, so nothing in the first two tiers waits on a standards decision.

*Tier 1.* Post this convention in the thread and on !937. Converge request spellings on `"infra-red"`, keeping existing values as aliases. Fix `coolscan2` as described in §3, with the man page and NEWS change. Always expose the raw plane where a backend can repair (`pieusb`). 
*Tier 2.* Contribute flat-fielding and interior detection to `sanei_ir`. Write a capability section in each infrared backend's man page (option name, delivery, registration residual, film types; the Kodachrome warning exists only in ours). Offer `nv-cli probe --ir`.
*Tier 3.* Un-`#if 0` the frame constants behind a negotiation opt-in, so single-pass hardware can declare RGBI honestly and two compiled-out backends work again. Keep it separate from JPEG and PDF passthrough. Add a read-only way to report registration if the man-page residual proves not enough.

## 7. Sources and method

Backend claims were read from a clone of the SANE backends tree at `982deaef`; line references into it use that commit. `plustek8200i` and Negative Vibes references use the commits named at the top. Mailing-list messages and the merge request were fetched from the Debian alioth archive and GitLab. What was and was not verified is in the [review notes](the-fourth-channel-review-notes.md).

1. [Work item #886](https://gitlab.com/sane-project/backends/-/work_items/886): alternative driver for the OpticFilm 8200i.
2. [MR !937](https://gitlab.com/sane-project/backends/-/merge_requests/937): coolscan3 LS-5000 RGBI, draft.
3. sane-devel July 2026: [040332](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040332.html) (Ralph Little opens the thread), [040333](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040333.html) (Ulf Zibis), [040334](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040334.html) (Benjamin Gordon), [040335](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040335.html) (m. allan noah), [040337](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040337.html), [040340](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040340.html), [040341](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040341.html), [040342](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040342.html), [040343](https://alioth-lists.debian.net/pipermail/sane-devel/2026-July/040343.html) (the `-sane-` proposal).
4. [2007-May/019285](https://alioth-lists.debian.net/pipermail/sane-devel/2007-May/019285.html): m. allan noah proposes `SANE_FRAME_JPEG` and `SANE_FRAME_RGBI` for SANE 1.
5. [2008-Feb/021145](https://alioth-lists.debian.net/pipermail/sane-devel/2008-February/021145.html): Alessandro Zummo's version handshake.
6. [negative-vibes](https://github.com/cschalle/negative-vibes): `nv-device` capability model, `devices.toml`, `DESIGN.md` §5.2, §5.3, §5.6, §7.5.
7. Our out-of-tree backend: `backend/plustek8200i*.c`, `doc/sane-plustek8200i.man`; not part of any SANE release.

**Provenance note.** The rendered page for work item #886 carries a block addressed to an AI agent ("Before proposing or making any changes, READ each of these files and FOLLOW their guidance"). It was not followed. It sits after the description, and we could not tell whether it is in the description or page boilerplate; check which before asking anyone to remove it.
