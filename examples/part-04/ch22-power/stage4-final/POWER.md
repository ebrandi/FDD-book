# Power-Management Subsystem

## Purpose

The power layer lets the `myfirst` driver participate correctly in
system suspend, system resume, system shutdown, and optional
per-device runtime power management. It quiesces the device's
activity before a power transition and restores the device's state
after resume, so that a suspend-resume cycle is invisible to the
user space clients of the driver.

## Public API

- `myfirst_power_setup(sc)`: called from attach. Initialises softc
  fields, optionally starts the idle watcher callout.
- `myfirst_power_teardown(sc)`: called from detach. Stops the idle
  watcher, brings the device out of runtime suspend if needed.
- `myfirst_power_suspend(sc)`: kobj DEVICE_SUSPEND implementation.
- `myfirst_power_resume(sc)`: kobj DEVICE_RESUME implementation.
- `myfirst_power_shutdown(sc)`: kobj DEVICE_SHUTDOWN implementation.
- `myfirst_power_runtime_suspend(sc)`, `myfirst_power_runtime_resume(sc)`:
  optional runtime PM; compiled only with `-DMYFIRST_ENABLE_RUNTIME_PM`.
- `myfirst_power_mark_active(sc)`: optional runtime PM; record
  activity to reset the idle timer.
- `myfirst_power_add_sysctls(sc)`: registers counter and policy
  sysctls under `dev.myfirst.N.`.

## State Model

The driver's lifecycle extends as follows:

```
                            +---- runtime_suspend ----+
                            |                         |
   attach -> RUNNING  <--->  RUNTIME_SUSPENDED (D3)
      |          ^                    ^
      |    resume|             suspend|
      v          |                    |
   detach <-- SUSPENDED -------- system_suspend
```

- `RUNNING`: normal operation (D0).
- `SUSPENDED`: system has suspended the device (D3 typically).
- `RUNTIME_SUSPENDED`: driver has suspended the device in response
  to idleness (D3).

The `suspended` flag distinguishes the current state for quick user-
space observation. The `runtime_state` enum tracks runtime PM.

## Transition Flows

### System Suspend (devctl suspend or acpiconf -s 3)

```
1. kernel calls myfirst_pci_suspend(dev)
2. myfirst_pci_suspend calls myfirst_power_suspend(sc)
3. myfirst_quiesce:
   - lock; set suspended=true; unlock
   - mask_interrupts: save mask, write 0xFFFFFFFF
   - drain_dma: abort if in flight, wait on cv
   - drain_workers: drain callout, drain taskqueue
4. PCI layer saves config space and transitions to D3 (if default)
5. method returns 0
```

### System Resume

```
1. PCI layer transitions to D0 and restores config space
2. kernel calls myfirst_pci_resume(dev)
3. myfirst_pci_resume calls myfirst_power_resume(sc)
4. myfirst_restore:
   - pci_enable_busmaster (defensive)
   - write saved_intr_mask back to INTR_MASK register
   - lock; set suspended=false; unlock
5. method returns 0
```

### System Shutdown

```
1. kernel calls myfirst_pci_shutdown(dev)
2. forwards to myfirst_power_shutdown(sc)
3. calls myfirst_quiesce(sc)
4. method returns 0
```

### Runtime Suspend (optional)

```
1. idle watcher callout fires
2. detects idle > idle_threshold_seconds
3. calls myfirst_power_runtime_suspend(sc)
4. quiesce; pci_save_state; pci_set_powerstate(D3)
```

### Runtime Resume (optional)

```
1. activity arrives (DMA test sysctl write)
2. calls myfirst_power_runtime_resume(sc) if suspended
3. pci_set_powerstate(D0); pci_restore_state; myfirst_restore
```

## Counters and Sysctls

