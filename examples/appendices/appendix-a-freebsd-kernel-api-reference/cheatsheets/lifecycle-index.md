# APIs by Driver Lifecycle Phase

A reverse index to Appendix A. When you are writing or auditing a
particular phase of a driver, these are the APIs that usually belong
there.

## Module load

- `MODULE_VERSION`, `MODULE_DEPEND`
- Static `MALLOC_DEFINE`
- Static `SYSCTL_NODE`
- `SDT_PROVIDER_DEFINE`
- One-time event-handler registration

## Probe

- `device_get_parent`, `device_get_nameunit`
- Match-scoring return values: `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`,
  `BUS_PROBE_SPECIFIC`, `BUS_PROBE_LOW_PRIORITY`, `ENXIO`

## Attach

- `device_get_softc`
- `malloc` and `MALLOC_DEFINE` tags
- `mtx_init`, `sx_init`, `rm_init`, `cv_init`, `sema_init`
- `bus_alloc_resource` or `bus_alloc_resource_any`
- `bus_space` setup through `rman_get_bustag` and `rman_get_bushandle`
- `bus_dma_tag_create`, `bus_dmamap_create`
- `callout_init`, `TASK_INIT`, `taskqueue_create` when needed
- `bus_setup_intr` (as the last attach step)
- `make_dev_s`
- `device_get_sysctl_ctx`, `SYSCTL_ADD_*`
- `uma_zcreate`
- Driver-specific event-handler registration

## Normal operation

- `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, `d_poll`, `d_kqfilter`
- `uiomove`, `copyin`, `copyout`, `copyinstr`
- `bus_space_read_N`, `bus_space_write_N`, `bus_space_barrier`
- `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`
- `mtx_lock`, `mtx_unlock`, `sx_slock`, `sx_xlock`
- `cv_wait`, `cv_signal`, `cv_broadcast`
- `atomic_*`
- `callout_reset`, `taskqueue_enqueue`
- `device_printf`, `log`, `KASSERT`, `SDT_PROBE`

## Detach (reverse of attach)

- `bus_teardown_intr` (first)
- `destroy_dev`
- `callout_drain`
- `taskqueue_drain_all` and `taskqueue_free` for private queues
- `bus_dmamap_unload`, `bus_dmamap_destroy`, `bus_dma_tag_destroy`
- `bus_release_resource` for every resource from attach
- `cv_destroy`, `sx_destroy`, `mtx_destroy`, `rm_destroy`, `sema_destroy`
- `uma_zdestroy`
- Driver-specific event-handler deregistration
- Final `free` and `contigfree`

## Module unload

- Refuse unload if state remains (Newbus usually handles this for you)
- Static resources declared at module load clean themselves up

## Print this page

This index is intentionally short. Keep it next to your monitor, or
export it to a single-page PDF. It should fit on one printed page in
almost any font.
