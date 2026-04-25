# myfirst Locking Strategy

Chapter 13 driver, version 0.7-timers.

## Overview

The driver uses three synchronization primitives plus three callouts:
a sleep mutex (`sc->mtx`) for the data path, an sx lock (`sc->cfg_sx`)
for the configuration subsystem, two condition variables
(`sc->data_cv`, `sc->room_cv`) for blocking reads and writes, and
three callouts (`sc->heartbeat_co`, `sc->watchdog_co`,
`sc->tick_source_co`) for periodic work. Byte counters use
`counter(9)` per-CPU counters.

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

Protects:
- `sc->cb` (the circular buffer's internal state)
- `sc->open_count`, `sc->active_fhs`
- `sc->is_attached` (writes; reads at handler entry may be unprotected
  as an optimization, re-checked after every sleep)
- `sc->heartbeat_interval_ms`, `sc->watchdog_interval_ms`,
  `sc->tick_source_interval_ms` (writes via sysctl handlers; reads
  inside callout callbacks)
- `sc->watchdog_last_used` (callout callback only)
- `sc->tick_source_byte` (callout callback only)

### sc->cfg_sx (sx(9))

Protects:
- `sc->cfg.debug_level`
- `sc->cfg.soft_byte_limit`
- `sc->cfg.nickname`

Shared mode: every read of any cfg field.
Exclusive mode: every write of any cfg field.

### sc->data_cv (cv(9))

Wait condition: data is available in the cbuf (`cbuf_used > 0`).
Interlock: `sc->mtx`.
Signalled by: `myfirst_write` and `myfirst_tick_source` after a
  successful cbuf write.
Broadcast by: `myfirst_detach`.
Waiters: `myfirst_read` in `myfirst_wait_data`.

### sc->room_cv (cv(9))

Wait condition: room is available in the cbuf (`cbuf_free > 0`).
Interlock: `sc->mtx`.
Signalled by: `myfirst_read` after a successful cbuf read, and by
  `myfirst_sysctl_reset` after resetting the cbuf.
Broadcast by: `myfirst_detach`.
Waiters: `myfirst_write` in `myfirst_wait_room`.

## Callouts Owned by This Driver

### sc->heartbeat_co (callout(9), MYFIRST_CO_INIT)

Lock: `sc->mtx`.
Callback: `myfirst_heartbeat`.
Behaviour: periodic; re-arms itself at the end of each firing if
  `sc->heartbeat_interval_ms > 0`.
Started by: the heartbeat sysctl handler (transition 0 -> non-zero).
Stopped by: the heartbeat sysctl handler (transition non-zero -> 0)
  via `callout_stop`, and by `myfirst_detach` via `callout_drain`.
Lifetime: initialised in attach via `MYFIRST_CO_INIT`; drained in
  detach via `MYFIRST_CO_DRAIN`.

### sc->watchdog_co (callout(9), MYFIRST_CO_INIT)

Lock: `sc->mtx`.
Callback: `myfirst_watchdog`.
Behaviour: periodic; emits a warning if `cb_used` has not changed
  and is non-zero between firings.
Started/stopped: via the watchdog sysctl handler and detach,
  parallel to the heartbeat.

### sc->tick_source_co (callout(9), MYFIRST_CO_INIT)

Lock: `sc->mtx`.
Callback: `myfirst_tick_source`.
Behaviour: periodic; injects a single byte into the cbuf each firing
  if there is room.
Started/stopped: via the tick_source sysctl handler and detach,
  parallel to the heartbeat.

## Callout Discipline

1. Every callout uses `sc->mtx` as its lock via `callout_init_mtx`
   (wrapped in the `MYFIRST_CO_INIT` macro).
2. Every callout callback asserts `MYFIRST_ASSERT(sc)` at entry.
3. Every callout callback checks `!sc->is_attached` at entry and
   returns early without re-arming.
4. The detach path clears `sc->is_attached` under `sc->mtx`,
   broadcasts both cvs, drops the mutex, and then calls
   `callout_drain` on every callout.
5. `callout_stop` is used to cancel pending callouts in normal
   driver operation (sysctl handlers); `callout_drain` is used at
   detach.
6. NEVER call `selwakeup`, `uiomove`, `copyin`, `copyout`,
   `malloc(M_WAITOK)`, or any sleeping primitive from a callout
   callback. The mutex is held during the callback, and these calls
   would violate the sleep-with-mutex rule. The tick source omits
   `selwakeup` for exactly this reason.

## Lock-Free Fields

- `sc->bytes_read`, `sc->bytes_written`: `counter_u64_t`. Updates
  via `counter_u64_add`; reads via `counter_u64_fetch`.
- `sc->read_timeout_ms`, `sc->write_timeout_ms`: plain ints,
  accessed without locking. Safe because aligned int reads and writes
  are atomic, and the values are advisory.

## Lock Order

```
sc->mtx -> sc->cfg_sx
```

A thread holding `sc->mtx` may acquire `sc->cfg_sx` in either mode.
A thread holding `sc->cfg_sx` may NOT acquire `sc->mtx`.

The callouts run with `sc->mtx` already held by the kernel; their
callbacks must respect this rule when accessing config (which is
fine, since they would acquire `sc->cfg_sx` second).

## Locking Discipline (Recap)

1. Acquire mutex with `MYFIRST_LOCK(sc)`, release with
   `MYFIRST_UNLOCK(sc)`.
2. Acquire sx in shared mode with `MYFIRST_CFG_SLOCK(sc)`, exclusive
   with `MYFIRST_CFG_XLOCK(sc)`. Release with the matching unlock.
3. Wait on a cv with `cv_wait_sig` (interruptible) or
   `cv_timedwait_sig` (interruptible + bounded).
4. Signal a cv with `cv_signal` (one waiter) or `cv_broadcast` (all
   waiters).
5. NEVER hold `sc->mtx` across `uiomove(9)`, `copyin(9)`,
   `copyout(9)`, `selwakeup(9)`, or any potentially-sleeping call.
   `cv_wait_sig` is the exception (it atomically drops the
   interlock).
6. NEVER hold `sc->cfg_sx` across `uiomove(9)`, `sysctl_handle_*`,
   or any other potentially-sleeping call.
7. The detach path clears `sc->is_attached` under `sc->mtx`,
   broadcasts both cvs, drains all callouts, then destroys
   primitives in reverse order.

## History

- 0.7-timers (Chapter 13): added heartbeat, watchdog, and tick-source
  callouts; documented callout discipline; standardised callout
  detach pattern via `MYFIRST_CO_INIT` and `MYFIRST_CO_DRAIN` macros.
- 0.6-sync (Chapter 12, Stage 4): combined version with cv channels,
  bounded reads, sx-protected configuration, reset sysctl.
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added throughout
  cbuf helpers and wait helpers.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters migrated to
  counter(9).
- 0.5-concurrency (Chapter 11, Stage 2): MYFIRST_LOCK/UNLOCK/ASSERT
  macros, explicit locking strategy.
- Earlier versions: see Chapter 10 / Chapter 11 history.
