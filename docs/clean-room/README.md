# Plustek OpticFilm 8200i GL128 implementation packet

Version 1, 2026-10-07. Device: USB `07b3:1825`.

This packet describes the behavior of the working driver supplied by the
scanner owner. It contains a behavioral specification, protocol constants,
and independently stated acceptance cases. An implementer can use it without
access to the reference implementation.

Read [SPECIFICATION.md](SPECIFICATION.md), use the exact wire data in
[protocol-data.json](protocol-data.json), and validate against
[CONFORMANCE.md](CONFORMANCE.md). These three documents and this README form
the complete implementer handoff.

The specification author inspected implementation source. For a clean-room
workflow, give a separate implementer only this directory; keep the original
source, decompilations, repository history, and the analysis record outside
their working environment. Questions about the hardware can go back to the
analyst, with answers added as behavioral facts or new test cases. A document
alone does not establish that this separation has occurred.

The separate `../clean-room-analysis/SOURCE-AUDIT.md` is for the analyst and
reviewer. It records the source snapshot, evidence locations, and qualifications;
it is not part of the implementer packet.

No scanner commands were issued while preparing this packet. The owner's
hardware verification is accepted as the baseline; the packet itself was
checked offline against the supplied source and its geometry expectations.
