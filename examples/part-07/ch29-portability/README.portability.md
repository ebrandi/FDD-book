# portdrv - Portability Notes

This document records the portability surface of the `portdrv`
reference driver that accompanies Chapter 29.

## Supported Platforms

| Architecture | Status      | Notes                                              |
|--------------|-------------|----------------------------------------------------|
| amd64        | tested      | Primary development platform.                      |
| i386         | tested      | Builds cleanly; runtime tested under bhyve.        |
| arm64        | builds      | Cross-compile tested; runtime should work.         |
| riscv64      | builds      | Cross-compile tested; runtime not validated.       |
| powerpc64    | builds      | Cross-compile tested; validates endian paths.      |

## Supported FreeBSD Versions

| Version   | Status          | Notes                                            |
|-----------|-----------------|--------------------------------------------------|
| 14.3      | primary target  | All labs assume this release.                    |
| 14.2      | tested          | Builds and loads; API surface identical.         |
| 13.x      | not supported   | Pre-`bus_barrier` work; would need backporting.  |

## Supported Buses

| Bus    | Backend file      | Status  |
|--------|-------------------|---------|
| PCI    | `portdrv_pci.c`   | tested  |
| SIM    | `portdrv_sim.c`   | tested  |
| FDT    | (challenge)       | planned |
| USB    | (challenge)       | planned |

## Supported Build Configurations

| Configuration                                    | Status  |
|--------------------------------------------------|---------|
| `make` (defaults to SIM only)                    | tested  |
| `make PORTDRV_WITH_PCI=yes`                      | tested  |
| `make PORTDRV_WITH_SIM=yes`                      | tested  |
| `make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes` | tested  |

## Known Limitations

1. The driver assumes little-endian hardware registers. Big-endian
   hardware would require swapping the `htole`/`letoh` calls in the
   data path to `htobe`/`betoh`.
2. The simulation backend does not model DMA. A production driver
   with DMA would need a richer simulation.
3. The driver does not currently support hotplug. `detach` is only
   exercised on `kldunload`.

## How to Report a Portability Bug

Include the output of:

```sh
uname -a
make -V CFLAGS
sysctl dev.portdrv
dmesg | grep portdrv
```

Along with the exact build command used and the platform on which the
failure occurred. Bug reports that include these four items are much
easier to triage.

## Version History

| Version | Change                                                         |
|---------|----------------------------------------------------------------|
| 1       | Initial monolithic driver (Lab 1).                             |
| 2       | Added backend interface (Lab 3).                               |
| 3       | Split into multiple files (Lab 4).                             |
| 4       | Added simulation backend (Lab 5).                              |
| 5       | Added conditional compilation (Lab 6).                         |
| 6       | Added endian helpers (Lab 7).                                  |
| 7       | Added sysctl tree (Lab 10).                                    |
| 8       | Added runtime backend selection (Lab 11).                      |
