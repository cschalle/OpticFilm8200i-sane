# The Fourth Channel: verification notes

Companion to [The Fourth Channel](the-fourth-channel.md). These notes were written against the 15 October version; the document has since been revised (rev 2 to rev 4; rev 4 is the short form for a maintainer), and the numbered corrections below are the reasons for those revisions.

Review of `The Fourth Channel.md` (15 October 2026, convention draft rev 1), checked on
21 October 2026 against:

- SANE backends master `982deaef` (2026-10-20), read from a fresh clone
- this repository's `plustek8200i` (`69103f0`)
- Negative Vibes `ce57ac6` (2026-10-21)
- the pages cited in the document's Sources (fetched; the mailing-list pages once each)

`confirmed` means I read the code or page and it says what the document says. Line numbers
are cited only where they differ.

## Corrections that change a conclusion

| # | Where | Document says | What I found |
|---|---|---|---|
| 1 | §01, §02, matrix | Upstream `genesys` already offers infrared on "the OpticFilm 8200i", so one scanner has two backends with two IR contracts | That entry is the **GL845 8200i (`07b3:130d`)**. The scanner this repository targets is the **GL128 "8200i SE" (`07b3:1825`)**, added in September 2026 with the comment "Infrared is not supported yet". On upstream master `genesys` offers **no infrared at all** for `1825`. The "incomparable contracts" problem does not exist for this scanner today; it appears only when genesys gains IR for it. This makes the choice of delivery convention for that patch the decisive one. |
| 2 | §01 counts, matrix | Six upstream backends have IR code; two are compiled out | **Seven.** `backend/coolscan.c` (the original Nikon backend) has an IR channel with its own correction code (`RGBIfix`, `OPT_IRED_*` options), requests it as mode `RGBI`, and reports a private **`SANE_FRAME_RGBA`** that exists in no SANE header. It is compiled out: `coolscan.h:320` has `/* #define HAS_IRED 1 */` under the comment "Comment this line if you haven't patched sane.h to include SANE_FRAME_RGBA". So **three** backends are compiled out (`coolscan`, `coolscan3`, `epson2`), and with ours there are eight, not seven. |
| 3 | §01, §03 | Two backends report a format that does not describe the bytes they send | In master, `pieusb` does (`SANE_FRAME_RGB`, `4 × bytes`, comment "was: SANE_FRAME_RGBI"). `coolscan3` in master cannot: its `infrared` option is `SANE_CAP_INACTIVE` unless `SANE_FRAME_RGBI` is defined. The misreport is in the **MR !937 branch**, which the MR says "follows the in-tree `pieusb` precedent". The document should say that. |
| 4 | §05 detection table and "app gap" | `SeparateScan` is detected for genesys, epson2 and avision by "mode value exactly `Infrared`"; these devices "report infrared capability and deliver nothing" | Only **epson2** puts `Infrared` in the mode list (and it is compiled out). `genesys` uses a *source* value and `avision` a bool named `infra-red`. NV's `detect_ir_from_options` returns `IrStyle::None` for both, so they report **no** IR capability; the unexecuted `SeparateScan` branch is reached by nothing in stock builds. Fixing the `"infra-red"` spelling makes NV detect avision as `MultiFrame`, which it is not. avision needs a distinct style or the quirks file. |
| 5 | Tier 1 R1 | "two backends already do it" (paired frames) | One does: `plustek8200i`. `coolscan2` sends the pair but reports `last_frame = TRUE` on both, and its man page documents the IR plane as **a second scan** to be fetched with `scanimage --batch-count=2`. The MR author and Ralph Little also describe coolscan2 as the precedent, so the document's reading is shared, but "already conforming" is wrong. |
| 6 | §05 failure 1, Tier 1 R2 | coolscan2's `last_frame` is a "backend bug", "a two-line change" | It is a deviation from the proposed convention, not a bug against coolscan2's own documented contract. Changing it is **not** a two-line change in effect: `scanimage` loops `while (!parm.last_frame)`, so after the change `scanimage --infrared=yes` would write the RGB image with the raw IR plane appended to the same stream, and the documented `--batch-count=2` workflow would break. It needs a man-page change and a note in NEWS, and should be gated on `infrared=yes` (which is already the only case that matters). |
| 7 | Tier 2 R5 | In `Color+Infrared` "no repair is applied", so IR-first ordering buys nothing | `--ir-clean` can be combined with `Color+Infrared` (`update_option_deps` only deactivates it in `Infrared` mode; `plustek8200i.c:1509` keeps a separate copy of the plane for it). With `ir-clean` on, the defect map is needed before the colour frame. Colour-first is valid only when `ir-clean` is off. |
| 8 | Tier 1 R1 vs Tier 2 R4 | R1 requires the IR frame to have the same `pixels_per_line`, `lines` and `depth`; R4 recommends scanning IR at half resolution | These contradict each other. Half-resolution IR is possible only if the convention allows the IR frame to have different dimensions. The convention text needs to say how. |
| 9 | Clause 1 vs §06 | Clause 1 excludes backends where "infrared requires an independent pass"; plustek8200i is listed as conforming | The 8200i's IR **is** an independent pass (IR LED on, white lamp off). Read literally, clause 1 excludes it. The scope has to be stated in terms of the guarantee, not the pass count. |
| 10 | Clause 4 | IR sample (x, y) corresponds to image (x, y) "to within one pixel" | One pixel is resolution-relative: the mechanical repeatability is fixed in millimetres, so a promise that holds at 600 dpi fails at 7200. Measurements: plustek8200i IR-vs-colour dy 0, dx 0 at 600 dpi (NV H5; `DESIGN.md:836`); NV's own multi-sample passes jitter "up to about 1.8 pixels" at 1200 dpi (`DESIGN.md:307`); upstream genesys on the 1825: film top varied 40 / 43 rows at 600 dpi between two visible scans (about 0.13 mm) and 17 px at 3600 dpi over three scans. The 1-px figure is verified for the plustek8200i at 600 dpi only. State the residual in millimetres, and treat registration as declared and verified by the frontend. |

