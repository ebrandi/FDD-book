# myfirst Simulation Interface

## Version

1.0-simulated. Chapter 17 complete.

## What the Simulation Is

A kernel-memory-backed simulated device that mimics a minimal
command-response hardware protocol, used to teach driver development
without requiring real hardware. The simulation lives in
`myfirst_sim.c` and is driven by three callouts:

- `sensor_callout`: periodic, updates `SENSOR` every configurable interval.
- `command_callout`: one-shot, fires after `DELAY_MS` to complete a command.
- `busy_callout`: periodic, re-asserts `STATUS.BUSY` when `FAULT_STUCK_BUSY` is set.

## What the Simulation Does

- Produces an oscillating sensor value autonomously.
- Accepts commands via `CTRL.GO` and completes them after a delay.
- Implements read-to-clear-style semantics when the fault framework enables them.
- Injects four distinct fault modes under controllable probability.
- Tracks every operation in a per-device counter.

## What the Simulation Does Not Do

- No DMA. No interrupts. No PCI configuration space.
- No real-silicon timing precision (limited to the kernel's timer resolution).
- No sub-microsecond delays.
- No coalescing of commands (one at a time).
- No queued commands (overlapping writes are rejected).

## Callout Map

| Callout            | Interval             | Purpose                               |
|--------------------|----------------------|---------------------------------------|
| `sensor_callout`   | `SENSOR_CONFIG`.interval ms (default 100) | Updates `SENSOR`. |
| `command_callout`  | `DELAY_MS` (default 500)  | Completes a single command.     |
| `busy_callout`     | 50 ms                     | Re-asserts `STATUS.BUSY` under `FAULT_STUCK_BUSY`. |

All three callouts are initialised with `callout_init_mtx(&co, &sc->mtx, 0)`.
Every callback runs with `sc->mtx` held.

## Fault Modes

| Bit | Name                       | Effect                                          |
|-----|----------------------------|-------------------------------------------------|
| 0   | `MYFIRST_FAULT_TIMEOUT`    | Command never completes; driver times out.       |
| 1   | `MYFIRST_FAULT_READ_1S`    | `DATA_OUT` returns `0xFFFFFFFF`.                |
| 2   | `MYFIRST_FAULT_ERROR`      | `STATUS.ERROR` set instead of `DATA_AV`.        |
| 3   | `MYFIRST_FAULT_STUCK_BUSY` | `STATUS.BUSY` latched on by the busy callout.   |

Multiple bits may be set; each operation independently consults
`should_fault()` which uses `arc4random_uniform(10000)` against
`FAULT_PROB`.

## Sysctl Reference (simulation-added)

| Sysctl                                    | Type | Default | Purpose                          |
|-------------------------------------------|------|---------|----------------------------------|
| `dev.myfirst.0.sim_running`               | RO   | true    | Simulation callouts active.       |
| `dev.myfirst.0.sim_sensor_baseline`       | RW   | 0x1000  | Sensor oscillation baseline.      |
| `dev.myfirst.0.sim_op_counter_mirror`     | RO   | 0       | Mirror of `OP_COUNTER`.           |
| `dev.myfirst.0.reg_delay_ms_set`          | RW   | 500     | Writable `DELAY_MS`.              |
| `dev.myfirst.0.reg_sensor_config_set`     | RW   | 0x00400064 | Writable `SENSOR_CONFIG`.       |
| `dev.myfirst.0.reg_fault_mask_set`        | RW   | 0       | Writable `FAULT_MASK`.            |
| `dev.myfirst.0.reg_fault_prob_set`        | RW   | 0       | Writable `FAULT_PROB`.            |
| `dev.myfirst.0.cmd_timeout_ms`            | RW   | 2000    | Command completion timeout.       |
| `dev.myfirst.0.rdy_timeout_ms`            | RW   | 100     | Device-ready polling timeout.     |
| `dev.myfirst.0.cmd_successes`             | RO   | 0       | Successful commands.              |
| `dev.myfirst.0.cmd_rdy_timeouts`          | RO   | 0       | Ready-wait timeouts.              |
| `dev.myfirst.0.cmd_data_timeouts`         | RO   | 0       | Data-wait timeouts.               |
| `dev.myfirst.0.cmd_errors`                | RO   | 0       | Commands with `STATUS.ERROR`.     |
| `dev.myfirst.0.cmd_recoveries`            | RO   | 0       | Recovery invocations.             |
| `dev.myfirst.0.fault_injected`            | RO   | 0       | Total faults injected.            |

## Development Guidance

When adding a new simulation behaviour:

1. Decide whether the behaviour needs a new callout or can fit in an
   existing one.
2. If new, follow the six-step template in the Chapter 17 "Anatomy of
   a Simulation Callout" reference.
3. Document the callout in the Callout Map table above.
4. If the behaviour exposes a new register, extend the register map in
   `myfirst_hw.h` and document it in `HARDWARE.md`.
5. If the behaviour introduces a new fault mode, add a new bit to
   `FAULT_MASK` definitions, implement the check in the appropriate
   callback, and document it in the Fault Modes table above.
6. Add a sysctl where appropriate.
7. Add a test script under `examples/part-04/ch17-simulating-hardware/labs/`.

## Relationship to Real Hardware

The simulation is deliberately designed to match patterns in real
FreeBSD drivers:

- `STATUS.READY` clears while busy; sets when ready. Common.
- `STATUS.DATA_AV` latches until read; driver clears. Common.
- `STATUS.ERROR` is sticky until cleared. Common.
- `CTRL.GO` is self-clearing. Common (called "start" or "trigger" on real devices).
- `INTR_STATUS` is RW in Chapter 17; many real devices make it RC or W1C.

When Chapter 18 introduces real PCI hardware, the simulation is
retired and the driver's accessors point at the real device's BAR.
The driver's high-level logic does not change.
