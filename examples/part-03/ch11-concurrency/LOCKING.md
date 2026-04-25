# myfirst Locking Strategy

Chapter 11 driver, version 0.5-concurrency.

## Overview

The driver uses a single sleep mutex (`sc->mtx`) to serialize all
accesses to the circular buffer, the sleep channel, the selinfo
structures, and several per-softc fields. Byte counters are migrated
to `counter(9)` per-CPU counters in Stage 3 and protected there by
the `counter(9)` API itself.

## What sc->mtx Protects

- `sc->cb` (the circular buffer's internal state: `cb_head`,
  `cb_used`, `cb_data`).
- `sc->open_count`, `sc->active_fhs`.
- `sc->is_attached` (writes; reads at handler entry may be
  unprotected as an optimization, re-checked after every sleep).
- `sc->rsel`, `sc->wsel` (indirectly: `selrecord` is called inside
  a critical section; `selwakeup` is called with the mutex dropped).

## Lock-Free Fields (Stage 3 and later)

- `sc->bytes_read`, `sc->bytes_written`: `counter_u64_t`. Updates
  via `counter_u64_add`; reads via `counter_u64_fetch`.

## Locking Discipline

1. Acquire with `MYFIRST_LOCK(sc)` and release with
   `MYFIRST_UNLOCK(sc)`. These are macros around `mtx_lock` and
   `mtx_unlock` respectively; they can be changed to a different
   lock class without touching call sites.
2. Wait with `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, wmesg, 0)`.
   Wake with `wakeup(&sc->cb)`.
3. NEVER hold `sc->mtx` across `uiomove(9)`, `copyin(9)`, `copyout(9)`,
   `selwakeup(9)`, or `wakeup(9)`. Each of these may sleep or take
   other locks.
4. All `myfirst_buf_*` and `myfirst_wait_*` helpers assert
   `MYFIRST_ASSERT(sc)` at entry (compiles to `mtx_assert(MA_OWNED)`).

## Known Non-Locked Accesses

### `sc->is_attached` (reads at handler entry)

Unprotected plain read. Safe because:
- A stale "true" is re-checked after every sleep via
  `if (!sc->is_attached) return (ENXIO)`.
- A stale "false" causes the handler to return ENXIO early, which
  is also what it would do with a fresh false.

### `sc->open_count`, `sc->active_fhs` (sysctl reads)

Unprotected plain loads. Safe on amd64 and arm64 (aligned 64-bit
loads are atomic). Acceptable on i386 and armv7 because the torn
read, if it ever happened, would produce a single bad statistic
with no correctness impact.

## Lock Order

Currently, the driver holds only `sc->mtx`. There are no lock-order
rules. When Chapter 12 introduces additional locks (`sx(9)` is a
likely candidate), the order will be documented here as:

```
sc->mtx -> sc->other_lock
```

meaning: a thread holding `sc->mtx` may acquire `sc->other_lock`,
but not the reverse.

## Wait Channels

- `&sc->cb`: signals that the buffer state has changed (space or
  data has become available). Used by both read and write paths.

## Rules Summary

1. Never sleep while holding `sc->mtx` except via `mtx_sleep`, which
   atomically drops and reacquires.
2. Never call `uiomove`, `copyin`, `copyout`, `selwakeup`, or
   `wakeup` while holding `sc->mtx`.
3. Every `cbuf_*` call must happen with `sc->mtx` held.
4. The detach path clears `sc->is_attached` under the mutex and
   then wakes any sleepers before destroying the mutex.
5. `counter_u64_add` does not require the mutex.

## History

- 0.5-concurrency (Chapter 11, Stage 2): MYFIRST_LOCK/UNLOCK/ASSERT
  macros; explicit version string.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters migrated to
  counter(9).
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added for every
  invariant on the cbuf and helper paths.
- 0.4-poll-refactor (Chapter 10, Stage 4): wait helpers; locking
  strategy documented inline.
- Earlier versions: see Chapter 10 history.