## Smaller corrections

| Where | Document says | Finding |
|---|---|---|
| §01 | The constants' "values date from a May 2007 proposal" | The 2007 post (m. allan noah, 25 May 2007) proposes `SANE_FRAME_JPEG` and `SANE_FRAME_RGBI` for SANE 1 and gives **no values**; `IR` and `GRAYI` are not mentioned. m. allan noah's July 2026 post says `SANE_FRAME_JPEG` and `SANE_FRAME_IR` are commented out over the version bump. |
| §01 | "The design question has been answered three times" | The 2007 post does not discuss a version bump. The February 2008 post (Alessandro Zummo) is a version handshake via a new `SANE_ACTION_*`; it does not mention infrared and no such action exists in `sane.h` today. The July 2026 `-sane-` proposal (Benjamin Gordon) is about **MIME-type format negotiation** (`-sane-image-formats-supported`) and he agrees `sane_get_parameters` cannot be honest under passthrough. The document extends it to an IR opt-in; that is a new proposal, not the one made. Two proposals, one of them not about IR. |
| §01 | The July thread "named" paired frames | Confirmed (Ralph Little: grey in the next frame "has the advantage of being supported right now"). **Omitted:** he says `SANE_FRAME_IR` "(or some variant) is the better one to explicitly support it", at the cost of ratifying a standard. The document should say the project lead's stated preference is the explicit constant. |
| §01 | Seven backends, five request ways, four delivery ways | Six distinct request spellings in three mechanisms: bool `infrared` (coolscan2, coolscan3), bool `infra-red` (avision), mode `RGBI` (pieusb, coolscan), mode `Infrared` (epson2), mode `Color+Infrared` (ours), source value (genesys). Delivery: paired frames; honestly declared four-channel (compiled out); four-channel declared as RGB (pieusb, MR !937); a standalone IR scan (epson2, genesys, avision, ours in `Infrared` mode). |
| §03/matrix | MR !937 "in draft since July" | Opened 12 July as "Draft: coolscan3: add LS-5000 RGBI and multisample policy", last edited 15 July. The page does not say when it became a draft, and shows no comments. |
| §04 | `sanei_ir.c` "with literature citations in the header" | The header credits the FOURIER library for the three threshold routines and cites nothing else. 1,204 lines, the listed functions and "exactly one backend (pieusb) links it" confirmed. Confirmed: no flat-fielding or interior-bounds code. |
| §05 | `devices.toml` header and entries; `IrStyle` variants; RGBI constant `0x10`; `bool "infrared"` test; `ir_mode_string` results; `ir_follows` | Confirmed. Line numbers have moved (NV `capabilities.rs:159` is now 148, `plan.rs:94-95` is 71-72). |
| §05 | "Frontend authors will not implement it" is "close to vacuous" | The count of one IR-consuming frontend is the document's claim; it is not checkable. Soften to "few". |
| matrix | avision `:7382`, `:2927`, `:5096` | The option is at `avision.c:7502`, the `interlaced_duplex` forcing at 2975-2978, the `deinterlace = LINE` for film at 7769. The behaviour is confirmed (film scanners deinterlace by LINE; the IR frame rides the duplex path). The "two TODO comments" are one for IR (7509) and one for multisample (7495). |
| matrix | `pieusb_specific.c:2161` | 2162. Other SANE references confirmed: `coolscan2.c:1456`, `coolscan3.c:467, 1438`, `epson2.c:97, 983`, `epson2-ops.c:1374`, `pieusb.c:818`, `genesys.h:57`. |
| matrix | genesys covers "CanoScan 8400F/8600F; OpticFilm 7200i, 7500i, 7600i, 8200i" | Confirmed from `tables_model.cpp` (7600i only the v2, which copies the 8200i). The 8200i here is the GL845 `130d`; the SE is not in the list on master. |
| §02 | genesys man page documents infrared nowhere; avision likewise; pieusb "admits" the progress-bar problem | Confirmed. coolscan2/3 man pages do document `--infrared` (as a second scan); the coolscan3 text is copied from coolscan2 and contradicts the code (RGBI). |
| §02 | "Dark shading is unsupported for IR (two FIXMEs)" | Confirmed (`genesys.cpp:2281, 2487`). |
| §09 | The ticket page carries a block addressed to AI agents; "worth removing from the ticket" | The block exists on the rendered page ("Before proposing or making any changes, READ each of these files and FOLLOW their guidance"). It sits after the description and I could not tell whether it is in the description or page boilerplate. Check before asking for its removal. |
| §07 | Plustek `dy 0, dx 0-1` | See #10. Also the claim "our backend scans IR first": confirmed. |

