# Annotated Notes on the GDB Stub Workflow

## The Moving Parts

- **KDB**: the in-kernel debugger framework. It selects a
  backend and invokes it.
- **DDB**: the local, in-kernel debugger backend. Uses the
  system console directly.
- **GDB (stub)**: a remote backend. Speaks the GDB remote
  serial protocol over a serial line.
- **kgdb**: the host-side debugger. On a dump, it reads
  `/var/crash/vmcore.last`. On a live remote session, it
  connects to the stub.

## Key Sysctls

- `debug.kdb.supported_backends` - read-only list of backends
  compiled in.
- `debug.kdb.current_backend` - writable; selects which backend
  handles the next debugger entry.
- `debug.kdb.enter` - write `1` to force entry.
- `debug.kdb.panic_str` - triggers a test panic (when the
  kernel has `INVARIANTS`).

## Serial Transport

The GDB stub requires a serial line the host can open. Options:

- bhyve: `-l com1,stdio` or `-l com1,/dev/nmdmXA` with
  `/dev/nmdmXB` on the host.
- QEMU: `-serial stdio`, `-serial pty`, or
  `-serial unix:/tmp/serial.sock,server,nowait`.
- Real hardware: a dedicated serial port.

## Attach Sequence

1. VM enters KDB via `debug.kdb.enter=1` or panic.
2. KDB switches to the GDB backend (if selected).
3. Stub waits for the `$` GDB packet.
4. Host `kgdb` opens the serial device, sends the handshake.
5. Stub responds with register values; `kgdb` takes control.

## Detach Sequence

1. In `kgdb`: `detach`, then `quit`.
2. Stub sees the detach, returns control to the kernel.
3. Kernel resumes at the instruction after the entry point.

A hard close (killing `kgdb` without `detach`) leaves the stub
expecting more packets. The kernel stays blocked. Usually only
a reboot recovers.

## When to Use the Stub vs. DDB

| Situation                          | Prefer |
|-----------------------------------:|:-------|
| Quick look on a local console      | DDB    |
| Deep debugging with source-level view | GDB stub |
| No serial port, dev box only        | DDB    |
| Intermittent bug, need live inspection | GDB stub |
| Post-mortem only                    | kgdb on vmcore |
| Watchpoints on specific addresses   | GDB stub |

## When the Stub Is the Wrong Tool

- Production servers: halts all work.
- Time-sensitive code under test: the halt itself is an
  observation effect.
- Hypervisor-managed migrations: a long halt can look like a
  dead node and be fenced.
- SMP hangs: only the CPU that entered the debugger is
  stopped; other CPUs may still be running, which can confuse
  the picture.
