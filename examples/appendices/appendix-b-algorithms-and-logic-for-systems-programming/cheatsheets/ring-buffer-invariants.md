# Ring-Buffer Invariants Checklist

An audit checklist for any circular (ring) buffer you write, review, or
debug. Most ring-buffer bugs are violations of an invariant the author
could have named on paper. Walk the code against this list.

## 1. Size and indexing

- [ ] The buffer capacity is a power of two, or the code documents why
      it is not.
- [ ] Indices either (a) wrap at capacity by modulo, or (b) run free and
      are reduced to a slot with `idx & (cap - 1)`. The choice is
      explicit, not mixed.
- [ ] The mask constant matches the capacity (`mask == cap - 1` when
      cap is a power of two).
- [ ] No code path compares a raw index with a masked index.

## 2. Empty and full

- [ ] The empty condition and the full condition are documented at the
      top of the file.
- [ ] If using the **sacrificed-slot** scheme (never use all slots),
      `full == ((head + 1) & mask) == tail`.
- [ ] If using **free-running** indices, `empty == (head == tail)` and
      `full == (head - tail == cap)`.
- [ ] The two are mutually exclusive (never both true at once).

## 3. Memory ordering

- [ ] The producer issues a release-style fence or
      `atomic_store_rel_*` before publishing the new head.
- [ ] The consumer issues an acquire-style fence or
      `atomic_load_acq_*` when reading the published head.
- [ ] The payload write happens *before* the index publication in
      program order, and the barrier enforces that order in machine
      order.
- [ ] For architectures without TSO assumptions, the barriers are
      explicit; there is no hidden dependency on x86 semantics.

## 4. Single vs multiple producers / consumers

- [ ] The ring's producer/consumer cardinality is documented in the
      file's block comment.
- [ ] SPSC (single producer, single consumer) implementations assume
      exactly one thread on each side and do **not** use atomics on the
      index other than for fencing.
- [ ] MPMC or MPSC use `buf_ring(9)` or equivalent, not a hand-rolled
      SPSC pattern.
- [ ] No third thread touches the indices without a lock.

## 5. Enqueue and dequeue

- [ ] Enqueue checks **full** before writing. If full, it returns an
      explicit error (`ENOBUFS`, `EAGAIN`), not a silent drop.
- [ ] Enqueue writes the payload first, then the index.
- [ ] Dequeue checks **empty** before reading. If empty, it returns an
      explicit error or blocks as the contract specifies.
- [ ] Dequeue reads the payload before advancing the tail that frees
      the slot for reuse.
- [ ] No slot is returned to the free pool while another thread could
      still be reading it.

## 6. Notification and wakeup

- [ ] Waking a consumer happens **after** the enqueue is visible, not
      before.
- [ ] The wakeup uses the discipline the consumer expects: `cv_signal`
      in the mutex-and-condvar model, `wakeup_one()` for `msleep`,
      `selwakeup()` for `select`/`poll`, `KNOTE` for `kqueue`.
- [ ] Missed wakeups are handled: the consumer re-checks the queue
      inside the sleep loop (`while`, not `if`).
- [ ] No wakeup happens while holding a spin lock that the sleeper
      needs to reacquire.

## 7. Shutdown and teardown

- [ ] Detach drains the ring before freeing the buffer.
- [ ] Detach quiesces producers before consumers, or vice versa,
      consistently with how the interrupt is stopped.
- [ ] Detach does not free the buffer while a softirq, taskqueue, or
      callout could still touch it.
- [ ] The shutdown sequence is documented in the detach function's
      comment block.

## 8. Back-pressure and policy

- [ ] The driver has a documented policy for the full condition: drop
      newest, drop oldest, block, return error.
- [ ] The policy matches the subsystem's expectations (network drivers
      drop, block-layer drivers usually queue upstream).
- [ ] Statistics for drops or blockages are exposed via `sysctl(9)` or
      a similar interface.
- [ ] The policy is not silently different between the fast path and
      the slow path.

## 9. Visibility and debugging

- [ ] Current head and tail are inspectable (via a sysctl, a debug
      ioctl, or a DB command) without recompiling.
- [ ] The code logs a warning, not a panic, when a drop happens.
- [ ] The invariant checks exist as `KASSERT` statements in DEBUG
      builds, but not as runtime costs in production.

## 10. Documentation

- [ ] The file comment names the ring's producer and consumer.
- [ ] The file comment names the empty and full conditions.
- [ ] The file comment names the memory-ordering discipline.
- [ ] The file comment cites the chapter or appendix it was derived
      from.

## Cross-references

- Appendix B, section **Circular and Ring Buffers**.
- Chapter 10, for producer/consumer and interrupt context.
- Chapter 11, for `atomic(9)` and fences.
- `buf_ring(9)` for the generic kernel ring.
