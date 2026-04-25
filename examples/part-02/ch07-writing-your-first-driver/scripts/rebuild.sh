#!/bin/sh
#
# FreeBSD kernel module rebuild script
# Usage: ./rebuild.sh <module_name>
#
# Drops, rebuilds, reloads, and reports on a kernel module under
# development. Designed for the inner loop of /home/${USER}/drivers
# style work, not for production deployment.

set -e

MODULE_NAME="${1}"

if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
fi

print_step()    { printf "${BLUE}==>${NC} ${1}\n"; }
print_success() { printf "${GREEN}OK${NC}  ${1}\n"; }
print_error()   { printf "${RED}ERR${NC} ${1}\n" >&2; }
print_warning() { printf "${YELLOW}!${NC}   ${1}\n"; }

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script must be run as root or with sudo"
        exit 1
    fi
}

is_module_loaded() {
    kldstat -q -n "${1}" 2>/dev/null
}

if [ -z "${MODULE_NAME}" ]; then
    print_error "Usage: $0 <module_name>"
    exit 1
fi

if [ ! -f "${MODULE_NAME}.c" ]; then
    print_error "Source file '${MODULE_NAME}.c' not found in current directory"
    exit 1
fi

check_root

if [ ! -f "Makefile" ]; then
    print_error "Makefile not found in current directory"
    exit 1
fi

print_step "Checking if module '${MODULE_NAME}' is loaded..."
if is_module_loaded "${MODULE_NAME}"; then
    print_warning "Module is loaded, unloading..."

    DMESG_BEFORE_UNLOAD=$(dmesg | wc -l)

    if kldunload "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module unloaded successfully"
    else
        print_error "Failed to unload module"
        exit 1
    fi

    sleep 1
    if is_module_loaded "${MODULE_NAME}"; then
        print_error "Module still loaded after unload attempt"
        exit 1
    fi
    print_success "Verified: module removed from memory"

    DMESG_AFTER_UNLOAD=$(dmesg | wc -l)
    DMESG_UNLOAD_NEW=$((DMESG_AFTER_UNLOAD - DMESG_BEFORE_UNLOAD))
    if [ ${DMESG_UNLOAD_NEW} -gt 0 ]; then
        echo
        print_step "Kernel messages from unload:"
        dmesg | tail -n ${DMESG_UNLOAD_NEW}
        echo
    fi
else
    print_success "Module not loaded, proceeding..."
fi

print_step "Cleaning build artifacts..."
make clean
print_success "Clean completed"

print_step "Building module..."
make
print_success "Build completed"

if [ ! -f "./${MODULE_NAME}.ko" ]; then
    print_error "Module file './${MODULE_NAME}.ko' not found after build"
    exit 1
fi

print_step "Loading module..."
DMESG_BEFORE=$(dmesg | wc -l)

if kldload "./${MODULE_NAME}.ko"; then
    print_success "Module load command executed"
else
    print_error "Failed to load module"
    exit 1
fi

sleep 1
print_step "Verifying module load..."
if is_module_loaded "${MODULE_NAME}"; then
    print_success "Module is loaded in kernel"
    echo
    kldstat | head -n 1
    kldstat | grep "${MODULE_NAME}"
else
    print_error "Module not found in kldstat output"
    exit 1
fi

echo
print_step "Recent kernel messages from load:"
DMESG_AFTER=$(dmesg | wc -l)
DMESG_NEW=$((DMESG_AFTER - DMESG_BEFORE))

if [ ${DMESG_NEW} -gt 0 ]; then
    dmesg | tail -n ${DMESG_NEW}
else
    print_warning "No new kernel messages"
    dmesg | tail -n 5
fi

echo
print_success "Module '${MODULE_NAME}' rebuilt and loaded successfully!"
