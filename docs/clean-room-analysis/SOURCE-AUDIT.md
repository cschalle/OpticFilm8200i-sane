# Analyst source audit — keep outside the implementer handoff

Prepared 2026-10-07 from working-tree source in this repository. The user
states that this driver has been verified to work and be correct. No device
access or new hardware measurements were performed for this documentation.

Repository HEAD: `dad46b6765f17db58360a12831f7b2f41c597079`.
The examined driver source files and geometry test were unmodified relative
to HEAD when inspected. The workspace contains many existing deletions of
historical material; those were left alone. No archived source, vendor
decompilation, old protocol documents, or external website was used to
derive the handoff.

The deliverable directory `../clean-room/` is intended to be copied alone
to the independent implementer's environment. This audit deliberately
contains implementation filenames, symbols, and evidence locations for
review by someone allowed to inspect the source.

## Evidence map

Line numbers identify the examined snapshot, not an API dependency.

| Packet topic | Source evidence |
| --- | --- |
| Device selection and endpoints | `backends/backend/plustek8200i.h:771`; `backends/backend/plustek8200i.c:531` |
| Register USB format and confirmation | `backends/backend/plustek8200i-usb.c:60`, `:127`; `backends/backend/plustek8200i-scan.c:74` |
| DMA requests, bulk padding, readiness | `backends/backend/plustek8200i-dma.c:101`, `:167`, `:223`, `:366` |
| Image FIFO header and chunk sizing | `backends/backend/plustek8200i-dma.c:433`, `:493` |
| Defaults and option dependencies | `backends/backend/plustek8200i.c:156`, `:351`, `:656` |
| Pre-start geometry and crop | `backends/backend/plustek8200i.h:809`; `backends/backend/plustek8200i.c:839`, `:864` |
| Physical pass dispatch | `backends/backend/plustek8200i.c:3998` |
| Initial sequence and lamp-drive requests | `backends/backend/plustek8200i.c:1773` |
| Home, intermediate, reinit, short move | `backends/backend/plustek8200i.c:2081`, `:2179`, `:2278`, `:2354` |
| Scan base and early discarded read | `backends/backend/plustek8200i.c:2484`, `:2559`, `:2654` |
| Dynamic image geometry and timing | `backends/backend/plustek8200i.c:2727` |
| Visible/IR calibration, fast feed, START flags | `backends/backend/plustek8200i.c:3282` |
| Fixed initialization lists and AFE | `backends/backend/plustek8200i-init.c:69`, `:231`, `:284`, `:348` |
| Constant-period tables and accepted-period bounds | `backends/backend/plustek8200i-init.c:543` |
| Scan-base lists and buffer boundaries | `backends/backend/plustek8200i-init.c:654` |
| Verified sensor set, AFE defaults, IR flags | `backends/backend/plustek8200i-init.c:901` |
| Home ramp | `backends/backend/plustek8200i-motor-tables.c:92` |
| Fast feed register preservation and ramps | `backends/backend/plustek8200i-feed.c:49`, `:138`, `:163`; `backends/backend/plustek8200i-feed.h:30` |
| Visible shading and IR reference | `backends/backend/plustek8200i-shading.c:73`, `:217`, `:412` |
| Image reconstruction and full-frame path | `backends/backend/plustek8200i.c:1266`, `:1273`, `:1331`, `:3429`, `:3568` |
| IR detection and repair | `backends/backend/plustek8200i.c:934`, `:1205` |
| Streaming, two frames, cancellation, cleanup | `backends/backend/plustek8200i.c:1464`, `:1580`, `:1717`, `:1733`, `:4124`, `:4180`, `:4261` |
| Scan stop and abort motor operations | `backends/backend/plustek8200i-scan.c:127`; `backends/backend/plustek8200i-motor.c:159` |
| Measured full-frame dimension fixtures | `backends/testsuite/backend/plustek8200i/tests.c:63` |

## Extraction method

The specification is authored prose, formulas, sequences, and synthetic
examples. It reproduces neither driver functions nor their implementation
structure. The protocol JSON retains numerical hardware data: ordered
register pairs, AFE index/value triples, and measured motor-table bytes.
It contains no C declarations, implementation comments, source symbols,
vendor decompilation, executable image-processing code, or repository paths.

The register catalog was extracted from literal byte arrays, except for the
initial 232-byte list, which was extracted from its sequential constant
assignments. C comments were removed before collecting values. The four
post-home register blocks were concatenated preserving their transmission
order. The runtime-patched feed list is explicitly identified as a template;
the specification supplies the patched feed length and ramp-selection byte.
Each payload has a decoded-byte SHA-256 digest and byte count. Independent
checks compare the packaged bytes against the source literals, and verify
register/AFE record sizes and ramp lengths.

No proprietary firmware image is needed by the examined scan path. The
motor ramps are exact numeric wire payloads retained from this driver; their
byte values are supplied so the implementer need not retrieve constants from
source or reconstruct an unproven curve-generation algorithm.

## Resolving conflicting descriptions

The current executable path and macro values control the specification.
Examples of comments/documents not carried forward as normative facts:

- The older README and many older file comments called this a port of
  another driver or a driver for a different Genesys chip and say it is incomplete. Current USB ID/manual/source
  identify the device as GL128 `07b3:1825`; the verified sensor path is
  selected by default and is the working baseline supplied by the user.
