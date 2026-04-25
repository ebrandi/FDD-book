# Pseudo-Datasheet: <Device Name>

> Replace `<Device Name>` with the device's marketed name, internal
> codename, or model number. If the device has multiple names
> (manufacturer, distributor, OEM rebrand), list them all in the
> Identity section below.

**Version:** 0.1-rev
**Last updated:** YYYY-MM-DD
**Status:** Draft / In progress / Stable

This document is a pseudo-datasheet: a structured account of what
has been learned about the device through reverse engineering. It
is not an official datasheet from the device's manufacturer. Every
fact in this document is tagged with its provenance, so that
readers can judge how much confidence to place in any given claim.

## 1. Identity

| Field | Value | Provenance |
|-------|-------|------------|
| Marketed name | | |
| Internal codename | | |
| Vendor | | |
| Vendor ID (USB or PCI) | | |
| Product ID | | |
| Subsystem vendor ID | | |
| Subsystem ID | | |
| Revision | | |
| Form factor | | |
| Bus interface | | |

**Provenance tag legend.** Each fact in this datasheet is tagged
with one of four levels of confidence:

- **HIGH**: directly observed by the author; reproducible.
- **MEDIUM**: inferred from observation, with strong supporting
  evidence; reproducible but indirect.
- **LOW**: hypothesised from limited observation; not yet
  conclusively verified.
- **UNKNOWN**: not yet investigated, or investigated without
  conclusive results.

Any claim in the datasheet may be re-tagged as new evidence
appears. The Git history records the changes.

## 2. Provenance

This section describes the methodology used to gather the facts in
the rest of the document. It exists so that any reader can
understand the basis of the work and reproduce it if they wish.

- **Captures used.** List the captures (pcap files, register
  dumps, log files) that informed this datasheet. Reference them
  by filename and a one-line description.

- **Tools used.** List the tools used to gather and analyse the
  evidence: `pciconf`, `usbconfig`, `usbdump`, Wireshark,
  Ghidra, IDA, radare2, custom scripts, custom kernel modules.

- **Sources of prior art.** List any public sources consulted:
  driver code in other operating systems, vendor patents, leaked
  documentation, hobbyist write-ups. Include URLs and access
  dates.

- **Dates of work.** When the investigation began, when it has
  been actively worked on, when this version of the document was
  last updated.

- **Cleanroom discipline.** If the work was performed under a
  cleanroom procedure to keep it free of derivation from
  copyrighted material, describe the procedure briefly.

## 3. Resources

This section describes the bus-level resources the device exposes:
memory regions, I/O ports, interrupt lines, DMA channels.

- **Memory BARs.** For each memory BAR, list the BAR number, size,
  alignment, and known purpose. Include the provenance of the
  size (read from PCI configuration space) and of the purpose
  (inferred from access patterns).

- **I/O port ranges.** For each I/O range, list the range and
  known purpose.

- **Interrupts.** Describe the interrupt mechanism (legacy INTx,
  MSI, MSI-X), the number of vectors used, and the purpose of
  each vector if known.

- **DMA channels.** Describe any DMA channels the device uses,
  their direction, addressing capabilities, and known buffer
  formats.

## 4. Register Map

This is the central section of the datasheet. List every register
that has been identified, with its offset, size, access mode, and
purpose.

| Offset | Name | Width | Access | Purpose | Provenance |
|--------|------|-------|--------|---------|------------|
| 0x00 | | 32 | RO | | |
| 0x04 | | 32 | RW | | |
| 0x08 | | 32 | W1C | | |
| ... | | | | | |

Access modes:

- **RO**: read-only.
- **WO**: write-only (reads return undefined or zero).
- **RW**: read-write.
- **W1C**: write-1-to-clear (writing 1 to a bit clears it).
- **W1S**: write-1-to-set (writing 1 to a bit sets it).
- **RWO**: read-write-once (writable once, then becomes read-only).
- **VOL**: read-volatile (value can change between reads).

For each register, include a sub-section with the bit-field
breakdown.

### Register: `<NAME>` (offset `0xNN`)

| Bits | Name | Access | Description | Provenance |
|------|------|--------|-------------|------------|
| 31:24 | | | | |
| 23:16 | | | | |
| 15:8 | | | | |
| 7:0 | | | | |

### Reserved Bits and Offsets

List any bits or offsets that are known to be reserved (writes
must preserve their current values; reads return undefined data).
Reserved means "do not touch", not "ignore".

## 5. Buffer Layouts

If the device uses ring buffers, descriptor rings, or other
in-memory data structures, document them here.

For each buffer:

- The C structure that describes one entry.
- The number of entries in the ring (or the size of the ring in
  bytes).
- The producer and consumer pointers (which side updates each).
- The synchronisation mechanism (memory barriers, doorbell
  registers, interrupts).
- Endianness, if it differs from the host's.
- Provenance for each of these claims.

## 6. Command Interface

If the device uses a command/status mailbox or a similar
high-level interface, document it here.

- The command opcodes, with their parameters and return values.
- The completion mechanism (status register, interrupt,
  descriptor completion).
- Error codes and their meanings.
- Any sequencing requirements (commands that must be issued in a
  specific order, or that depend on the device's state).

## 7. Initialisation Sequence

The exact sequence of operations the driver must perform to bring
the device from reset to operational. Each step should include the
register accesses, the values, and the verification that confirms
the step succeeded.

```
1. Soft reset:
   - Write 0x80000000 to CTRL (offset 0x00).
   - Wait for bit 31 of STATUS (offset 0x04) to clear.
   - Timeout: 100 ms.
   - Provenance: HIGH (observed in vendor capture, reproducible).

2. Verify identifier:
   - Read CHIP_ID (offset 0xfc).
   - Expect 0x4d4f434b.
   - On mismatch: abort attach.
   - Provenance: HIGH (observed in vendor capture).

3. ...
```

## 8. Operating Patterns

Common operations performed during normal use, with the register
sequences they generate. This section is the bridge between the
register map and the high-level driver behaviour.

For each operating pattern:

- The user-facing operation (e.g., "transmit a packet").
- The register-access sequence the driver performs.
- The expected device behaviour (counters that change,
  interrupts that fire, status bits that toggle).
- Any common pitfalls (race conditions, ordering requirements,
  values that must not be written).

## 9. Quirks and Errata

A list of unexpected behaviours that the driver must work around.
Each quirk is a fact about the device that no one would have
guessed from a normal reading of the register map.

For each quirk:

- A short description.
- The conditions under which it manifests.
- The work-around that the driver applies.
- Whether the quirk is known to be revision-specific.
- Provenance for each claim.

## 10. Open Questions

Things you do not yet know about the device. Recording open
questions explicitly is as valuable as recording known facts: it
tells future readers where to focus further investigation.

For each open question:

- A clear statement of the question.
- What you have already tried.
- What you would want to try next.
- Why the question matters.

## 11. References

External material that informed this datasheet. URLs, documents,
source code, conversations.

- [ ] Vendor patent: ...
- [ ] Linux driver: ...
- [ ] OpenBSD driver: ...
- [ ] Mailing list discussion: ...
- [ ] Personal correspondence: ...

## 12. Changelog

A record of significant updates to this document. Distinct from
the Git history because it summarises the substantive changes
rather than every textual change.

| Date | Author | Summary |
|------|--------|---------|
| YYYY-MM-DD | | Initial draft. |
