# Interrupt vs Polling Cheatsheet

Quick reference for deciding how a driver should learn about events
from its device. Full teaching lives in Appendix C and in Chapters 19
and 20. This sheet only helps you pick.

## The question to ask first

What is the *event rate* for this device, and how sensitive is the
driver to latency?

- If events are rare and latency matters, use an interrupt.
- If events are frequent and dominate the CPU, consider polling
  inside an ithread or a taskqueue instead.
- Do not decide by reflex. Measure the event rate before designing
  the data path.

## Comparison at a glance

| You should use... | When... | Cost |
| :-- | :-- | :-- |
| A hardware interrupt | Event rate is moderate, latency matters | Interrupt setup, filter discipline, context switches |
| Polling in an ithread | Event rate is very high and interrupts dominate | Pinned CPU time, no latency from interrupt dispatch |
| A taskqueue triggered by interrupts | Work is heavy and cannot run in the filter | A thread wakeup per batch, not per event |
| A callout (timer) plus occasional interrupts | Device is slow, state is simple | Periodic wake-up, possibly imprecise timing |
| Pure polling in a kernel thread | The device has no interrupt, or the spec forbids interrupts | CPU time scales with polling rate |

## Decision ladder

```
Does the device raise an interrupt on the event?
|-- No  -> Polling loop in a kernel thread or callout; make sure the
|          rate is tolerable.
|-- Yes -> Is the per-event work small enough for a filter?
          |-- Yes -> Filter-only handler (FILTER_HANDLED).
          |-- No  -> Filter that acknowledges the device and returns
                     FILTER_SCHEDULE_THREAD; ithread or taskqueue
                     does the real work.

Is the event rate so high that interrupts dominate the CPU?
|-- No  -> Stay with the interrupt model.
|-- Yes -> Consider disabling interrupts while an ithread polls for a
           batch, re-enabling when the queue is empty. This is the
           iflib pattern for fast NICs.
```

## Interrupt discipline checklist

If you chose the interrupt path, walk this checklist before you ship.

- [ ] The filter is short and allocation-free.
- [ ] The filter takes spin locks only, never sleep mutexes.
- [ ] The filter acknowledges the device before returning
      `FILTER_HANDLED`, or the device keeps asserting until the
      ithread runs.
- [ ] The filter returns `FILTER_STRAY` when the interrupt did not
      come from this driver's hardware (legacy INTx path).
- [ ] A per-vector counter is exported through `sysctl` so you can
      see the interrupt rate in a live system.
- [ ] The ithread (or taskqueue) is the only place that does heavy
      work, allocations, or sleeping.
- [ ] Teardown is the reverse of setup: `bus_teardown_intr` before
      `bus_release_resource`.

## Edge vs level reminder

```
Edge-triggered
    __                __
   |  |              |  |
___|  |______________|  |____
       ^                 ^
       one event         one event
       (lose it once and it is gone)

Level-triggered
                ____________
               |            |
_______________|            |_______
              ^              ^
              interrupt      driver acks
              fires and      the device,
              keeps firing   line deasserts
              until ack
```

Legacy PCI INTx lines are level-triggered and shared. MSI and MSI-X
messages are effectively edge-like events (one message per signal).
A filter for legacy INTx must check the device's status before
returning `FILTER_HANDLED`, because the line is shared.

## Anti-patterns to avoid

Do not do real work in the filter. The filter is a dispatcher, not a
worker. If you find yourself writing a long sequence of reads and
writes, you belong in an ithread or a taskqueue.

Do not return `FILTER_HANDLED` on a shared legacy line without first
checking that the interrupt came from your device. That wedges the
system.

Do not poll when an interrupt would work. A polling loop that wakes
thousands of times per second on an idle system is a bug, not a
design.

Do not start an ithread and never join it at detach. `bus_teardown_intr`
is non-optional.

## Cross-references

- Appendix C, section **Interrupts**, for the full mental model.
- Chapter 19 for the interrupt discipline in detail.
- Chapter 20 for MSI and MSI-X.
- `intr_event(9)`, `bus_setup_intr(9)`, and the `FILTER_` constants
  in `/usr/src/sys/sys/bus.h`.
