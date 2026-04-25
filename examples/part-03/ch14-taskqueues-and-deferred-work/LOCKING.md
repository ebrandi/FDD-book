# LOCKING.md — Chapter 14 Stage 4 (`myfirst` v0.8-taskqueues)

Extension of the Chapter 13 `LOCKING.md`.  Adds a Tasks section and an
updated Detach Ordering section reflecting the Chapter 14 changes.

## Primitives Owned by the Driver

| Primitive | Kind | Member | Purpose |
|---|---|---|---|
| `sc->mtx` | `struct mtx` (MTX_DEF) | data-path mutex | Protects `sc->cb`, `sc->open_count`, `sc->active_fhs`, `sc->is_attached`, task-related counters. |
| `sc->data_cv` | `struct cv` | on `sc->mtx` | Signals readers when bytes arrive. |
| `sc->room_cv` | `struct cv` | on `sc->mtx` | Signals writers when room appears. |
| `sc->cfg_sx` | `struct sx` | configuration lock | Protects `sc->cfg`. |
| `sc->heartbeat_co` | `struct callout` | periodic | Logs stats. Lock: `sc->mtx`. |
| `sc->watchdog_co` | `struct callout` | periodic | Detects stalled drainage. Lock: `sc->mtx`. |
| `sc->tick_source_co` | `struct callout` | periodic | Injects synthetic bytes. Lock: `sc->mtx`. |
| `sc->tq` | `struct taskqueue *` | private | One worker thread at `PWAIT`. Owns every task below. |
| `sc->selwake_task` | `struct task` | plain | Runs `selwakeup(&sc->rsel)` in thread context. |
| `sc->bulk_writer_task` | `struct task` | plain | Writes a fixed batch of bytes per firing. |
| `sc->reset_delayed_task` | `struct timeout_task` | scheduled | Delayed reset in thread context. |

## Tasks

### `selwake_task` (plain)

- Callback: `myfirst_selwake_task`.
- Enqueued from: `myfirst_tick_source` (callout callback) when a byte
  was written.
- Cancelled by: nothing.  Enqueue is unconditional when work is done.
- Drained at detach: yes, after callouts are drained and before
  `seldrain`.
- Locks acquired in callback: `sc->mtx` briefly, only when the
  coalescing counter needs updating.  `selwakeup(&sc->rsel)` is called
  with no driver lock held.

### `bulk_writer_task` (plain)

- Callback: `myfirst_bulk_writer_task`.
- Enqueued from: `myfirst_sysctl_bulk_writer_flood` (the sysctl
  handler, in syscall context).
- Cancelled by: nothing.
- Drained at detach: yes.
- Locks acquired in callback: `sc->mtx` twice: once to read
  `sc->bulk_writer_batch`, once to write to the cbuf and signal
  `data_cv`.  `selwakeup(&sc->rsel)` is called with no lock held.

### `reset_delayed_task` (timeout_task)

- Callback: `myfirst_reset_delayed_task`.
- Armed by: `myfirst_sysctl_reset_delayed` with a non-zero millisecond
  value.
- Cancelled by: `myfirst_sysctl_reset_delayed` with a zero value.
- Drained at detach: yes, via `taskqueue_drain_timeout`.
- Locks acquired in callback: `sc->mtx` and `sc->cfg_sx` in the
  canonical order (mtx first, sx second).
- `cv_broadcast(&sc->room_cv)` is called with no lock held.

## Detach Ordering

The detach sequence is:

1. Refuse detach if `sc->active_fhs > 0` (`EBUSY`).
2. Clear `sc->is_attached` under `sc->mtx`.
3. `cv_broadcast(&sc->data_cv)`; `cv_broadcast(&sc->room_cv)`.
4. Release `sc->mtx`.
5. `callout_drain(&sc->heartbeat_co)`,
   `callout_drain(&sc->watchdog_co)`,
   `callout_drain(&sc->tick_source_co)`.
6. `taskqueue_drain(sc->tq, &sc->selwake_task)`,
   `taskqueue_drain(sc->tq, &sc->bulk_writer_task)`,
   `taskqueue_drain_timeout(sc->tq, &sc->reset_delayed_task)`.
7. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
8. `taskqueue_free(sc->tq); sc->tq = NULL`.
9. Destroy cdev and cdev alias.
10. `sysctl_ctx_free(&sc->sysctl_ctx)`.
11. `cbuf_destroy(&sc->cb)`, free `bytes_read`/`bytes_written`
    counters.
12. `cv_destroy(&sc->data_cv)`, `cv_destroy(&sc->room_cv)`.
13. `sx_destroy(&sc->cfg_sx)`.
14. `mtx_destroy(&sc->mtx)`.

Why the order matters:

- Step 2 prevents callouts from re-arming after they fire.
- Step 5 guarantees no callout callback is in flight, so no new tasks
  can be enqueued from callouts.
- Step 6 guarantees no task callback is running.  Safe to do only
  after Step 5 because callouts are enqueue producers.
- Step 7 guarantees no `selwakeup` is in progress.  Safe to do only
  after Step 6 because tasks are `selwakeup` callers.
- Step 8 frees the taskqueue.  Safe because every task has been
  drained.
- Steps 9-14 unwind the rest of the softc in the reverse order of
  attach.

Violating the order risks use-after-free in task callbacks or
`selwakeup` on a drained selinfo.

## Lock Order

The driver's documented lock order, unchanged from Chapter 13:

```text
sc->mtx  ->  sc->cfg_sx
```

`WITNESS` enforces this order at runtime.  Task callbacks obey it
exactly like any other thread-context code.

The taskqueue's internal `tq_mutex` is not ordered against `sc->mtx`;
taking one does not restrict the other.  A task callback may acquire
`sc->mtx`; a sysctl handler may call `taskqueue_enqueue` while holding
`sc->mtx` (as the `bulk_writer_flood` handler does).  `WITNESS`
accepts both because the two locks are never held in conflicting
orders by the same thread.