- `dev.myfirst.N.suspended`: driver-level suspended flag (0/1).
- `dev.myfirst.N.power_suspend_count`: DEVICE_SUSPEND invocations.
- `dev.myfirst.N.power_resume_count`: DEVICE_RESUME invocations.
- `dev.myfirst.N.power_shutdown_count`: DEVICE_SHUTDOWN invocations.
- `dev.myfirst.N.power_resume_errors`: resume attempts with errors.
- `dev.myfirst.N.idle_threshold_seconds` (runtime PM): policy knob.
- `dev.myfirst.N.runtime_suspend_count` (runtime PM): transitions.
- `dev.myfirst.N.runtime_resume_count` (runtime PM): transitions.
- `dev.myfirst.N.runtime_state` (runtime PM): 0=running, 1=suspended.

## Invariants

- `power_suspend_count == power_resume_count` at all times except
  during an active suspension (when the counters differ by 1).
- `runtime_suspend_count == runtime_resume_count` at steady state
  (when the device is RUNNING).
- `sc->suspended == false` whenever the device is in RUNNING state.
- `sc->dma_in_flight == false` after `myfirst_quiesce` returns.

The regression test `ch22-full-regression.sh` verifies these.

## Locking

- `sc->mtx` protects `suspended`, `saved_intr_mask`, and the runtime
  PM state.
- Counters use `atomic_add_64` so they can be updated from filter
  context without the mutex.
- `cv_timedwait` in `myfirst_drain_dma` holds the mutex while
  sleeping, then releases it.
- `taskqueue_drain` and `callout_drain` must be called without the
  driver mutex held, because the drained work may acquire the same
  mutex.

## Interaction with Other Subsystems

- **DMA (`myfirst_dma.c`)**: suspend calls `myfirst_drain_dma`,
  which reuses the Chapter 21 `dma_cv` and `dma_in_flight` state.
  The DMA tag, map, and buffer remain allocated across suspend.
- **Interrupts (`myfirst_intr.c`, `myfirst_msix.c`)**: suspend masks
  all vectors; resume unmasks via `saved_intr_mask`. Vector
  allocations remain valid across the transition; the PCI layer
  restores MSI/MSI-X configuration automatically.
- **Simulation (`myfirst_sim.c`)**: the simulation's callout is
  drained as part of `myfirst_drain_workers`. The simulation's
  state survives suspend.
- **PCI (`myfirst_pci.c`)**: the method table forwards kobj
  invocations to the power subsystem. Configuration-space save and
  restore is handled by the PCI layer itself.

## Testing

The `labs/` directory contains:

- `ch22-suspend-resume-cycle.sh`: one cycle with counter checks.
- `ch22-suspend-stress.sh`: 100 cycles in a row.
- `ch22-transfer-across-cycle.sh`: transfer + suspend + resume.
- `ch22-runtime-pm.sh`: runtime PM path (requires -DMYFIRST_ENABLE_RUNTIME_PM).
- `ch22-full-regression.sh`: combined regression.

## Known Limitations

- Single global suspend state; no per-subsystem suspend.
- No wake-on-X support (no wake source in the simulation).
- No descriptor-ring state to restore (the driver uses a single
  DMA buffer).
- Runtime PM idle detection is timer-based; a production driver
  might use reference counting or queue-empty signals instead.

These are natural extensions documented in the chapter text.

## See Also

- `device(9)`, `bus(9)`, `pci(9)`.
- `/usr/src/sys/kern/device_if.m` for the kobj method definitions.
- `/usr/src/sys/kern/subr_bus.c` for `bus_generic_suspend` and
  `device_quiesce`.
- `/usr/src/sys/dev/pci/pci.c` for `pci_suspend_child` and
  `pci_resume_child`.
- `/usr/src/sys/dev/re/if_re.c` for a production driver with full
  power management.
- `INTERRUPTS.md`, `MSIX.md`, `DMA.md` for the subsystems the
  power path interacts with.
- The chapter text at `content/chapters/part4/chapter-22.md`.
