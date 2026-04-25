# Chapter 24 Integration Concept Map

This document maps every concept introduced in Chapter 24 to the file
under this directory where the concept first appears. Use it as a
lookup table when working through the chapter.

## Section 1: Why Integration Matters

Conceptual material only; no code in this stage. The chapter explains
the difference between a working module and an integrated driver.

## Section 2: Working With devfs and the Device Tree

| Concept | File |
|---------|------|
| `make_dev_args_init` and `make_dev_s` | `stage1-devfs/myfirst.c` (in `myfirst_attach`) |
| `D_TRACKCLOSE` flag | `stage1-devfs/myfirst.c` (in `myfirst_cdevsw`) |
| `si_drv1` per-cdev state | `stage1-devfs/myfirst.c` (set in `mda_si_drv1`, read in cdevsw callbacks) |
| `destroy_dev` drain semantics | `stage1-devfs/myfirst.c` (in `myfirst_detach`) |

## Section 3: Implementing ioctl() Support

| Concept | File |
|---------|------|
| `_IO`, `_IOR`, `_IOW`, `_IOWR` macros | `stage2-ioctl/myfirst_ioctl.h` |
| Group letter `'M'` choice | `stage2-ioctl/myfirst_ioctl.h` |
| Public header convention | `stage2-ioctl/myfirst_ioctl.h` (no kernel-only headers included) |
| `d_ioctl` callback | `stage2-ioctl/myfirst_ioctl.c` (function `myfirst_ioctl`) |
| `ENOIOCTL` for unknown commands | `stage2-ioctl/myfirst_ioctl.c` (default branch) |
| `fflag & FWRITE` check | `stage2-ioctl/myfirst_ioctl.c` (in `MYFIRSTIOC_SETMSG` and `MYFIRSTIOC_RESET`) |
| User-space companion | `stage2-ioctl/myfirstctl.c` |
| Build of the companion | `stage2-ioctl/Makefile.user` |

## Section 4: Exposing Metrics Through sysctl()

| Concept | File |
|---------|------|
| Per-device sysctl context | `stage3-sysctl/myfirst_sysctl.c` (function `myfirst_sysctl_attach`) |
| `SYSCTL_ADD_UINT` for counters | `stage3-sysctl/myfirst_sysctl.c` (open_count, total_reads, total_writes) |
| `SYSCTL_ADD_STRING` for version | `stage3-sysctl/myfirst_sysctl.c` (version, message, debug.classes) |
| `SYSCTL_ADD_PROC` with handler | `stage3-sysctl/myfirst_sysctl.c` (message_len) |
| `SYSCTL_ADD_NODE` for subtree | `stage3-sysctl/myfirst_sysctl.c` (debug subtree) |
| `CTLFLAG_RW \| CTLFLAG_TUN` | `stage3-sysctl/myfirst_sysctl.c` (debug.mask OID) |
| `TUNABLE_INT_FETCH` | `stage3-sysctl/myfirst.c` (in `myfirst_attach`, before the sysctl call) |
| `/boot/loader.conf` example | `labs/loader.conf.example` |

## Section 5: Networking Integration (Optional)

No code in this directory. The chapter cites
`/usr/src/sys/net/if_disc.c` as the canonical example of `if_alloc`,
`if_attach`, and `bpfattach`.

## Section 6: CAM Storage Integration (Optional)

No code in this directory. The chapter cites
`/usr/src/sys/dev/ahci/ahci.c` as the canonical example of
`cam_sim_alloc`, `xpt_bus_register`, and the action callback.

## Section 7: Registration, Teardown, Cleanup Discipline

| Concept | File |
|---------|------|
| `goto err` chain in attach | `stage3-sysctl/myfirst.c` (in `myfirst_attach`) |
| Reverse-order teardown | `stage3-sysctl/myfirst.c` (in `myfirst_detach`) |
| Soft refusal on open count | `stage3-sysctl/myfirst.c` (early `EBUSY` return) |

## Section 8: Refactoring and Versioning

| Concept | File |
|---------|------|
| Source-tree split by concern | All `stage3-sysctl/` files |
| Public vs private headers | `stage2-ioctl/myfirst_ioctl.h` is public; everything else is private |
| `MYFIRST_VERSION` release string | `stage3-sysctl/myfirst_sysctl.c` |
| `MODULE_VERSION` integer | `stage3-sysctl/myfirst.c` |
| `MYFIRST_IOCTL_VERSION` API integer | `stage2-ioctl/myfirst_ioctl.h` |

## Section 9: Hands-on Labs

| Lab | Script |
|-----|--------|
| Lab 24.1 (build stage 1) | `labs/lab24_1_stage1.sh` |
| Lab 24.2 (add ioctls) | `labs/lab24_2_stage2.sh` |
| Lab 24.3 (add sysctl) | `labs/lab24_3_stage3.sh` |
| Lab 24.4 (inject failure) | `labs/lab24_4_failure.sh` |
| Lab 24.5 (DTrace traces) | `labs/lab24_5_dtrace.sh` |
| Lab 24.6 (smoke test) | `labs/lab24_6_smoke.sh` |
| Lab 24.7 (reload test) | `labs/lab24_7_reload.sh` |
