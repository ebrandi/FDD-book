/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * hwsim2.c -- Chapter 17 stand-alone simulation practice module.
 *
 * Demonstrates callout-driven hardware simulation without the myfirst
 * driver. Exposes:
 *   - a SENSOR value that oscillates, updated every 100 ms
 *   - a simulated command cycle with a configurable delay
 *   - a fault-injection flag
 *   - sysctls for control and observation
 *
 * This module is x86-specific. See the discussion in Chapter 16 about
 * the simulation shortcut for bus_space_tag_t.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <machine/bus.h>

#if !defined(__amd64__) && !defined(__i386__)
#error "hwsim2 simulation supports x86 only"
#endif

MALLOC_DEFINE(M_HWSIM2, "hwsim2", "hwsim2 simulation module");

#define HWSIM2_SIZE         0x40
#define HWSIM2_REG_SENSOR    0x00
#define HWSIM2_REG_CMD_DATA  0x04
#define HWSIM2_REG_CMD_GO    0x08
#define HWSIM2_REG_RESULT    0x0c
#define HWSIM2_REG_STATUS    0x10

#define HWSIM2_STATUS_BUSY   0x00000001u
#define HWSIM2_STATUS_DONE   0x00000002u
#define HWSIM2_STATUS_ERROR  0x00000004u

static uint8_t            *hwsim2_buf;
static bus_space_tag_t     hwsim2_tag;
static bus_space_handle_t  hwsim2_handle;

static struct mtx          hwsim2_mtx;
static struct callout      hwsim2_sensor_co;
static struct callout      hwsim2_cmd_co;

static uint32_t            hwsim2_sensor_tick;
static uint32_t            hwsim2_sensor_baseline = 0x1000;
static uint32_t            hwsim2_delay_ms = 500;
static bool                hwsim2_inject_error;
static bool                hwsim2_running;

static void
hwsim2_sensor_cb(void *arg __unused)
{
        uint32_t phase, value;

        mtx_assert(&hwsim2_mtx, MA_OWNED);

        if (!hwsim2_running)
                return;

        hwsim2_sensor_tick++;
        phase = hwsim2_sensor_tick & 0x7;
        value = hwsim2_sensor_baseline +
                ((phase < 4) ? phase : (7 - phase)) * 16;

        bus_space_write_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_SENSOR, value);

        callout_reset_sbt(&hwsim2_sensor_co,
            100 * SBT_1MS, 0, hwsim2_sensor_cb, NULL, 0);
}

static void
hwsim2_cmd_cb(void *arg __unused)
{
        uint32_t data, status;

        mtx_assert(&hwsim2_mtx, MA_OWNED);

        if (!hwsim2_running)
                return;

        data = bus_space_read_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_CMD_DATA);
        bus_space_write_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_RESULT, data);

        status = HWSIM2_STATUS_DONE;
        if (hwsim2_inject_error) {
                status |= HWSIM2_STATUS_ERROR;
                hwsim2_inject_error = false;
        }
        bus_space_write_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_STATUS, status);
}

static int
hwsim2_sysctl_sensor(SYSCTL_HANDLER_ARGS)
{
        uint32_t value;

        mtx_lock(&hwsim2_mtx);
        value = bus_space_read_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_SENSOR);
        mtx_unlock(&hwsim2_mtx);
        return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
hwsim2_sysctl_result(SYSCTL_HANDLER_ARGS)
{
        uint32_t value;

        mtx_lock(&hwsim2_mtx);
        value = bus_space_read_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_RESULT);
        mtx_unlock(&hwsim2_mtx);
        return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
hwsim2_sysctl_status(SYSCTL_HANDLER_ARGS)
{
        uint32_t value;

        mtx_lock(&hwsim2_mtx);
        value = bus_space_read_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_STATUS);
        mtx_unlock(&hwsim2_mtx);
        return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
hwsim2_sysctl_do_command(SYSCTL_HANDLER_ARGS)
{
        uint32_t value = 0;
        int error;

        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        mtx_lock(&hwsim2_mtx);
        bus_space_write_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_CMD_DATA, value);
        bus_space_write_4(hwsim2_tag, hwsim2_handle,
            HWSIM2_REG_STATUS, HWSIM2_STATUS_BUSY);
        callout_reset_sbt(&hwsim2_cmd_co,
            hwsim2_delay_ms * SBT_1MS, 0, hwsim2_cmd_cb, NULL, 0);
        mtx_unlock(&hwsim2_mtx);
        return (0);
}

static SYSCTL_NODE(_dev, OID_AUTO, hwsim2,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, "hwsim2 simulation module");

static int
hwsim2_modevent(module_t mod, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                hwsim2_buf = malloc(HWSIM2_SIZE, M_HWSIM2,
                    M_WAITOK | M_ZERO);
                hwsim2_tag = X86_BUS_SPACE_MEM;
                hwsim2_handle = (bus_space_handle_t)(uintptr_t)hwsim2_buf;

                mtx_init(&hwsim2_mtx, "hwsim2", NULL, MTX_DEF);
                callout_init_mtx(&hwsim2_sensor_co, &hwsim2_mtx, 0);
                callout_init_mtx(&hwsim2_cmd_co, &hwsim2_mtx, 0);

                hwsim2_running = true;

                mtx_lock(&hwsim2_mtx);
                callout_reset_sbt(&hwsim2_sensor_co,
                    100 * SBT_1MS, 0, hwsim2_sensor_cb, NULL, 0);
                mtx_unlock(&hwsim2_mtx);

                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "sensor",
                    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
                    NULL, 0, hwsim2_sysctl_sensor, "IU",
                    "Oscillating sensor value");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "result",
                    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
                    NULL, 0, hwsim2_sysctl_result, "IU",
                    "Last command result");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "status",
                    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
                    NULL, 0, hwsim2_sysctl_status, "IU",
                    "Command status register");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "do_command",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0, hwsim2_sysctl_do_command, "IU",
                    "Trigger a simulated command with this data");
                SYSCTL_ADD_UINT(NULL,
                    SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "delay_ms", CTLFLAG_RW,
                    &hwsim2_delay_ms, 0,
                    "Command processing delay in ms");
                SYSCTL_ADD_BOOL(NULL,
                    SYSCTL_STATIC_CHILDREN(_dev_hwsim2),
                    OID_AUTO, "inject_error", CTLFLAG_RW,
                    &hwsim2_inject_error, 0,
                    "Set to true to inject an error on next command");

                printf("hwsim2: loaded, %d bytes at %p\n",
                    HWSIM2_SIZE, hwsim2_buf);
                return (0);

        case MOD_UNLOAD:
                mtx_lock(&hwsim2_mtx);
                hwsim2_running = false;
                callout_stop(&hwsim2_sensor_co);
                callout_stop(&hwsim2_cmd_co);
                mtx_unlock(&hwsim2_mtx);

                callout_drain(&hwsim2_sensor_co);
                callout_drain(&hwsim2_cmd_co);

                mtx_destroy(&hwsim2_mtx);

                if (hwsim2_buf != NULL) {
                        free(hwsim2_buf, M_HWSIM2);
                        hwsim2_buf = NULL;
                }
                printf("hwsim2: unloaded\n");
                return (0);

        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t hwsim2_mod = {
        "hwsim2",
        hwsim2_modevent,
        NULL
};

DECLARE_MODULE(hwsim2, hwsim2_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(hwsim2, 1);
