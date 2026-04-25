# myfirst Locking Strategy

Chapter 12 driver, version 0.6-sync.

## Overview

The driver uses three synchronization primitives: a sleep mutex
(`sc->mtx`) for the data path, an sx lock (`sc->cfg_sx`) for the
configuration subsystem, and two condition variables (`sc->data_cv`,
`sc->room_cv`) for blocking reads and writes. Byte counters use
`counter(9)` per-CPU counters and protect themselves.

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

Protects:
- `sc->cb` (the circular buffer's internal state)
- `sc->open_count`, `sc->active_fhs`
- `sc->is_attached` (writes; reads at handler entry may be unprotected
  as an optimization, re-checked after every sleep)

(Note: the timeout fields `sc->read_timeout_ms` and `sc->write_timeout_ms`
are *not* protected by `sc->mtx`. They are plain ints, written by the
sysctl framework via direct `CTLFLAG_RW` binding and read by the wait
helpers without locking. See "Known Non-Locked Accesses" below.)

### sc->cfg_sx (sx(9))

Protects:
- `sc->cfg.debug_level`
- `sc->cfg.soft_byte_limit`
- `sc->cfg.nickname`

Shared mode: every read of any cfg field.
Exclusive mode: every write of any cfg field, plus the reset sysctl
in Stage 4.

### sc->data_cv (cv(9))

Wait condition: data is available in the cbuf (`cbuf_used > 0`).
Interlock: `sc->mtx`.
Signalled by: `myfirst_write` after a successful cbuf write.
Broadcast by: `myfirst_detach`.
Waiters: `myfirst_read` in `myfirst_wait_data`.

### sc->room_cv (cv(9))

Wait condition: room is available in the cbuf (`cbuf_free > 0`).
Interlock: `sc->mtx`.
Signalled by: `myfirst_read` after a successful cbuf read, and by
`myfirst_sysctl_reset` after resetting the cbuf.
Broadcast by: `myfirst_detach`.
Waiters: `myfirst_write` in `myfirst_wait_room`.

## Lock-Free Fields

- `sc->bytes_read`, `sc->bytes_written`: `counter_u64_t`. Updates via
  `counter_u64_add`; reads via `counter_u64_fetch`.

## Lock Order

```
sc->mtx -> sc->cfg_sx
```

A thread holding `sc->mtx` may acquire `sc->cfg_sx` in either mode.
A thread holding `sc->cfg_sx` may NOT acquire `sc->mtx`.

Rationale: the data path always holds `sc->mtx` and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need the data mutex; the only path that
holds both is `myfirst_sysctl_reset`, which acquires them in the
canonical order.

## Locking Discipline

1. Acquire mutex with `MYFIRST_LOCK(sc)`, release with
   `MYFIRST_UNLOCK(sc)`.
2. Acquire sx in shared mode with `MYFIRST_CFG_SLOCK(sc)`, exclusive
   with `MYFIRST_CFG_XLOCK(sc)`. Release with the matching unlock.
3. Wait on a cv with `cv_wait_sig` (interruptible) or
   `cv_timedwait_sig` (interruptible + bounded).
4. Signal a cv with `cv_signal` (one waiter) or `cv_broadcast` (all
   waiters). Use `cv_broadcast` only for state changes that affect
   all waiters (detach, configuration reset).
5. NEVER hold `sc->mtx` across `uiomove(9)`, `copyin(9)`, `copyout(9)`,
   `selwakeup(9)`, or any potentially-sleeping call. `cv_wait_sig` is
   the exception (it atomically drops the interlock).
6. NEVER hold `sc->cfg_sx` across `uiomove(9)`, `sysctl_handle_*`, or
   any other potentially-sleeping call.
7. All `cbuf_*` calls must happen with `sc->mtx` held (the helpers
   assert MA_OWNED).
8. The detach path clears `sc->is_attached` under `sc->mtx`,
   broadcasts both cvs, and refuses detach while `active_fhs > 0`.

## Snapshot-and-Apply Pattern

When a path needs both `sc->mtx` and `sc->cfg_sx`, prefer the
snapshot-and-apply pattern over holding both at once:

  1. `sx_slock(&sc->cfg_sx)`; read cfg fields into local variables;
     `sx_sunlock(&sc->cfg_sx)`.
  2. `MYFIRST_LOCK(sc)`; do cbuf operations using the snapshot;
     `MYFIRST_UNLOCK(sc)`.

The snapshot may be slightly stale by the time it is used. For
configuration values that are advisory (debug level, soft byte
limit), this is acceptable.

The reset sysctl in Stage 4 is the exception: it holds both locks
together because it must atomically clear the buffer, the counters,
and the debug level. The acquisition order is mtx first, sx second;
the release order is the reverse.

## Wait Channels

- `sc->data_cv`: data has become available.
- `sc->room_cv`: room has become available.

The legacy `&sc->cb` wakeup channel from Chapter 10 has been retired.

## Known Non-Locked Accesses

### `sc->is_attached` at handler entry

Unprotected plain read. Safe because:
- A stale "true" is re-checked after every sleep via
  `if (!sc->is_attached) return (ENXIO)`.
- A stale "false" causes the handler to return ENXIO early, which is
  also what it would do with a fresh false.

### `sc->open_count`, `sc->active_fhs` at sysctl read time

Unprotected plain loads. Safe on amd64 and arm64 (aligned 64-bit
loads are atomic). Acceptable on i386 because the torn read, if it
ever happened, would produce a single bad statistic with no
correctness impact.

### `sc->read_timeout_ms`, `sc->write_timeout_ms`

The wait helpers read these without taking any lock. The sysctl
framework writes them as plain integer stores. Tearing is impossible
on aligned ints on every architecture FreeBSD supports. A stale read
results only in a slightly different timeout for the next wait, which
is acceptable.

## History

- 0.6-sync (Chapter 12, Stage 4): combined version with cv channels,
  bounded reads, sx-protected configuration, and reset sysctl.
- 0.6-sx-config (Stage 3): sx for configuration subsystem; debug,
  soft_byte_limit, nickname sysctls.
- 0.6-bounded-read (Stage 2): cv_timedwait_sig with read_timeout_ms
  and write_timeout_ms sysctls.
- 0.6-cv-channels (Stage 1): cv replaces anonymous wakeup channels.
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters via counter(9).
- 0.5-concurrency (Chapter 11, Stage 2): macros, locking strategy.
- Earlier versions: see Chapter 10 and Chapter 11 history.
