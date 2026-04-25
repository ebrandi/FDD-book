# Where Drivers Fit in the Operating System

A short map of how a user program reaches your driver, and how your
driver reaches the hardware. Use it to sanity-check where a piece of
new code belongs before you write it.

## The Sandwich

```
+--------------------------------------------------------------------+
|                           USER SPACE                               |
|                                                                    |
|   Applications, daemons, shells, tools                             |
|      open(2), read(2), write(2), ioctl(2), socket(2), ...          |
+--------------------------------------------------------------------+
                                  |
                      system-call trap (boundary)
                                  |
+--------------------------------------------------------------------+
|                          KERNEL SPACE                              |
|                                                                    |
|   System-call dispatcher                                           |
|     |                                                              |
|     +-- VFS / devfs --> cdevsw entry points (char / pseudo)        |
|     |       |                                                      |
|     |       +-- your driver (d_open, d_read, d_write, d_ioctl)     |
|     |                                                              |
|     +-- Network stack (socket, IP, L2) --> ifnet                   |
|     |       |                                                      |
|     |       +-- your driver (if_transmit, receive callback)        |
|     |                                                              |
|     +-- VFS / GEOM --> GEOM class / provider                       |
|             |                                                      |
|             +-- your driver (g_start, biodone)                     |
|                                                                    |
|     Core kernel services (proc, vm, locks, callout, taskqueue,     |
|       bus, interrupts, sysctl, eventhandler, kld)                  |
|                                                                    |
|   Newbus tree                                                      |
|     root -> pci -> your-device -> your-softc                       |
|     root -> usbus -> your-device -> your-softc                     |
|     root -> iicbus -> your-device -> your-softc                    |
+--------------------------------------------------------------------+
                                  |
                        hardware I/O boundary
                                  |
+--------------------------------------------------------------------+
|                           HARDWARE                                 |
|                                                                    |
|   MMIO registers, interrupt lines, DMA-capable memory              |
+--------------------------------------------------------------------+
```

Your driver is the single component with feet on both sides of the
lower boundary: it speaks the hardware's language down, and the
kernel subsystem's language up.

## What Lives Where

| Concern                        | Lives in                               |
| :----------------------------- | :------------------------------------- |
| User pointer validation        | Subsystem boundary (syscall handler or |
|                                | `d_ioctl` prologue)                    |
| Kernel-to-user copy            | Same boundary                          |
| Subsystem-format buffer        | Subsystem layer (`uio`, `mbuf`, `bio`) |
| Pending-work queue             | Your driver's softc                    |
| Softc lock                     | Your driver                            |
| Register access                | Your driver                            |
| Interrupt filter               | Your driver (registered via            |
|                                | `bus_setup_intr`)                      |
| Interrupt ithread / taskqueue  | Your driver                            |
| DMA descriptor setup           | Your driver                            |
| Cache coherence / bus sync     | `bus_dma(9)`, invoked from your driver |

## Decision Heuristics

1. **Is this code validating something from userland?** It belongs at
   the subsystem boundary (the first lines of `d_ioctl`, the first
   lines of the syscall handler, the receive path of the stack).

2. **Is this code manipulating per-device state?** It belongs in the
   softc, under the softc lock.

3. **Is this code touching a register?** It belongs in the driver,
   through `bus_space_read_*` / `bus_space_write_*`.

4. **Is this code deciding when work runs?** It belongs in an interrupt
   filter, an ithread, a taskqueue, or a callout, depending on
   urgency and sleepability.

5. **Is this code allocating memory?** Use `malloc(9)` for control
   data, `uma(9)` for frequent fixed-size allocations,
   `bus_dmamem_alloc(9)` for DMA buffers.

## Newbus in One Paragraph

Every FreeBSD device is a `device_t` node in the Newbus tree. The
root of the tree (`root0`) has a child for each top-level bus
(`nexus0`), and each bus has a child for each peripheral it
enumerates. Your driver registers with a parent bus name
(`pci`, `usbus`, `iicbus`, `spibus`, `acpi`, `simplebus`) in its
`DRIVER_MODULE` line, and the kernel offers each child of that bus
to your `probe` function. The `device_t` you receive is a handle
into the tree; the softc behind it is your per-instance state. Read
`devinfo -v` on a running system to see the tree in action.

## Common Traps

- **Putting user-pointer validation deep inside the driver.** If the
  entry point already took a copied, validated structure, the
  internal paths should not re-validate the raw user pointer; they
  should not have access to it at all.
- **Mixing subsystem buffers.** An `mbuf` does not belong in the
  storage path; a `bio` does not belong in the network path. If a
  pipeline seems to need conversion, a layer boundary is in the
  wrong place.
- **Reaching past the subsystem to call into another driver
  directly.** Let the subsystem be the contact surface. Cross-driver
  coupling through private APIs is the fastest way to make both
  drivers unmaintainable.

## Where to Read More

- `intro(9)` for the kernel overview, `devinfo(8)` for the running
  Newbus tree, `sysctl(8)` for runtime visibility.
- Chapter 3 for UNIX foundations, Chapter 6 for driver anatomy,
  Chapter 7 for a first complete driver.
- Appendix A for the full API vocabulary.
