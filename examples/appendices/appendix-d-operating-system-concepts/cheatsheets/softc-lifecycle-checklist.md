# Softc Lifecycle Checklist

A one-page audit sheet for the per-device state structure. Use it when
you review a driver's `probe`, `attach`, `detach`, `suspend`, and
`resume` paths. Most lifecycle bugs are checklist violations you can
name on paper.

## The Big Picture

```
+---------+      +---------+      +-------------+     +---------+
|  probe  | ---> | attach  | ---> |   runtime   | --> |  detach |
+---------+      +---------+      +-------------+     +---------+
                    |                   |                |
                    v                   v                v
             [softc allocated]   [softc in use]    [softc freed by
              and initialised    for every         the kernel, after
              in this order:     operation;        this driver's
                                 all state under   detach cleanup]
              1. device_get_softc  the softc lock]
              2. init sc->sc_lock
              3. alloc resources
              4. setup_intr
              5. publish (/dev,
                 ifnet, GEOM)
```

The kernel zeroes the softc before handing it to `attach`. Nothing is
"left over" from a previous attach.

## Probe Checklist

- [ ] Read only what is needed to identify the device (vendor/device
      IDs, class codes, FDT compatibility strings).
- [ ] Return `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, or a more
      specific value depending on confidence.
- [ ] Do **not** allocate resources in `probe`.
- [ ] Do **not** store long-lived state anywhere (the softc is not
      yours yet).
- [ ] Set `device_set_desc` so that `devinfo(8)` has a human label.

## Attach Checklist

- [ ] Get the softc: `sc = device_get_softc(dev)`.
- [ ] Stash back-pointer: `sc->dev = dev`.
- [ ] Initialise the softc lock: `mtx_init(&sc->sc_lock, ...)`.
- [ ] Allocate memory (`malloc(9)`), queues, and internal state.
- [ ] Allocate bus resources (`bus_alloc_resource_any`) in the order
      that matches the `detach` release order (you will release in
      reverse).
- [ ] Map MMIO through `bus_space`, not raw pointers.
- [ ] Install the interrupt last (`bus_setup_intr`), so the handler
      cannot fire before the softc is ready.
- [ ] Publish the device to its subsystem (`make_dev_s`, `if_attach`,
      `g_new_providerf`, etc.) only after the softc is fully ready.
- [ ] On any failure, unwind in reverse through a `goto fail_N:` ladder.

## Runtime Checklist

- [ ] Every entry point starts by fetching `sc = device_get_softc(dev)`.
- [ ] Every field access that can race is protected by the softc lock.
- [ ] Long operations (allocation with `M_WAITOK`, copyin/copyout)
      happen with the lock dropped, or under an `sx(9)` lock designed
      for the purpose.
- [ ] Interrupt filters take only spin locks; sleep locks are forbidden.
- [ ] Sysctl handlers walk the softc under the right lock too.

## Suspend Checklist

- [ ] Stop hardware DMA and pending work before the system suspends.
- [ ] Snapshot volatile register state that the hardware will lose.
- [ ] Cancel any callouts with `callout_drain` (not just `callout_stop`).
- [ ] Return `0` only after the device is quiescent.

## Resume Checklist

- [ ] Restore the snapshot taken in `suspend`.
- [ ] Re-arm interrupts, DMA engines, timers, callouts.
- [ ] Replay any state the hardware requires (NIC MAC filter,
      transceiver mode, clock dividers).

## Detach Checklist

Walk the attach steps in reverse.

- [ ] Unpublish from the subsystem (`destroy_dev`, `if_detach`,
      `g_wither_geom`, etc.).
- [ ] Tear down the interrupt (`bus_teardown_intr`).
- [ ] Drain any callout, taskqueue, or kernel thread the driver owns.
- [ ] Release bus resources in reverse order.
- [ ] Free allocated memory and queue contents.
- [ ] Destroy the softc lock (`mtx_destroy(&sc->sc_lock)`).
- [ ] Return `0` only after nothing can dereference the softc anymore.

## Common Traps

- **Globals masquerading as "single device" convenience.** If a second
  instance of the device appears, the two will share the globals. Keep
  everything in the softc.
- **Interrupt set up before the softc is ready.** The handler can fire
  between `bus_setup_intr` and the rest of `attach`. Install interrupts
  last.
- **Detach that frees memory the interrupt still uses.** Tear down the
  interrupt before freeing the state it references.
- **`callout_stop` instead of `callout_drain` in `detach`.** `stop`
  cancels a pending callout but does not wait for a running one; a
  driver can be freed while a callout is mid-execution.
- **Forgetting `mtx_destroy` in `detach`.** The mutex leaks; repeated
  load/unload cycles accumulate leaked locks.

## Where to Read More

- `device_get_softc(9)`, `mtx(9)`, `callout(9)`.
- Chapter 6 for the lifecycle overview, Chapter 7 for a full
  implementation, Chapter 11 for locking discipline.
