# DMA Mental-Model Checklist

Audit a DMA path end to end. Walk this checklist when you are
designing a new DMA-capable driver, reviewing one, or debugging a
transfer that works on one machine and fails on another. Full teaching
lives in Appendix C and in Chapter 21. This sheet only helps you
verify.

## The question to ask first

Who touches the buffer, in what direction, and in what order?

- The CPU writes -> the device reads -> the CPU frees or reuses.
- The device writes -> the CPU reads -> the CPU frees or reuses.
- Round-trip: CPU writes, device reads, device writes, CPU reads.

Once you can say this out loud for your buffer, the sync calls follow
mechanically. If you cannot say it, stop and draw it on paper first.

## The five-stage sequence

Every DMA path in FreeBSD has the same five stages. Every bug lives
somewhere in this diagram. Audit each arrow.

```
(1) bus_dma_tag_create(parent, align, boundary,
                       lowaddr, highaddr, filter,
                       filterarg, maxsize, nsegments,
                       maxsegsz, flags, lockfunc,
                       lockarg, &tag)
        |
        v
(2) bus_dmamem_alloc(tag, &vaddr, flags, &map)
        or
    bus_dmamap_create(tag, flags, &map) + load later
        |
        v
(3) bus_dmamap_load(tag, map, vaddr, length,
                    callback, arg, flags)
        -> callback receives bus_dma_segment_t entries
        -> each segment has ds_addr (bus address) and ds_len
        |
        v
(4) Hand ds_addr values to the device descriptor.
    Before the transfer:
        bus_dmamap_sync(tag, map, BUS_DMASYNC_PRE...)
    After the transfer:
        bus_dmamap_sync(tag, map, BUS_DMASYNC_POST...)
        |
        v
(5) bus_dmamap_unload(tag, map)
    bus_dmamem_free(tag, vaddr, map)
    bus_dma_tag_destroy(tag)   (usually in detach)
```

## Stage-by-stage audit

### Stage 1: Tag

- [ ] Parent tag is `bus_get_dma_tag(dev)` (inherits platform limits)
      unless the device needs a custom parent.
- [ ] Alignment matches the device's actual alignment requirement.
- [ ] Boundary is zero unless the device cannot cross a page-size or
      other hardware boundary.
- [ ] `lowaddr` is the highest bus address the device can reach.
      `BUS_SPACE_MAXADDR_32BIT` for a 32-bit device.
- [ ] `highaddr` is `BUS_SPACE_MAXADDR` unless you are restricting on
      purpose.
- [ ] `maxsize`, `nsegments`, and `maxsegsz` match what the device
      actually accepts in a single transfer.

### Stage 2: Allocation

- [ ] For DMA buffers the device touches: `bus_dmamem_alloc`, not
      plain `malloc`.
- [ ] For control structures the device never touches: `malloc(9)`.
- [ ] If you pre-create a map for user-supplied buffers: `bus_dmamap_create`,
      remember to destroy with `bus_dmamap_destroy`.
- [ ] Flags: `BUS_DMA_WAITOK` in process context, `BUS_DMA_NOWAIT` in
      atomic contexts; check the return value in the nowait case.
- [ ] Consider `BUS_DMA_COHERENT` and `BUS_DMA_ZERO` if appropriate.

### Stage 3: Load

- [ ] Callback handles the error case (`error != 0`) and does not
      assume the callback always runs synchronously.
- [ ] Callback inspects `nseg` and walks every segment; do not assume
      one segment unless the tag guarantees it.
- [ ] `ds_addr` is treated as a bus address, not a physical or
      virtual address. Never printed or compared as if it were a
      physical address.

### Stage 4: Use

- [ ] Before the device reads: `BUS_DMASYNC_PREWRITE`.
- [ ] Before the device writes: `BUS_DMASYNC_PREREAD`.
- [ ] After the device wrote (CPU about to read): `BUS_DMASYNC_POSTREAD`.
- [ ] After the device read (buffer reusable): `BUS_DMASYNC_POSTWRITE`.
- [ ] The sync calls are present on *every* architecture build,
      even when they happen to be no-ops on coherent platforms.

### Stage 5: Teardown

- [ ] `bus_dmamap_unload` runs before `bus_dmamem_free`.
- [ ] `bus_dmamem_free` runs before `bus_dma_tag_destroy`.
- [ ] Teardown order mirrors setup order, in reverse.
- [ ] Detach path waits for all in-flight transfers before unloading
      any map. A map cannot be safely unloaded while the device is
      still reading or writing the buffer.

## Sync direction reminder

The `READ` and `WRITE` halves in the flag names follow the wording
used in `bus_dma(9)`:

- `PREREAD`  = "the device is about to update host memory, and the
  CPU will later read what was written".
- `PREWRITE` = "the CPU has updated host memory, and the device is
  about to access it".
- `POSTREAD`  is the companion to `PREREAD`, issued after the device
  has written and before the CPU reads.
- `POSTWRITE` is the companion to `PREWRITE`, issued after the device
  has finished reading the buffer.

Mapped to the two directions a driver actually thinks about:

| CPU intent | Device operation | Sync calls |
| :-- | :-- | :-- |
| "Device, please read this" | Device reads the buffer | PREWRITE before, POSTWRITE after |
| "Device, please write here" | Device writes the buffer | PREREAD before, POSTREAD after |
| "Round trip" (CPU fills, device rewrites) | Device reads and writes | PREWRITE + PREREAD before, POSTREAD + POSTWRITE after |

If the names feel backwards, remember that they describe what the
*CPU* will need to do with the buffer next: `PREREAD` prepares for a
later CPU read (so the device must be writing first), and `PREWRITE`
prepares for a device access to bytes the CPU just wrote.

## Anti-patterns to avoid

Do not cast a kernel virtual pointer to a `uint64_t` and hand it to
the device. Use `ds_addr`.

Do not call `vtophys()` in a driver that has a `bus_dma` tag. The
layer already does the right thing; calling around it silently breaks
IOMMU setups.

Do not skip the sync calls on amd64 because "it works". It will not
work on non-coherent platforms, and it will not work forever on amd64.

Do not free a DMA buffer while the device may still be reading or
writing it. A descriptor ring that the hardware owns must be
quiesced before the buffer is released.

Do not allocate the DMA tag with `lowaddr = BUS_SPACE_MAXADDR` on a
32-bit-only device. The driver will appear to work under an IOMMU and
fail on systems without one.

## Debugging checklist

- [ ] Confirm `pci_enable_busmaster(dev)` was called in attach for a
      PCI device.
- [ ] Confirm `ds_addr` values look plausible for the device (not
      0xffffffff, not near the kernel's virtual range).
- [ ] Confirm sync calls bracket every device use.
- [ ] Confirm `bus_dmamap_unload` runs before any buffer reuse.
- [ ] Add a `sysctl` counter for successful and failed loads; a burst
      of failures is often the first symptom.
- [ ] Rerun with `WITNESS` and `INVARIANTS` enabled; the kernel will
      catch many misuses at the point of the mistake.

## Cross-references

- Appendix C, section **DMA**, for the full mental model.
- Chapter 21 for the line-by-line teaching.
- `bus_dma(9)`, `/usr/src/sys/sys/bus_dma.h`, and `/usr/src/sys/kern/subr_bus_dma.c`.
- Real drivers that use `bus_dma` extensively: most NIC drivers under
  `/usr/src/sys/dev/` (for example, drivers under `/usr/src/sys/dev/e1000/`).
