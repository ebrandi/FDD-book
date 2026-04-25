# Debug and Tracing Subsystem for the myfirst Driver

This document describes the debug and tracing infrastructure introduced
in Chapter 23 of *FreeBSD Device Drivers: From First Steps to Kernel
Mastery*. The subsystem covers both human-readable logging (via the
`DPRINTF` macro) and machine-readable events (via SDT probes).

## Runtime Verbosity: `DPRINTF`

The driver carries a `sc_debug` field in its softc, a 32-bit bitmask.
Each bit enables a category of log output. The operator sets the mask
at runtime:

```sh
sysctl dev.myfirst.0.debug.mask=0xFF
```

Messages appear in `dmesg` prefixed by the device name, for example:

```
myfirst0: open: pid=1234 uid=1001 flags=0x0
```

### Class Bits

| Bit          | Name           | Covers                                    |
|--------------|----------------|-------------------------------------------|
| `0x00000001` | `MYF_DBG_INIT` | Probe, attach, detach lifecycle           |
| `0x00000002` | `MYF_DBG_OPEN` | open/close calls                          |
| `0x00000004` | `MYF_DBG_IO`   | read/write paths                          |
| `0x00000008` | `MYF_DBG_IOCTL`| ioctl handling                            |
| `0x00000010` | `MYF_DBG_INTR` | Interrupt handler                         |
| `0x00000020` | `MYF_DBG_DMA`  | DMA mapping and sync                      |
| `0x00000040` | `MYF_DBG_PWR`  | Power-management events                   |
| `0x00000080` | `MYF_DBG_MEM`  | malloc/free trace                         |

### Useful Combinations

| Command                                         | Effect                           |
|-------------------------------------------------|----------------------------------|
| `sysctl dev.myfirst.0.debug.mask=0x0`           | Disable all debug output.        |
| `sysctl dev.myfirst.0.debug.mask=0x3`           | Init + open/close lifecycle.     |
| `sysctl dev.myfirst.0.debug.mask=0xF`           | Init + open/close + I/O + ioctl. |
| `sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF`    | All classes.                     |

### Cost When Disabled

A `DPRINTF` call with its bit clear costs one load and one branch,
which compilers typically execute in a single cycle. The cost is
effectively zero in production.

### Cost When Enabled

A `DPRINTF` with its bit set is equivalent to a normal `device_printf`
call, which itself is not free: writing to the kernel message buffer
involves a lock and a format operation. The operator should set debug
bits only for the categories they are actively investigating, not as a
long-term production setting.

## Static Tracing: SDT Probes

The driver declares three SDT probes:

| Probe                    | Fires on               | Arguments                                     |
|--------------------------|------------------------|-----------------------------------------------|
| `myfirst:::open`         | Every device open      | arg0 = softc*, arg1 = flags                   |
| `myfirst:::close`        | Every device close     | arg0 = softc*, arg1 = flags                   |
| `myfirst:::io`           | Every read or write    | arg0 = softc*, arg1 = is_write, arg2 = resid, arg3 = off |

When no DTrace script has attached to a probe, the probe is a no-op,
and the runtime cost is negligible.

### Listing the Probes

```sh
sudo dtrace -l -P myfirst
```

### Example One-Liners

```sh
# Count every event, grouped by probe name, for 10 seconds
sudo dtrace -n 'myfirst::: { @[probename] = count(); }
                tick-10s { exit(0); }'

# Show each open with the pid and flags
sudo dtrace -n 'myfirst:::open { printf("open pid=%d flags=0x%x",
                                        pid, arg1); }'

# Aggregate I/O sizes
sudo dtrace -n 'myfirst:::io { @ = quantize(arg2); }'

# Measure latency between open and close per pid
sudo dtrace -n '
myfirst:::open { self->t = timestamp; }
myfirst:::close /self->t/ {
    @[pid] = quantize(timestamp - self->t); self->t = 0;
}'
```

## Combined Debug Workflow

The two systems complement one another. A typical debug session:

1. Set the debug mask to a broad value to see events in `dmesg`:
   ```sh
   sudo sysctl dev.myfirst.0.debug.mask=0x7
   ```
2. Reproduce the problem.
3. Inspect `dmesg | tail -200` to see the event flow.
4. Attach a DTrace script to quantify frequencies or timings.
5. Disable the debug mask when the diagnosis is complete:
   ```sh
   sudo sysctl dev.myfirst.0.debug.mask=0
   ```

## Extending the Subsystem

To add a new class, edit `myfirst_debug.h`:

1. Define the new constant, using the next unused bit.
2. Update this document's table.
3. Use the new class in `DPRINTF` calls throughout the driver.

To add a new SDT probe:

1. Declare the probe in `myfirst_debug.h` with `SDT_PROBE_DECLARE`.
2. Define the probe in `myfirst_debug.c` with `SDT_PROBE_DEFINEn`.
3. Fire the probe in the driver source with `SDT_PROBEn(...)`.

Both extensions are additive; existing users of the driver see no
difference until they choose to attach scripts or enable mask bits.

## Version

This infrastructure was introduced in the `1.6-debug` release of the
driver.
