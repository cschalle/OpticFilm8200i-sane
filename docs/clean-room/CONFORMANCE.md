# Conformance and acceptance cases

These cases are part of the implementer packet. They use the specification's
formulas and wire constants, with synthetic image examples. They do not
require access to implementation source. Hardware cases at the end are
acceptance procedures, not claims of new measurements by the specification
author.

## 1. Payload integrity

Decode every `hex` string in `protocol-data.json`. Assert its byte count and
SHA-256 digest match the adjacent metadata. Register payloads have even byte
counts; AFE payloads have byte counts divisible by three; every motor ramp
contains exactly 512 bytes, or 256 little-endian periods.

| Payload | Bytes | Register control-write lengths |
| --- | --- | --- |
| INITIAL_REGISTERS | 232 | 64,64,64,40 |
| POST_HOME_REGISTERS | 250 | 64,64,64,58 |
| SCAN_BASE_1 | 64 | 64 |
| SCAN_BASE_2 | 34 | 34 |
| VERIFIED_SENSOR | 40 | 40 |
| HOME_MOVE | 12 | 12 |
| RETURN_MOVE | 38 | 38 |
| SHORT_MOVE | 12 | 12 |
| CLEAR_INTERRUPTS | 18 | 18 |
| FEED_REGISTERS | 84 | 64,20 |
| FEED_SAVED_ADDRESSES | 42 | Not a register-write payload |

Verify that `INITIAL_REGISTERS` does not write `0f`: a START command must
not occur accidentally inside that bulk initialization list. Verify all
eight initial AFE triples produce eight separate six-byte control writes.

## 2. Transport vectors

| Operation | Expected request fields and bytes |
| --- | --- |
| Write register `03=30` | OUT `40/a0/0083/0000`, length 2, `03 30` |
| Shadow read `01` | IN `c0/a0/008e/0122`, length 2 |
| Live read `01` | IN `c0/a0/018e/0122`, length 2 |
| Live DMA status `18` | IN `c0/a0/018e/1822`, length 2 |
| Write 512 logical bytes at `1000c000` | OUT `40/a0/0082/0001`, length 8, `00 c0 00 10 00 02 00 00` |
| Early 3072-byte memory read | OUT `40/a0/0082/0000`, length 8, `00 00 00 10 00 0c 00 00` |
| Maximum image chunk | OUT `40/04/0082/0000`, length 8, `00 00 00 10 00 ee 00 00`, then bulk IN 60928 bytes |
| Image remainder 100 bytes | OUT `40/04/0082/0000`, length 8, `00 00 00 10 64 00 00 00`, then bulk IN 100 bytes |
| AFE gain G=`001a` | OUT `40/a0/0083/0000`, length 6, `51 03 5d 00 5e 1a` |

Use a fake transport to feed DMA status values `0c,04,00`. Ordinary DMA
write must wait through both busy responses before setup. The short-move
table path must have no pre-setup poll. After bulk OUT, both paths poll
until ready. A persistent `0c` must expire the polling budget.

Test image reads with remaining=1024: one setup requests 1024; if only
512 bytes arrive, issue a new 512-byte setup for the same FIFO address.
Do not increment the FIFO address. A zero-byte read is an error. Assemble
a synthetic image split at odd-byte and mid-row boundaries and verify
its reconstruction matches an unsplit stream.

For a 600-byte logical memory write, verify the setup length stays 600
while the bulk traffic is 512 bytes followed by a second 512-byte block
containing the remaining 88 data bytes and 424 zero bytes.

## 3. Sequence and state assertions

A fake-device command log for one pass must contain:

1. The two separate lamp-drive requests before the first initialization.
2. Three 232-byte initial lists and their specified AFE writes; the
   third post-initialization adjustment does not toggle `0b=84/8c`.
3. Home and return moves, then the 250-byte reinitialization, then the
   short move. `0f=01` always follows that move's register/table setup.
4. The scan-base lists and early memory read with request `a0`.
5. Dynamic geometry, verified sensor values, AFE settings, and, only at
   7200 dpi, the last timing override `28=00,29=3e,2a=5b,2b=17`.
6. Stationary calibration before the 14,860-step fast feed.
7. Feed register restoration and restoration of both short-move ramps
   before `FEEDL=1`, counter clears, and the image START.
