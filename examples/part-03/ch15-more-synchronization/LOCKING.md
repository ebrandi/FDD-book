# LOCKING.md — Chapter 15 Stage 4 (`myfirst` v0.9-coordination)

Extension of the Chapter 14 `LOCKING.md`.  Adds Semaphores, Stats
Cache, Coordination, and updated Detach Ordering sections.

## Primitives Owned by the Driver

| Primitive | Kind | Member | Purpose |
|---|---|---|---|
| `sc->mtx` | `struct mtx` (MTX_DEF) | data-path mutex | Protects `sc->cb`, `open_count`, `active_fhs`, counters, state flags. |
| `sc->data_cv` | `struct cv` | on `sc->mtx` | Signals readers when bytes arrive. |
| `sc->room_cv` | `struct cv` | on `sc->mtx` | Signals writers when room appears. |
| `sc->cfg_sx` | `struct sx` | configuration | Protects `sc->cfg`. |
| `sc->stats_cache_sx` | `struct sx` | stats cache | Protects cached bytes_written_10s. |
| `sc->writers_sema` | `struct sema` | writer admission | Caps concurrent writers at `writers_limit`. |
| `sc->heartbeat_co` | `struct callout` | periodic | Heartbeat log. Lock: `sc->mtx`. |
| `sc->watchdog_co` | `struct callout` | periodic | Stall detection. Enqueues `recovery_task` on stall. Lock: `sc->mtx`. |
| `sc->tick_source_co` | `struct callout` | periodic | Synthetic byte injection. Lock: `sc->mtx`. |
| `sc->tq` | `struct taskqueue *` | private | Private taskqueue. Owns every task below. |
| `sc->selwake_task` | `struct task` | plain | Deferred `selwakeup`. |
| `sc->bulk_writer_task` | `struct task` | plain | Fixed-batch writes. |
| `sc->reset_delayed_task` | `struct timeout_task` | scheduled | Delayed reset. |
| `sc->recovery_task` | `struct task` | plain | Stall recovery. Coordinated via `recovery_in_progress`. |
| `sc->is_attached` | atomic int | lifecycle flag | `atomic_load_acq_int` on reads; `atomic_store_rel_int` on writes. |
| `sc->recovery_in_progress` | int under `sc->mtx` | state flag | "At most one recovery" invariant. |

## Semaphores

### `writers_sema`

- Initial value: `writers_limit` (default 4; tunable 1-64).
- Drained by: writers on entry via `myfirst_sync_writer_enter`.
- Posted by: writers on exit via `myfirst_sync_writer_leave`; also
  by the detach path to wake any blocked waiters (they then see
  `is_attached=0` and return ENXIO).
- Not signal-interruptible (uses `cv_wait` internally).  Users
  expecting SIGINT on blocked writes should set a short writer
  timeout or use `O_NONBLOCK`.
- Destroyed in detach after every task is drained and every
  potential waiter has been woken.

### Raising/Lowering the Limit

- Raising posts additional slots immediately.
- Lowering is best-effort: the new lower limit takes effect as new
  writers enter and the counter drains below the new cap.

### Sema Drain Discipline (writers_inflight)

`sc->writers_inflight` is an atomic int that counts threads currently
somewhere between `myfirst_sync_writer_enter` and
`myfirst_sync_writer_leave`.  Detach waits for this counter to reach
zero before calling `sema_destroy`.

Why: `sema_destroy` calls `mtx_destroy` on the internal
`sema_mtx`.  A woken waiter returning from `cv_wait` still holds that
mutex briefly (for `sema_waiters--` and the final unlock).  Racing
`mtx_destroy` against that unlock is a use-after-free.  The drain
counter closes the race.

Pattern:

- `myfirst_sync_writer_enter`:
  1. `atomic_add_int(&writers_inflight, 1)`.
  2. Check `is_attached`; if zero, decrement and return ENXIO.
  3. `sema_wait` (or `sema_trywait` for NDELAY).
  4. Check `is_attached` again after wake; if zero, `sema_post`,
     decrement, return ENXIO.
