/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * ARM64 boot device pool selection
 */

#include "device-pool.h"
#include "private.h"
#include "../devicetree/device-pool-fdt.h"
#include <debug.h>
#include <string.h>

// Saved on the boot CPU before the MMU turns on, then handed off to generic
// memory setup once early boot finishes. Length stays zero when no pool was
// found. If selection fails before logging is ready, g_poolReason and
// g_poolNode hold the details until MmuGetDevicePool runs (g_poolNode points
// straight into the DTB in firmware RAM).
static PlatformDevicePool_t g_pool;
static oserr_t              g_poolStatus = OS_ENOTSUPPORTED;
static const char*          g_poolReason = "no pool was selected";
static const char*          g_poolNode = "";

oserr_t
Arm64PrepareDevicePool(
    _In_ const struct VBoot* boot)
{
    // ARM64 devices are often non-coherent with CPU caches, so the pool is
    // always mapped as uncached Normal RAM.
    PlatformDevicePool_t pool = { .Cached = false };

    g_pool = pool;
    g_poolReason = "";
    g_poolNode = "";
    if (boot == NULL) {
        g_poolReason = "no boot information";
        g_poolStatus = OS_EINVALPARAMS;
        return g_poolStatus;
    }

#if defined(__OSCONFIG_DEVICETREE_SUPPORT)
    g_poolStatus = Arm64FindDevicePool(
        boot,
        ARM64_DEVICE_POOL_SIZE,
        ARM64_BLOCK_SIZE,
        &pool,
        &g_poolReason,
        &g_poolNode
    );
#else
    // On ACPI platforms without a Device Tree, place a 4 MiB pool below 2 GiB
    // so both 31-bit and 32-bit DMA devices can reach it.
    g_poolStatus = SystemMemoryFitDeviceRegion(
        boot,
        ARM64_DEVICE_POOL_SIZE,
        ARM64_ACPI_DEVICE_POOL_LIMIT,
        ARM64_BLOCK_SIZE,
        &pool.Base,
        &pool.Length
    );
    if (g_poolStatus != OS_EOK) {
        g_poolReason = "no free RAM below 2 GiB fits the pool";
    }
#endif
    if (g_poolStatus != OS_EOK) {
        return g_poolStatus;
    }

    g_pool = pool;
    return OS_EOK;
}

PlatformDevicePool_t
Arm64SelectedDevicePool(void)
{
    return g_pool;
}

oserr_t
MmuGetDevicePool(
    _Out_ PlatformDevicePool_t* poolOut)
{
    // Selection already happened before the MMU enabled. Logging is up now,
    // so print any saved failure once here and return the result.
    if (poolOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (g_poolStatus != OS_EOK) {
        ERROR("MmuGetDevicePool: device pool unavailable (%u), device allocations will fail: %s %s",
              g_poolStatus, g_poolReason, g_poolNode);
        return g_poolStatus;
    }

    *poolOut = g_pool;
    return OS_EOK;
}
