# myfirst Hardware Interface

## Version

1.0-simulated. Chapter 17 complete.

## Register Block

- Size: 64 bytes (`MYFIRST_REG_SIZE`).
- Access: 32-bit reads and writes on 32-bit-aligned offsets.
- Storage: `malloc(9)`, `M_WAITOK|M_ZERO`, allocated in
  `myfirst_hw_attach`, freed in `myfirst_hw_detach`.
- `bus_space_tag_t`: `X86_BUS_SPACE_MEM` (x86 only, simulation shortcut).
- `bus_space_handle_t`: pointer to the malloc'd block.

## API

All register access goes through:

- `CSR_READ_4(sc, off)`: read a 32-bit register.
- `CSR_WRITE_4(sc, off, val)`: write a 32-bit register.
- `CSR_UPDATE_4(sc, off, clear, set)`: read-modify-write.

The driver's main mutex (`sc->mtx`) must be held for every register
access. Accessor macros assert this via `MYFIRST_ASSERT`.

## Register Map

| Offset | Width  | Name            | Access     | Default        | Notes                                    |
|--------|--------|-----------------|------------|----------------|------------------------------------------|
| 0x00   | 32 bit | CTRL            | R/W        | 0              | Control; GO bit triggers command.        |
| 0x04   | 32 bit | STATUS          | R/W        | 0x1 (READY)    | Status; dynamic Ch17 updates.            |
| 0x08   | 32 bit | DATA_IN         | R/W        | 0              | Command input data.                      |
| 0x0c   | 32 bit | DATA_OUT        | R/W        | 0              | Command output data.                     |
| 0x10   | 32 bit | INTR_MASK       | R/W        | 0              | Interrupt enables.                        |
| 0x14   | 32 bit | INTR_STATUS     | R/W        | 0              | Pending interrupts (RC in real HW).       |
| 0x18   | 32 bit | DEVICE_ID       | RO         | 0x4D594649     | Fixed identifier.                         |
| 0x1c   | 32 bit | FIRMWARE_REV    | RO         | 0x00010000     | Fixed FW rev (major<<16|minor).           |
| 0x20   | 32 bit | SCRATCH_A       | R/W        | 0              | Scratch register.                         |
| 0x24   | 32 bit | SCRATCH_B       | R/W        | 0              | Scratch register.                         |
| 0x28   | 32 bit | SENSOR          | RO (dyn)   | 0              | Oscillating value (Ch17 callout).        |
| 0x2c   | 32 bit | SENSOR_CONFIG   | R/W        | 0x00400064     | Low 16: interval ms; high 16: amplitude. |
| 0x30   | 32 bit | DELAY_MS        | R/W        | 500            | Command completion delay.                |
| 0x34   | 32 bit | FAULT_MASK      | R/W        | 0              | Enabled fault bits.                      |
| 0x38   | 32 bit | FAULT_PROB      | R/W        | 0              | Fault probability 0..10000.              |
| 0x3c   | 32 bit | OP_COUNTER      | RO         | 0              | Count of processed commands.             |

## CTRL register fields

| Bit | Name                    | Purpose                                  |
|-----|-------------------------|------------------------------------------|
| 0   | `MYFIRST_CTRL_ENABLE`   | Device enable.                           |
| 1   | `MYFIRST_CTRL_RESET`    | Write 1 to reset; self-clears.           |
| 4-7 | `MYFIRST_CTRL_MODE`     | Operating mode (0..15).                  |
| 8   | `MYFIRST_CTRL_LOOPBACK` | Loopback mode (challenge).               |
| 9   | `MYFIRST_CTRL_GO`       | Start a command; self-clearing.          |

## STATUS register fields

| Bit | Name                     | Set by                    | Cleared by                |
|-----|--------------------------|---------------------------|---------------------------|
| 0   | `MYFIRST_STATUS_READY`   | `myfirst_hw_attach`       | `myfirst_hw_detach`       |
| 1   | `MYFIRST_STATUS_BUSY`    | `sim_start_command`, `busy_cb` | `command_cb`, recovery |
| 2   | `MYFIRST_STATUS_ERROR`   | `command_cb` (fault path) | driver error handler      |
| 3   | `MYFIRST_STATUS_DATA_AV` | `command_cb`              | driver after DATA_OUT read |

## INTR_MASK / INTR_STATUS fields