- `myfirst_sync_writer_leave`:
  1. `sema_post(&writers_sema)`.
  2. `atomic_subtract_int(&writers_inflight, 1)`.
- Detach, in order:
  1. `atomic_store_rel_int(&is_attached, 0)`.
  2. Post `writers_limit` wake-up slots to `writers_sema`.
  3. `while (atomic_load_acq_int(&writers_inflight) > 0) pause("myfwrd", 1);`.
  4. Drain callouts and tasks.
  5. `sema_destroy(&writers_sema)`.

## Stats Cache

### `stats_cache_sx`

- Shared (read) mode: readers of the cached stat.
- Exclusive (write) mode: refreshing the cache.
- Upgrade pattern: `sx_try_upgrade` fast path; `sx_sunlock` +
  `sx_xlock` + re-check fallback; always ends with `sx_downgrade`
  back to shared before final `sx_sunlock`.
- Refresh frequency: at most once per `MYFIRST_CACHE_STALE_HZ * hz`
  ticks (1 second by default).

## Coordination

### Atomic `is_attached`

- Written only once during the life of the softc (0 → 1 on attach,
  1 → 0 on detach or attach failure).
- Read from every context: handlers via
  `myfirst_sync_is_attached`, callback callbacks via the same.
- Memory ordering: `atomic_store_rel_int` on writes ensures
  preceding state changes are visible before the flag becomes 0;
  `atomic_load_acq_int` on reads ensures subsequent reads see
  post-flag state.

### `recovery_in_progress`

- Set by: `myfirst_watchdog` (callout callback, under `sc->mtx`).
- Cleared by: `myfirst_recovery_task` (task callback, under
  `sc->mtx`).
- Invariant: at most one recovery task pending or running at a
  time.
- Accounting: `recovery_task_runs` counts invocations.

## Lock Order

The documented lock order is:

```text
sc->mtx  ->  sc->cfg_sx  ->  sc->stats_cache_sx
```

`WITNESS` enforces this order.  The `writers_sema`'s internal mutex
is not in the graph because the driver never holds `sc->mtx` (or
any other driver lock) across a `sema_wait`/`sema_post`.

## Detach Ordering

1. Refuse detach if `active_fhs > 0` (EBUSY).
2. Under `sc->mtx`: `atomic_store_rel_int(&sc->is_attached, 0)`;
   `cv_broadcast` both cvs.
3. Release `sc->mtx`.
4. Post `writers_limit` slots to `writers_sema` to wake any
   blocked waiters (they return ENXIO).
5. Wait for `writers_inflight == 0`
   (`myfirst_sync_writers_drain`).
6. `callout_drain` the three callouts.
7. `taskqueue_drain` every task: selwake, bulk_writer,
   reset_delayed (timeout variant), recovery.
8. `seldrain` rsel and wsel.
9. `taskqueue_free(sc->tq)`; `sc->tq = NULL`.
10. `sema_destroy(&sc->writers_sema)` (safe: writers_inflight is 0).
11. Destroy cdev and cdev alias.
12. `sysctl_ctx_free`.
13. `cbuf_destroy`, free counters.
14. `sx_destroy(&sc->stats_cache_sx)`.
15. `cv_destroy` twice.
16. `sx_destroy(&sc->cfg_sx)`.
17. `mtx_destroy(&sc->mtx)`.

## Summary of Discipline

- Every primitive has a named wrapper in `myfirst_sync.h`.
- Every mutex-held critical section is short and never includes
  sleepable calls.
- Every cv wait in a syscall context uses the signal-aware variant
  with explicit EINTR/ERESTART handling and a partial-progress
  sentinel.
- Every sema-draining context holds no driver lock during the wait.
- Every destroy happens after every possible user of the primitive
  has been quiesced.