8. Image FIFO requests using `04`, while acquisition runs.
9. Scan-control bit 0 clear at normal finish, with no ordinary motor
   power-off operation while the automatic home return runs.

The baseline also issues optional diagnostic reads (specification section
4.10). A trace may contain them; an implementation that omits them still
conforms, so do not require or forbid them in these checks.

For fast-feed polling, provide `20,21,01,20` as live status values.
The first `20` must not complete the feed; `21` establishes running;
the last `20` completes it. In the starting `20` state, bit 5 is the
stale completion flag and bit 0 is already clear.

For the previous-return wait, status `09` means the sensor is set but
the motor is still enabled: continue polling. Status `08` means home
and idle: proceed. Home/return/short-move completion polling itself
tests bit 0 clear and does not wait for bit 3 clear.

For IR, verify white lamp bit `10` is clear and register `37` bit `04`
is set before image START. Its image scan-control value is `03`, never
`23`. For successful visible shading, image scan-control is `23`.

## 4. Geometry vectors

| dpi | Selector | Width | Raw rows | Output rows | Base period | RGB output bytes/line, 16 bit |
| --- | --- | --- | --- | --- | --- | --- |
| 300 | 50 | 432 | 1604 | 322 | 1450 | 2592 |
| 600 | 100 | 864 | 1604 | 645 | 1450 | 5184 |
| 1200 | 200 | 1728 | 1604 | 1291 | 1450 | 10368 |
| 2400 | 400 | 3456 | 2585 | 2585 | 2335 | 20736 |
| 3600 | 600 | 5184 | 3877 | 3877 | 3503 | 31104 |
| 7200 | 1200 | 10368 | 7755 | 7755 | 7006 | 62208 |

The full crop at the advertised 36.5-by-27.2-mm maxima must give exactly
these sizes, including the last column and row. Parameter queries before
START and actual delivered frame dimensions must agree.

At 600 dpi, crop left=1 mm, top=2 mm, right=10 mm, bottom=12 mm gives
exclusive pixel bounds `(23,47)..(236,283)`, a 213-by-236 output. In a
SANE fixed-point adapter use its actual decoded fixed-point values;
these particular coordinates are exactly representable. Reverse or
empty coordinates must still produce a bounded one-pixel minimum.

At 600 dpi, exposure 1.25 produces exposure register value 17500
(`00 44 5c`) and image motor period 1813 (`15 07` in each table entry).
The nominal output dimensions do not change. At 7200 dpi, exposure
3.0 requests period 21018, outside the loader's accepted range; the
baseline writes 1450 instead. Treat that as a qualified limitation,
not a successful geometry-preservation test.

## 5. Image arithmetic vectors

### 5.1 Area resampling

For a single component with raw rows `[100,200,300,400,500]` and ratio
2.5, two output rows must be `[180,420]`. The first row weights the
third input by 0.5; the second weights it by the other 0.5. Adding a
constant 1000 to every input must add exactly 1000 to each output.
Do not replace this with dropped rows or nearest-neighbor sampling.

### 5.2 Registration and mirror

Given these four resampled rows, two pixels per row:

```text
row 0: (100,200,300)       (110,210,310)
row 1: (1100,1200,1300)    (1110,1210,1310)
row 2: (2100,2200,2300)    (2110,2210,2310)
row 3: (3100,3200,3300)    (3110,3210,3310)
```

At shift=1, registration followed by mirror must give:

```text
row 0: (110,210,1310)      (100,200,1300)
row 1: (110,1210,2310)     (100,1200,2300)
row 2: (1110,2210,3310)    (1100,2200,3300)
row 3: (2110,3210,3310)    (2100,3200,3300)
```

### 5.3 Gray, IR, and depth

Registered RGB `(10000,20000,30000)` gives Gray=18150. IR with scale
1.25 gives 25000. The corresponding 8-bit gray and IR values are
70 and 97. A 16-bit wire sample `34 12` decodes to `0x1234`; 8-bit
output is `12`. Its 16-bit SANE output is `34 12` on a little-endian
host and `12 34` on a big-endian host.

White balance on repeated RGB `(10000,20000,30000)` gives
`(30000,30000,30000)` before crop. Test rounding and clipping separately
with a nonuniform image. Color+Infrared must ignore automatic white
balance and preserve the captured normalized IR frame for export.

## 6. Calibration vectors

