# Shared Build and Development Notes

## Prerequisites

- FreeBSD 14.3 or later.
- Kernel sources under `/usr/src`.
- `devel/dtc` (install with `pkg install dtc`).
- For cross-building from amd64 to arm64, a full FreeBSD build tree.

## Standard Makefile pattern

Every kernel module in this chapter uses the same Makefile shape:

```make
KMOD=   <module-name>
SRCS=   <module-name>.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

`bsd.kmod.mk` handles compilation, linking, symbol tables, and debug
output.  Typing `make` produces `<module>.ko` and
`<module>.ko.debug`.

## Compiling DT overlays

Overlay sources have the extension `.dts` (some projects use
`.dtso`; the content is the same).  Compile with:

```sh
dtc -I dts -O dtb -@ -o out.dtbo input.dts
```

- `-I dts` declares the input is text.
- `-O dtb` requests binary output.
- `-@` includes symbol information for label resolution.

Install the output under `/boot/dtb/overlays/` and list the
basename (without `.dtbo`) in `fdt_overlays` in `/boot/loader.conf`.

## Loading modules manually

```sh
sudo kldload ./module.ko
kldstat -m module
sudo kldunload module
```

Use `./` before the filename to load from the current directory
rather than the default kernel module path.

## Boot-time loading

In `/boot/loader.conf`:

```
module_load="YES"
fdt_overlays="overlay1,overlay2"
```

Ensure the module `.ko` lives in `/boot/modules/` and the overlays
live in `/boot/dtb/overlays/`.

## Common debugging commands

```sh
# Show the current DT as the kernel sees it
ofwdump -a | less

# Show one node
ofwdump -p /soc/your-node

# Show one property
ofwdump -P compatible /soc/your-node

# Show attached devices
devinfo -r

# Show sysctls for a driver
sysctl dev.your-driver

# Show interrupt counts
vmstat -i
```

## Tearing down state between iterations

When iterating on a driver, unload before reloading:

```sh
sudo kldunload your-driver
sudo kldload ./your-driver.ko
```

If `kldunload` returns `EBUSY`, detach did not release all resources.
Check the source and try again.
