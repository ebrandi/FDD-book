#!/bin/sh
#
# build-kernel.sh - walk through building a debug kernel.
#
# This script is not run automatically; it prints the commands the
# reader should run, with brief commentary. It does not change the
# system.

cat <<'EOF'
Debug kernel build procedure
============================

1. Ensure /usr/src/ is populated. If it is not:

    # git clone --depth 1 -b releng/14.3 \
        https://git.freebsd.org/src.git /usr/src

2. Review the existing GENERIC-DEBUG config:

    $ cat /usr/src/sys/amd64/conf/GENERIC-DEBUG

3. Build the kernel:

    # cd /usr/src
    # make buildkernel KERNCONF=GENERIC-DEBUG

   This takes twenty to forty minutes on a modest VM.

4. Install the new kernel (the previous one is kept as kernel.old):

    # make installkernel KERNCONF=GENERIC-DEBUG

5. Reboot:

    # shutdown -r now

6. After reboot, confirm:

    $ uname -v
    $ sysctl debug.kdb.supported_backends
    $ sysctl debug.debugger_on_panic

7. If the new kernel fails to boot, pick "Boot kernel.old" from the
   loader menu to return to the previous working kernel.

EOF
