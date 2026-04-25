# Boot-to-Init Timeline

A compact timeline of how a FreeBSD machine gets from power-on to a
userland ready to run your driver. Use it to orient yourself when a
bug happens early in boot, or when you need to decide at which stage
a piece of initialisation should happen.

## Timeline

```
Power on
   |
   v
Firmware (BIOS or UEFI)                         [hardware + vendor code]
   |
   v
First-stage boot (boot0 / UEFI boot manager)    [/usr/src/stand/]
   |
   v
FreeBSD loader                                   loader(8)
   | reads /boot/loader.conf, loads kernel
   | and any preload modules, passes tunables
   v
Kernel entry (arch-specific)                     hammer_time()   (amd64)
   | early assembly -> C                         init386()       (i386)
   | set up basic CPU state, console             (arch-specific) (arm64...)
   v
mi_startup()                                     sys/kern/init_main.c
   | walks the sorted SYSINIT chain, in increasing SI_SUB_* value
   |
   |-- SI_SUB_VM            (virtual memory up; malloc(9) safe)
   |-- SI_SUB_LOCK          (locking primitives ready)
   |-- SI_SUB_EVENTHANDLER  (event handlers registrable)
   |-- SI_SUB_KLD           (module and KLD system up)
   |-- SI_SUB_INTRINSIC     (proc 0 initialised)
   |-- SI_SUB_DRIVERS       (early driver-subsystem init)
   |-- SI_SUB_CONFIGURE     (bus probe/attach runs)      <-- your driver here
   |-- SI_SUB_KICK_SCHEDULER (callout machinery starts)
   |-- SI_SUB_ROOT_CONF     (root FS configuration stage)
   |-- SI_SUB_CREATE_INIT   (init proc created in stopped state)
   |-- SI_SUB_KTHREAD_INIT  (init proc made runnable by kick_init)
   |-- SI_SUB_LAST          (terminal SYSINIT bucket)
   v
start_init()                                     sys/kern/init_main.c
   | runs on PID 1 once scheduled, mounts root FS
   | via vfs_mountroot(), then execs /sbin/init
   v
init(8)                                          init(8), rc(8)
   | runs /etc/rc, brings up services, opens consoles
   v
login(1), daemons, getty, and the system is up
```

Stage names match `SI_SUB_*` constants in `/usr/src/sys/sys/kernel.h`,
sorted by their numeric value. Only the driver-relevant landmarks are
shown; many more stages exist between them. Order numbers
(`SI_ORDER_FIRST`, `SI_ORDER_MIDDLE`, `SI_ORDER_ANY`) decide ordering
within one subsystem.

## Driver-Relevant Facts

- A **compiled-in** driver runs its probe/attach at
  `SI_SUB_CONFIGURE`, once buses have enumerators.
- A **preloaded module** (listed in `/boot/loader.conf`) is available
  to the kernel by `SI_SUB_KLD` and registers with Newbus in the
  usual way. Its `probe`/`attach` also happens at `SI_SUB_CONFIGURE`.
- A **runtime `kldload(8)`** happens after boot. Probe/attach happens
  immediately, against whatever devices the module claims.
- A driver that must drive the root disk must reach the kernel before
  `SI_SUB_VFS`. That usually means built-in or preloaded.

## What Becomes Usable When

| You want to...                               | Wait until (at least)...  |
| :------------------------------------------- | :------------------------ |
| Call `malloc(9)` with `M_WAITOK`             | `SI_SUB_VM`               |
| Take and release a mutex                     | `SI_SUB_LOCK`             |
| Register an event handler                    | `SI_SUB_EVENTHANDLER`     |
| Load a kernel module                         | `SI_SUB_KLD`              |
| `probe` / `attach` your device               | `SI_SUB_CONFIGURE`        |
| Arm a `callout(9)` and expect it to fire     | `SI_SUB_KICK_SCHEDULER`   |
| Read from the root filesystem                | After `SI_SUB_ROOT_CONF`  |
| Expect any userland process to exist         | After `start_init` runs   |

## Common Traps

- **Assuming the console is available very early.** The kernel has a
  console message buffer from the start, but not every stage can
  flush it to the actual screen. Early panics may leave limited
  output; `boot -v` and `hw.serial` tunables help.
- **Depending on file-system presence at attach.** Root may not be
  mounted yet. If you need to read a file at boot, use
  `eventhandler(9)` to subscribe to `mountroot` or similar events.
- **Writing `SYSINIT` functions that can sleep before `SI_SUB_VM`.**
  Early subsystems run before the VM is fully up; check the stage
  before you call allocation or locking primitives.

## Where to Read More

- `/usr/src/sys/kern/init_main.c` for `mi_startup` and `start_init`.
- `/usr/src/sys/sys/kernel.h` for `SYSINIT` macros and `SI_SUB_*`.
- `/usr/src/sys/kern/subr_bus.c` for the Newbus side of
  `SI_SUB_CONFIGURE`.
- `loader(8)`, `loader.conf(5)`, `kld(9)`, `kldload(8)`, `kldstat(8)`.
- Chapter 2 for boot in a lab context. Chapter 6 for the driver side
  of module loading. Appendix E for deeper subsystem detail.
