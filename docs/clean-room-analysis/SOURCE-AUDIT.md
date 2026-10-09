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
| Device selection and endpoints | `backends/backend/plustek8200i.h:794`; `backends/backend/plustek8200i.c:549` |
| Register USB format and confirmation | `backends/backend/plustek8200i-usb.c:78`, `:170`; `backends/backend/plustek8200i-scan.c:94` |
| DMA requests, bulk padding, readiness | `backends/backend/plustek8200i-dma.c:119`, `:185`, `:241`, `:384` |
| Image FIFO header and chunk sizing | `backends/backend/plustek8200i-dma.c:451`, `:511` |
| Defaults and option dependencies | `backends/backend/plustek8200i.c:174`, `:369`, `:674` |
| Pre-start geometry and crop | `backends/backend/plustek8200i.h:833`; `backends/backend/plustek8200i.c:857`, `:882` |
| Physical pass dispatch | `backends/backend/plustek8200i.c:4217` |
| Initial sequence and lamp-drive requests | `backends/backend/plustek8200i.c:1804` |
| Home, intermediate, reinit, short move | `backends/backend/plustek8200i.c:2112`, `:2210`, `:2309`, `:2385` |
| Scan base and early discarded read | `backends/backend/plustek8200i.c:2515`, `:2591`, `:2686` |
| Dynamic image geometry and timing | `backends/backend/plustek8200i.c:2759` |
| Visible/IR calibration, fast feed, START flags | `backends/backend/plustek8200i.c:3364` |
| Fixed initialization lists and AFE | `backends/backend/plustek8200i-init.c:87`, `:249`, `:302`, `:366` |
| Constant-period tables and accepted-period bounds | `backends/backend/plustek8200i-init.c:561` |
| Scan-base lists and buffer boundaries | `backends/backend/plustek8200i-init.c:672` |
| Verified sensor set, AFE defaults/overrides, IR flags | `backends/backend/plustek8200i-init.c:920` |
| Home ramp | `backends/backend/plustek8200i-motor-tables.c:111` |
| Fast feed register preservation and ramps | `backends/backend/plustek8200i-feed.c:67`, `:156`, `:181`; `backends/backend/plustek8200i-feed.h:48` |
| Visible shading and IR reference | `backends/backend/plustek8200i-shading.c:91`, `:237`, `:432` |
| Image reconstruction and full-frame path | `backends/backend/plustek8200i.c:1284`, `:1291`, `:1349`, `:3530`, `:3751` |
| IR detection and repair | `backends/backend/plustek8200i.c:952`, `:1223` |
| Streaming, two frames, cancellation, cleanup | `backends/backend/plustek8200i.c:1490`, `:1606`, `:1743`, `:1764`, `:4343`, `:4399`, `:4480` |
| Scan stop and abort motor operations | `backends/backend/plustek8200i-scan.c:147`; `backends/backend/plustek8200i-motor.c:179` |
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

- The older README and numerous file comments call this a macOS port or
  a driver for a different Genesys chip and say it is incomplete. Current USB ID/manual/source
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
- Diagnostic dry-run prose says that nothing moves. The stop check
  occurs after homing, stationary scans, and the fast feed; it only
  suppresses image-pass START. It is not included as a motion-free
  acceptance command.
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
| `backends/backend/plustek8200i.c` | `542c10699fdd47c89596f9d618903d993272dc57184eb8d80a422880a005f2b0` |
| `backends/backend/plustek8200i.h` | `edf9c8ea397a7ccd12878b61d751d3b1369b2c84e9efb09d693ae908ce4754ae` |
| `backends/backend/plustek8200i-usb.c` | `ac3090a7456ae82e81a3af605a8c3ab6f97a3a8333601054f9c12c6394cdae78` |
| `backends/backend/plustek8200i-dma.c` | `81df263564ae5404b8c67bd522c8d66b2f03a69c6f267aa60dcc34adaea4bb19` |
| `backends/backend/plustek8200i-scan.c` | `28d9c23c603e4a9178ffb9bc1a5d11bc4077b212f0eaee811fc6ad6c04e8ae20` |
| `backends/backend/plustek8200i-init.c` | `d428b2b4a83109f64fb407c0a4707e6fc230cd5e211ab4ac45573b6ebc9fde7b` |
| `backends/backend/plustek8200i-motor-tables.c` | `46e7ecf6db4189ee55f631b29abe8c6f2c7c1d697bb2d97cc9b3dbb6b8b59e50` |
| `backends/backend/plustek8200i-motor.c` | `e3ed4792917dec7577e2e738b3078e6404cee859a3c56fc48709e696ba86f63c` |
| `backends/backend/plustek8200i-feed.c` | `11d04740bfd845dfee5e92a60eac1dd2a92717e79e2346bc4a455b474b027fbe` |
| `backends/backend/plustek8200i-mode.c` | `429577da82ebd44b3cd756e53562a4d31a329d72585207b45e8ccba92f8dd3f0` |
| `backends/backend/plustek8200i-shading.c` | `dcbb6a9e04c1887bd695a99af5b6344e63dd87fe85ba8af5f946b9fb6703ec60` |
| `backends/testsuite/backend/plustek8200i/tests.c` | `1df52e009689a0de274dfa7104fabb0ce29328b0f6eb29c41ecc1f7d315bee96` |

The generated SANE stub `plustek8200i-s.c`, unrelated backends, archived
documents, and historical analyses are not derivation sources.