For a class with 100 sorted light values, the p99 result is element
99, the largest. With gain-0 channel tops 30000,40000,50000, the
minimum gain codes to reach `0.9*65535/top` are 38,25,12 respectively.
The computed trim must make `top*gain(code)*trim` equal 58981.5 to
floating-point precision, since `gain(0)=1`.

A raw white peak of 65001 rejects shading; a peak of 65000 does not
reject it on that criterion. A channel top below 1000 rejects shading.
White-strip relative drift exactly 0.002 does not meet the strict
stability condition. Successful table upload is required before DVDSET.

For 43 pixels, the shading table has 524 logical bytes: 42 twelve-byte
records, 8 zero bytes, then one twelve-byte record. For a pixel with
dark terms `(1,2,3)` and unity gains, its record is:

`01 00 00 20 02 00 00 20 03 00 00 20`.

Padding after pixel 41 occupies offsets 504..511; pixel 42 starts at
offset 512. At widths 432,864,10368, logical table lengths are
5264,10528,126384 bytes. Their bulk OUT padded totals are
5632,10752,126464 bytes. The setup length remains logical.

For IR green light p99=50000, its normalization scale is 1.16. A
mean registered image sample of 50000 therefore becomes 58000.
Unusable reference data leaves scale 1.0 and does not enable shading.

## 7. Defect detection and interpolation

Use a sufficiently large synthetic IR rectangle with uniform illumination
and isolated dark pixels. Flat-fielded uniform samples normalize to 32768;
MAD is floored to 1. Isolated samples substantially below the resulting
local threshold must be marked, while pixels outside the inferred
interior remain unchanged. Repeat with smooth column falloff: the
column/row normalization must prevent a smooth dim edge from becoming
an entire repaired band.

For a defective run of length two with neighboring color component
values 100 and 400, the repaired values are 200 and 300. For descending
neighbors 400 and 100, they are 300 and 200. To check signed division,
neighbors 4 and 0 around two defective samples give 3 and 2, not 2 and 1.
Runs longer than `floor(interior_width/32)` remain unchanged. A boundary
run with only one usable neighbor repeats that neighbor.

## 8. Lifecycle checks without hardware

Use a fake transport and synthetic acquisition data to verify:

- Arbitrary read-buffer sizes (1,3,511,65536 bytes) concatenate to
  exactly the advertised frame size and identical pixels.
- Streamed and buffered reconstruction agree for Color, Gray, and IR,
  at both depths, with crops and edge registration.
- Two-frame output returns RGB EOF, then a memory-backed IR frame
  after the next start; no USB START is issued for that second frame.
- IR repair does not replace the second exported frame with its mask.
- An in-flight cancellation issues no new USB operation until the
  current transfer returns; it then frees all pass storage.
- Normal completed-frame cancellation does not power off the returning
  carriage; a duplicate open is device-busy; an unknown name is invalid.
- A transport failure is distinguishable from EOF and from unusable
  calibration. A streaming error leaves no retained raw ring or IR plane.

## 9. Hardware acceptance

Using the owner's working driver as the behavioral reference, record USB
transactions and output for the same physical unit, holder position, and
film. Cover all six resolutions, all modes, both depths, full and partial
windows, repair on/off, and exposures whose table period stays accepted.

Compare request types, payload order, table hashes, START placement,
poll conditions, raw byte counts, and frame metadata. Ignore the optional
diagnostic reads of specification section 4.10 in the comparison. Strip means,
calibrated gain codes, image samples, and timing can vary with sensor
noise, lamp stabilization, and exposure; expect those to be measured
comparisons rather than literal whole-image hashes.

Use a reference frame with recognizable orientation and horizontal edges
to check mirror direction and channel registration. Use film borders to
check positioning and aspect. Use a frame with dust or fine scratches
to check IR behavior and repair. Compare raw/processed IR with the
visible frame to assess pass alignment without adding an undocumented
registration transform.

Repeat consecutive scans without power cycling, after another scanner
application, and after cancellation. Include a short vertical crop
followed immediately by a full scan, and cancellation during initialization,
calibration, image transfer, and between the two delivered frames. Verify
that the carriage reaches home and idle and that the next scan succeeds.

Record any validated extension separately: periods above 20000, other
USB IDs, simultaneous multiple devices, and alternate timing profiles
are outside the baseline successful-path coverage.
