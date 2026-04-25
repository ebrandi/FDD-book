# Error-Unwinding Checklist

A line-by-line audit list for kernel functions that acquire multiple
resources. Use it when writing a new `attach` routine, reviewing
someone else's probe or ioctl handler, or debugging a leak report.

## When this checklist applies

The checklist applies to any function that:

- acquires two or more resources (locks, memory, bus resources, device
  handles, taskqueues, callouts, interrupts),
- can fail partway through,
- must leave the system in its original state on failure.

The canonical example is `attach`, but the same pattern applies to
`ioctl` handlers, probe-time setup, and any helper that sets up a
non-trivial subsystem.

## The pattern at a glance

```
int
foo_attach(device_t dev)
{
        struct foo_softc *sc = device_get_softc(dev);
        int err;

        err = bus_alloc_resource_any(...);
        if (err != 0)
                goto fail;

        err = bus_setup_intr(...);
        if (err != 0)
                goto fail_res;

        err = make_dev_s(...);
        if (err != 0)
                goto fail_intr;

        return (0);

fail_intr:
        bus_teardown_intr(...);
fail_res:
        bus_release_resource(...);
fail:
        return (err);
}
```

The invariant: labels appear in **reverse order** of acquisition. Each
label releases one resource and falls through to the next.

## The checklist

### A. Acquisition order

- [ ] Every resource acquisition is documented by a comment or a
      `KASSERT` that names the resource.
- [ ] No two resources are acquired in a single statement that can
      fail halfway.
- [ ] Each acquisition either succeeds completely or leaves no state
      behind.

### B. Label naming

- [ ] Labels are named for the resource they undo (`fail_intr`,
      `fail_res`, `fail_mem`), not for line numbers (`fail1`,
      `fail2`).
- [ ] Labels are unique within the function.
- [ ] Labels appear in reverse order of acquisition.
- [ ] The last label before the shared return (`fail`, `out`) is used
      for "release nothing, just return".

### C. Ordering of release

- [ ] Each label releases exactly the resource its name implies.
- [ ] Fall-through from one label to the next is intentional and
      required.
- [ ] No label jumps over another label's release.
- [ ] The final return is on the success path, before the label
      block.

### D. Nullability and state

- [ ] A resource acquired into a pointer is either `NULL`-initialised
      or acquired unconditionally before the first `goto` that could
      reach its release.
- [ ] A release that can tolerate `NULL` (e.g., `free()`,
      `bus_release_resource()` on a saved pointer) is used safely.
- [ ] A release that cannot tolerate `NULL` is protected by an
      `if (p != NULL)` check, or by careful ordering.

### E. Error code

- [ ] `err` (or the equivalent variable) holds a meaningful error
      code at every `goto` site.
- [ ] The return value matches a `/usr/src/sys/sys/errno.h` constant
      (not a negative number or a bespoke code).
- [ ] No path reaches the return statement with `err == 0` on the
      failure side.

### F. Locking during unwind

- [ ] If a lock is held at the time of `goto`, each label that could
      be reached while the lock is held releases it correctly.
- [ ] Locks are not double-released.
- [ ] Locks are not required to be held during a specific release; if
      they are, the ordering reflects that.

### G. Roll-back of partial success

- [ ] Any mid-function state change that becomes visible to other
      threads (publishing a pointer, inserting into a list, starting
      an interrupt) has an explicit reversal on the unwind path.
- [ ] The reversal is in the correct label, not scattered.
- [ ] If the reversal itself can fail, the function's contract on
      that case is documented.

### H. Diagnostics

- [ ] Each failure path logs via `device_printf(9)`, `log(9)`, or a
      dedicated `counter_u64_t`, enough to diagnose the failure
      without reproducing.
- [ ] The log message names the resource and the error code.
- [ ] Logs on the unwind path do not hide the original error.

### I. Detach matches attach

- [ ] The corresponding `detach` releases the same resources in the
      same reverse order as the last `fail_*` label in `attach`.
- [ ] A successful `attach` followed by a `detach` leaves no leaked
      resource.
- [ ] Detach is idempotent with respect to interrupted attach: if
      `attach` failed partway, `detach` is not called by the bus.

### J. Style

- [ ] The function fits on one screen, or has been broken up because
      it does not.
- [ ] The labels are separated from the success path by a visible
      blank line.
- [ ] The function follows `style(9)`: four-space indent, labels at
      column one, no tabs-after-code.

## Common mistakes this catches

- Forgetting to release a resource because it was acquired in a
  conditional branch that falls through to the label.
- Releasing in alphabetical order instead of acquisition order,
  triggering a use-after-free when one teardown depends on another.
- Returning `0` on a failure path because `err` was overwritten by a
  successful intermediate call.
- Holding a mutex across a release that itself can sleep.
- Detaching resources that were never attached when probe failed very
  early.

## Cross-references

- Appendix B, section **Error-Handling Patterns**.
- Chapter 5, for the teaching introduction to `goto` in kernel C.
- `style(9)` for formatting conventions.
- `errno(2)` for the standard error constants.