- The manual and some comments say high resolutions are vertically
  upsampled. The live geometry code clamps the target to at least 1490,
  increases it above that floor, slows the motor, and bypasses vertical
  downsampling when the ratio is 1. The packet follows those expressions.
- Gray is called an average in some prose, but both current pipelines
  use 299/587/114 integer luminance weights. IR uses a normalized mean.
- Old comments invert AFE gain/offset indices and multibyte register
  order. The current gain calibration writes 02..04, offset defaults
  occupy 05..07, and LINCNT/FEEDL have high bytes at their first address.
- Some MAXWD comments claim it is a raw-line byte stride. The live
  path deliberately retains `0x2f04c0`, including IR bit handling, and
  computes raw line size separately. The packet does not assert an
  unproven internal field interpretation.
- Scan-stop prose in `plustek8200i-scan.c` says bit 7, but the actual
  operation uses `CISSET_SCAN`, defined as `0x01`. The packet uses bit 0.
- Obsolete comments discuss desegmentation and R/GB demosaic. The
  current verified configuration processes contiguous RGB triples.
- A header declares `plustek8200i_set_scan_parameters`, but the active
  scan path programs geometry directly. It is not treated as a live
  alternative scan path.

## Qualified behavior recorded in the packet

The 500..20000 period check in the constant-table loader falls back to
1450 outside that range. At 7200 dpi, the base period is 7006, so the
advertised 1..4 exposure range can exceed that bound. The packet reports
the actual substitution and does not invent a corrected configuration.

The streaming path can finish a vertical crop without draining all raw
rows. Its EOF path clears SCAN and marks hardware done; the packet calls
for a repeated cropped/full-frame hardware acceptance case rather than
claiming residual FIFO behavior has been measured during extraction.

Several error results are intentionally ignored by the reference; the
buffered image path even retains a zero-filled partial image on read error.
The packet separates these policies from successful-path requirements.

Raw sample casts in the reference assume native order equals sensor
little-endian order. The packet states explicit sensor decoding and native
SANE serialization as separate requirements; big-endian source execution
was not claimed or tested.

These qualifications are documentation of the supplied snapshot, not
driver fixes, and do not negate the user's successful hardware verification.

## Offline validation performed

All 18 packaged payloads were re-extracted and compared byte-for-byte with
the examined source. Their lengths, SHA-256 digests, register-pair and AFE
record alignment, ramp sizes, and absence of START in the initial register
list passed. The source digest manifest matched the files on disk.

The geometry, crop, exposure, shading-table size/padding, AFE code-selection,
area-resample, registration/mirror, and Gray/IR arithmetic examples passed
independent calculations. Markdown links, fence pairing, and trailing
whitespace were checked. `git diff --check` reported no errors.

The existing no-hardware geometry suite passed all 24 checks, zero failures.
It was built and run without changing the driver or accessing USB:

```sh
gcc -I backends/include backends/testsuite/backend/plustek8200i/tests.c \
    backends/sanei/.libs/libsanei.a -o /tmp/plustek8200i-spec-geometry-tests
/tmp/plustek8200i-spec-geometry-tests
```

These checks validate extraction and stated examples. They do not claim
that every hardware acceptance case in the implementer packet was run.

## Source SHA-256 manifest

| Source | SHA-256 |
| --- | --- |
| `backends/backend/plustek8200i.c` | `89d1f726036000b7a573c0e2af45d3fcb6983ada9d083690eb15e3c3d64c639e` |
| `backends/backend/plustek8200i.h` | `c608a40e5114fefb93768059a062e53b8e10703e87ccf5188588fa6d4a928c19` |
| `backends/backend/plustek8200i-usb.c` | `792309793256d7e1668acb1a50cbbe06e396fa8ddbd7ce916a10999664c49b6e` |
| `backends/backend/plustek8200i-dma.c` | `5bff9ccfecd49dbe5037b932c707b8d3c6599fb78145e87b5164e02fbd5f8274` |
| `backends/backend/plustek8200i-scan.c` | `d902dd52d7bd9304d75b48ba0029df5bb2a071dda166dae04f2646e182649c28` |
| `backends/backend/plustek8200i-init.c` | `5374a61d2060a625f9fc5bc7cfff2602308dac6800e08614261f673bffa36c43` |
| `backends/backend/plustek8200i-motor-tables.c` | `3789650fb1e2ca031fda13368d8c4527bfcb502fb2aaafa33db55215e6b1e7dc` |
| `backends/backend/plustek8200i-motor.c` | `0eba5d3be11b101bd5b551524f9f7f986efca4aa46c7206daaf57a80ec422ed2` |
| `backends/backend/plustek8200i-feed.c` | `d51e94241102262951462be7c37185b29a991e61e8ac451d649f612ade142511` |
| `backends/backend/plustek8200i-mode.c` | `87ea110d8be5ee2c1a33848f957a594a62980b3428f070bc46a2901a9a60657a` |
| `backends/backend/plustek8200i-shading.c` | `d89f04296f2016c85cb6d69864db05399017612ff5c8ee9adc848bcfa59acff2` |
| `backends/testsuite/backend/plustek8200i/tests.c` | `c32aebc71e7c0594c3fc5c03fe8a49ab2082bdf30b240f22ba754458b371b017` |

The generated SANE stub `plustek8200i-s.c`, unrelated backends, archived
documents, and historical analyses are not derivation sources.
