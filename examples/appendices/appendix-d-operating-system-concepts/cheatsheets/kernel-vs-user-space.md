# Kernel vs User Space Cheatsheet

A one-page reminder of the boundary that shapes every rule in driver
code. Read it before touching any pointer that came from userland.

## The Two Sides

```
+---------------------------------------------+---------------------------+
|             Kernel Space                    |       User Space          |
+---------------------------------------------+---------------------------+
| Full CPU privilege                          | Reduced privilege         |
| One shared address space                    | One per process, isolated |
| Memory stable while allocated               | May page, swap, unmap     |
| Direct hardware access                      | None                      |
| Bug blast radius: whole machine             | Bug blast radius: one proc|
| Entered from userland via a syscall trap    | Entered from kernel on    |
|                                             | return from syscall       |
+---------------------------------------------+---------------------------+
```

## The Rule of the Door

A user pointer is never dereferenced directly from the kernel.
Always use a copy primitive that catches a bad user address and
returns `EFAULT` instead of panicking.

```
user side                       kernel side
---------                       -----------
char buf[N]    -- write(2) -->  struct uio  -- uiomove() --> kernel buffer
                <- read(2) --   struct uio  <- uiomove() <-- kernel buffer

void *ptr      -- ioctl(2) -->  arg cookie  -- copyin() --> kernel struct
                <- ioctl(2) -- arg cookie  <- copyout() <- kernel struct
```

## Copy Primitive Cheat Lines

| You have...                               | Use                         |
| :---------------------------------------- | :-------------------------- |
| A fixed-size struct coming in from user   | `copyin(9)`                 |
| A fixed-size struct going out to user     | `copyout(9)`                |
| A NUL-terminated string from user         | `copyinstr(9)`              |
| A buffer list described by `struct uio`   | `uiomove(9)`                |
| A single word from user                   | `fueword(9)` / `suword(9)`  |

## Red-Flag Checklist

Before you press commit on a driver function that received a user
pointer or structure, tick all of these.

- [ ] The user pointer is never dereferenced directly in this function.
- [ ] Every copy primitive has its return value checked and propagated.
- [ ] `EFAULT` is returned, not ignored, on a bad user address.
- [ ] Any structure received from user is validated before use
      (sizes bounded, offsets inside the buffer, flags in the known set).
- [ ] String lengths are capped with the `maxlen` argument to
      `copyinstr(9)`; no unbounded lengths pulled from user fields.
- [ ] No kernel pointer is written out verbatim to user space (no info
      leak, no bypass of address-space-layout protections).

## Common Traps

- Assuming a write of known size is safe because the user "always
  passes the right value". Size fields must be bounded on input.
- Copying user data once and trusting it thereafter, when an
  attacker can change the buffer between two accesses. Copy once,
  work from the kernel copy only.
- Holding a sleep mutex across a copy primitive. Copy primitives may
  sleep; design the critical section to release before copying.

## Where to Read More

- `copyin(9)`, `copyout(9)`, `copyinstr(9)`, `uiomove(9)` manual pages.
- `/usr/src/sys/sys/uio.h` for `struct uio`.
- Chapter 5 for kernel-C context. Chapter 9 for a working character
  driver `d_read`/`d_write` path that uses `uiomove(9)` end to end.
