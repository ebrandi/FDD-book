# Manual-Page Checklist for Driver Writers

A list of the section 9 manual pages that every FreeBSD driver author
should be comfortable opening. Each line is a page, not a command; run
`man 9 <name>` to read it.

You do not need to memorise these. You need to know that they exist, so
that you can open the right one the moment you need it.

## Memory

- `malloc`
- `contigmalloc`

## Synchronisation

- `mutex`
- `mtx_pool`
- `sx`
- `rmlock`
- `condvar`
- `sema`
- `atomic`
- `epoch`
- `lock`
- `locking`

## Deferred execution

- `callout`
- `taskqueue`
- `kthread`
- `kproc`

## Bus and resources

- `bus_space`
- `bus_dma`
- `rman`
- `bus_alloc_resource`
- `bus_release_resource`
- `bus_activate_resource`
- `BUS_SETUP_INTR`
- `device`
- `device_get_softc`
- `device_printf`
- `devclass`
- `DEVICE_PROBE`
- `DEVICE_ATTACH`
- `DEVICE_DETACH`

## Module declaration

- `module`
- `DRIVER_MODULE`
- `MODULE_VERSION`
- `MODULE_DEPEND`
- `DEV_MODULE`

## Device nodes

- `make_dev`
- `dev_clone`
- `devfs_set_cdevpriv`
- `dev_refthread`

## User-space interaction

- `copy`
- `fetch`
- `store`
- `uio`
- `priv`

## Observability

- `sysctl`
- `sysctl_add_oid`
- `sysctl_ctx_init`
- `EVENTHANDLER`
- `selrecord`
- `kqueue`

## Diagnostics

- `printf`
- `KASSERT`
- `ktr`
- `SDT`

## How to learn one manual page a day

Reading one manual page per working day is a realistic pace. Over
roughly two months, you will have made the full pass above. Start with
the families you already use, then broaden outward.

The manual pages on FreeBSD are intentionally short and focused.
Reading them is often faster than searching the web, and the results
are always up to date with your installed kernel.
