# Driver Type Comparison

A compact map of the driver categories FreeBSD publishes. Use it when
you are starting a new driver and deciding which subsystem interface
to implement, or when you open an unfamiliar driver and want to know
which set of rules applies.

## Side-by-side

| Aspect              | Character                          | Network                         | Storage (GEOM)                 | Pseudo                          |
| :------------------ | :--------------------------------- | :------------------------------ | :----------------------------- | :------------------------------ |
| Registers with      | `cdevsw` via `make_dev_s`          | `ifnet`                         | GEOM class / provider          | Same as the category it mimics  |
| Userland view       | `/dev/<name>` node                 | `if_<name>` (e.g., `em0`)       | `/dev/<disk>` node (GEOM)      | `/dev/<name>` or `if_<name>`    |
| Main entry points   | `d_open`, `d_close`, `d_read`,     | `if_transmit`, receive          | `g_start`, `biodone(9)`        | Same as above                   |
|                     | `d_write`, `d_ioctl`               | callbacks, link-state           |                                |                                 |
| Main buffer         | `struct uio` (user I/O)            | `struct mbuf` chain             | `struct bio` request           | Same as above                   |
| Completion style    | Return from entry point            | `m_freem(m)` or post to stack   | `biodone(9)` on the `bio`      | Same as above                   |
| Typical device      | Serial port, sensor, `/dev/null`   | Ethernet, WiFi, `tun`, `tap`    | Disk, SSD, `md(4)`             | Any driver with no hardware     |
| Example source      | `/usr/src/sys/dev/null/null.c`     | `/usr/src/sys/net/if_tuntap.c`  | `/usr/src/sys/dev/md/md.c`     | `/usr/src/sys/dev/null/null.c`  |
| Taught in           | Chapter 7, 8, 9                    | Chapter 28                      | Chapter 27                     | Chapter 7 (as first driver)     |

## Decision prompts

Answer these in order. Usually one answer ends the question.

1. **Is the device a network interface?** If yes, you want an
   `ifnet`-based network driver. Everything network-shaped flows
   through the stack, not through `cdevsw`.

2. **Is the device storage with a GEOM-compatible abstraction?** If
   yes, write a GEOM class or provider. The data unit is `struct bio`,
   completion is `biodone(9)`, and filesystems reach you through the
   GEOM tree.

3. **Is the device a stream or control surface exposed as a device
   node?** A character driver with a `cdevsw` is the right shape.

4. **Is there no hardware at all?** You are writing a pseudo-device.
   Pick the category of its userland interface (character, network,
   storage) and follow that category's rules.

## What not to mix

- Do **not** reach for `cdevsw` when writing a network driver. The
  stack expects an `ifnet`, not a `/dev` node.
- Do **not** expose a disk as a raw `cdevsw` character device. Modern
  FreeBSD routes disks through GEOM; bypassing it skips partitioning,
  caching, and transforms the rest of the system expects.
- Do **not** register with both `cdevsw` and `ifnet` for the same
  logical device. If you need a userland control surface for a
  network driver, add a `sysctl(9)` tree or a small `ioctl(2)` on
  a separate character node; do not duplicate identities.

## Minimum registration sketches

Character driver (end of file):

```c
static struct cdevsw mydev_cdevsw = {
    .d_version = D_VERSION,
    .d_name    = "mydev",
    .d_open    = mydev_open,
    .d_close   = mydev_close,
    .d_read    = mydev_read,
    .d_write   = mydev_write,
    .d_ioctl   = mydev_ioctl,
};

DRIVER_MODULE(mydev, <parent-bus>, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
```

Network driver (key step at attach):

```c
if_t ifp = if_alloc(IFT_ETHER);
if_setsoftc(ifp, sc);
if_initname(ifp, device_get_name(dev), device_get_unit(dev));
if_settransmitfn(ifp, mydev_transmit);
/* ... further if_set*() calls to wire up flags, MTU, mediastatus ... */
if_attach(ifp);
```

GEOM provider (skeleton):

```c
static struct g_class g_mydev_class = {
    .name    = "mydev",
    .version = G_VERSION,
    .start   = mydev_start,
    /* ... */
};
DECLARE_GEOM_CLASS(g_mydev_class, mydev);
```

## Common traps

- Writing a character driver and then realising partway through that
  the device is really network-shaped. Stop and switch early; the
  buffer type and the locking shape are both different.
- Copying the `cdevsw` of a real device when writing a pseudo-device
  without trimming the entry points to the ones you actually support.
  Unused entry points should not be registered; they should simply be
  absent from the `cdevsw` initialiser.

## Where to read more

- `cdevsw(9)`, `ifnet(9)`, `bio(9)`, `geom(4)`, `geom(9)`.
- Appendix A for the full API entries.
- Chapter 6 for driver anatomy, Chapter 7 for the first character
  driver, Chapter 27 for GEOM and storage, Chapter 28 for `ifnet` and
  networking.
