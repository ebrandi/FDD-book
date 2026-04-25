# Concurrency-Pattern Decision Aid

A decision aid for picking the right concurrency pattern. It does not
replace the chapters on locking and synchronisation; it helps you frame
the problem before you open them.

## Step 1: Identify the shape of the problem

Answer these questions in order. The first "yes" usually picks your
pattern.

- **Is this a short critical section, no sleeping?**
  -> `mtx(9)` with `MTX_DEF`.

- **Is this inside a filter interrupt handler (`INTR_FILTER`)?**
  -> `mtx(9)` with `MTX_SPIN`.

- **Is there one thread that produces, one that consumes, with
  blocking on empty or full?**
  -> mutex plus condition variable (`cv(9)`), or a ring plus a
  wakeup primitive.

- **Does the workload have many readers and an occasional writer, and
  readers may sleep (call `malloc`, copyin, etc.) while holding the
  lock?**
  -> `sx(9)`.

- **Does the workload have many readers and a rare writer, and readers
  must not sleep?**
  -> `rmlock(9)`.

- **Is the object owned by several parties and you do not know which
  one will release it last?**
  -> `refcount(9)`.

- **Is the resource a pool of N identical slots?**
  -> `sema(9)`.

- **Is the state a single word (flag, counter) and every access is
  trivial?**
  -> `atomic(9)`.

If two answers both apply, the higher-level pattern usually wins. A
reference-counted object still uses a mutex to serialise field
updates; a `cv` is built on top of a mutex.

## Step 2: Check the constraints

Some patterns exclude others. Use this table to catch conflicts early.

| If you are in...            | You cannot...                          |
| :-------------------------- | :------------------------------------- |
| A filter interrupt          | Sleep, acquire a sleepable lock        |
| Holding a spin mutex        | Sleep, acquire `sx`, `rmlock`, sema    |
| Holding a sleepable mutex   | Acquire a spin mutex out of order      |
| Holding any lock            | Call an unrelated user-supplied cb     |
| A callout or taskqueue item | Assume caller's lock is still held     |

## Step 3: Producer-consumer variants

If you answered "producer/consumer" in Step 1, pick the variant.

| Variant                                  | Primitive                     |
| :--------------------------------------- | :---------------------------- |
| One producer, one consumer, fixed-size   | SPSC ring, no lock            |
| One producer, many consumers             | `buf_ring(9)` in MPSC mode    |
| Many producers, one consumer             | `buf_ring(9)` in MPMC mode    |
| Blocking consumer, non-interrupt producer| mutex + `cv(9)`               |
| Interrupt producer, thread consumer      | SPSC ring + wakeup            |
| Select/poll/kqueue exposure              | Ring + `selrecord`/`KNOTE`    |

## Step 4: Shared ownership variants

If you answered "reference count" in Step 1, confirm the model.

- [ ] The object is allocated once and may be destroyed multiple
      places.
- [ ] Every acquire has a paired release.
- [ ] The release function frees the object when the count drops to
      zero.
- [ ] The initial count is 1 on allocation; the creator holds the
      first reference.
- [ ] No code loads, inspects, then acquires. Code uses
      `refcount_acquire_if_gt()` when the acquire is conditional.
- [ ] Reads of the count for logging or statistics use
      `refcount_load()`, not a raw load.

## Step 5: Read-mostly variants

If readers dominate, decide between `sx` and `rmlock`.

- `sx(9)` if readers may sleep, writers are not extremely rare, or
  the lock is not in the hottest path.
- `rmlock(9)` if readers must not sleep, writers are rare, and the
  hot path is on the read side.

Do not use `rmlock(9)` as a default read-mostly lock. Its writer
cost is higher than `sx(9)`'s, and the contract forbids sleeping in
readers. Use it when the profile actually shows it is warranted.

## Step 6: Validate the choice

Once you have picked a pattern, sanity-check it against these
questions.

- [ ] Every code path that reaches the shared state acquires the
      lock I picked.
- [ ] The lock discipline is documented at the top of the file (which
      lock protects which fields).
- [ ] The locking order with other locks in the driver is documented
      and consistent.
- [ ] Holding the lock does not cause a call into code that expects
      not to be called under a lock of that type.
- [ ] Tests or runtime checks exist (WITNESS, INVARIANTS) to catch
      violations during development.

## Common mistakes

- Using `mtx(9)` when a sleepable lock is needed because a path under
  the lock calls `malloc(M_WAITOK)` or `copyin`.
- Using `sx(9)` when a spin mutex would suffice, adding needless
  context switches.
- Using `rmlock(9)` by default because "reads dominate"; overpaying
  on the write side without measurement.
- Holding a lock across a wakeup, or releasing it and then expecting
  a wakeup to reach the waiter (race window).
- Mixing `refcount_acquire` and raw pointer copies; only one lineage
  of references is tracked.
- Using `atomic(9)` ops on several fields that together are a
  logical invariant; you need a lock, not atomics.

## Cross-references

- Appendix B, section **Concurrency Patterns**.
- Chapter 11, for `atomic(9)`, `mtx(9)`, `MTX_SPIN` vs `MTX_DEF`.
- Chapter 12, for `sx(9)`, `cv(9)`, and blocking primitives.
- `locking(9)` for the kernel locking overview.
- `refcount(9)`, `rmlock(9)`, `sx(9)`, `sema(9)` for the specifics.