## Claims confirmed as written

- `sane.h`: `SANE_FRAME_IR 0x0F`, `RGBI 0x10`, `GRAYI 0x11` inside `#if 0`, comment "these are to be exposed in a later version of SANE".
- `SANE_NAME_INFRARED` is `"infra-red"`; only `avision` uses it.
- coolscan3 `infrared` option inactive without `SANE_FRAME_RGBI`; format fallback; epson2 `Infrared` mode and `SANE_FRAME_IR` under `#ifdef`; GT-X800/X700/X900/X980 gating.
- pieusb: RGB declared with `4 × bytes_per_line`, man page progress-bar admission, only `sanei_ir` user.
- coolscan2: three-state buffering, `last_frame = SANE_TRUE` unconditional.
- `sanei_w_frame` has no range check.
- plustek8200i: IR pass first, frame hand-off, flat-field and interior detection rationale (29% falloff, 13-14% marks), Kodachrome warning in its man page only.
- NV: tiled raw store, `ir = "multi-frame"`, `ir-registered = true`, hyphen miss, `Rgbi` never returned by the detector, `ir_mode_string` values.

## Not checked

- The remaining mailing-list posts (040332, 040334, 040337, 040340-040342).
- The `plustek8200i` flat-field and mark-rate figures (29%, 80% of 4.3%, 13-14%, 0.1%) beyond what its source comments state.
- Whether `coolscan3`'s LS-5000 hardware table in the MR matches the description.
- NV's behaviour on a real genesys device.