| Bit | Name                     | Interrupt source                         |
|-----|--------------------------|------------------------------------------|
| 0   | `MYFIRST_INTR_DATA_AV`   | Data available for read.                 |
| 1   | `MYFIRST_INTR_ERROR`     | Error condition detected.                |
| 2   | `MYFIRST_INTR_COMPLETE`  | Operation complete.                      |

## SENSOR_CONFIG fields

| Bits  | Name      | Purpose                         |
|-------|-----------|---------------------------------|
| 0-15  | interval  | Sensor update interval in ms.    |
| 16-31 | amplitude | Oscillation amplitude (0..65535).|

## FAULT_MASK fields

| Bit | Name                       | Effect                                          |
|-----|----------------------------|-------------------------------------------------|
| 0   | `MYFIRST_FAULT_TIMEOUT`    | Command never completes.                        |
| 1   | `MYFIRST_FAULT_READ_1S`    | `DATA_OUT` returns `0xFFFFFFFF`.                |
| 2   | `MYFIRST_FAULT_ERROR`      | `STATUS.ERROR` set instead of `DATA_AV`.        |
| 3   | `MYFIRST_FAULT_STUCK_BUSY` | `STATUS.BUSY` latched on.                       |

## Common Sequences

### Issuing a command

```
1. Wait for STATUS.BUSY == 0           (driver poll)
2. Write DATA_IN                        (driver)
3. Write CTRL |= GO                     (driver; self-clears)
4. Simulation schedules command_callout after DELAY_MS
5. command_callout sets DATA_OUT, STATUS.DATA_AV, clears BUSY
6. Wait for STATUS.DATA_AV == 1         (driver poll)
7. Read DATA_OUT                        (driver)
8. Clear STATUS.DATA_AV                 (driver)
```

### Reading a sensor sample

```
1. Wait for STATUS.BUSY == 0
2. Write DATA_IN = 0xCAFE (marker)
3. Write CTRL |= GO
4. Wait for STATUS.DATA_AV == 1
5. Read DATA_OUT
6. Push low byte into the ring buffer
```

### Error recovery

```
1. On ETIMEDOUT from wait_for_data:
   a. Increment cmd_data_timeouts
   b. Call myfirst_recover_from_stuck
   c. Return ETIMEDOUT to the caller
2. recover_from_stuck:
   a. Stop sim->command_callout
   b. Clear sim->command_pending
   c. Clear STATUS.BUSY, STATUS.DATA_AV
```

## Observability

| Sysctl                                | Purpose                                    |
|---------------------------------------|--------------------------------------------|
| `dev.myfirst.0.reg_*`                 | Read each register.                        |
| `dev.myfirst.0.reg_ctrl_set`          | Write CTRL.                                |
| `dev.myfirst.0.reg_delay_ms_set`      | Write DELAY_MS.                            |
| `dev.myfirst.0.reg_sensor_config_set` | Write SENSOR_CONFIG.                       |
| `dev.myfirst.0.reg_fault_mask_set`    | Write FAULT_MASK.                          |
| `dev.myfirst.0.reg_fault_prob_set`    | Write FAULT_PROB.                          |
| `dev.myfirst.0.access_log_enabled`    | Toggle access logging.                     |
| `dev.myfirst.0.access_log`            | Dump the access log.                       |
| `dev.myfirst.0.cmd_*`                 | Per-outcome statistics counters.           |
| `dev.myfirst.0.fault_*`               | Fault-injection counters.                  |
| `dev.myfirst.0.sim_running`           | Simulation running flag.                   |

## Simulation Dynamics (Chapter 17)

Registers that are modified autonomously by the simulation:

- `SENSOR`: updated every `SENSOR_CONFIG.interval` ms.
- `STATUS.BUSY`: set by `sim_start_command`; cleared by `command_cb` or
  recovery; re-asserted by `busy_cb` when `FAULT_STUCK_BUSY`.
- `STATUS.DATA_AV`: set by `command_cb`; cleared by driver after reading
  `DATA_OUT`.
- `STATUS.ERROR`: set by `command_cb` under `FAULT_ERROR`; cleared by
  driver.
- `DATA_OUT`: set by `command_cb`.
- `OP_COUNTER`: incremented on each completed command.

All updates happen under `sc->mtx` (which all callouts acquire
automatically via `callout_init_mtx`).

## Architecture Portability

The simulation path uses `X86_BUS_SPACE_MEM` as the tag and a kernel
virtual address as the handle. On non-x86 platforms, `bus_space_tag_t`
is a pointer to a structure and this shortcut does not compile.
Chapter 18 introduces real PCI, which is portable by design.
