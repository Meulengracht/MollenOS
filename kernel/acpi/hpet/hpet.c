/**
 * Copyright 2022, Philip Meulengracht
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
 */

// TODO: Disable legacy routings and just don't enable RTC/PIT.

//#define __TRACE

#include <acpiinterface.h>
#include <arch/interrupts.h>
#include <component/timer.h>
#include <debug.h>
#include "private.h"

static void __HPETGetCount(void*, UInteger64_t*);
static void __HPETGetFrequency(void*, UInteger64_t*);

static SystemTimerOperations_t g_hpetOperations = {
        .Enable = NULL,
        .Configure = NULL,
        .GetFrequencyRange = NULL,
        .Recalibrate = NULL,
        .Read = __HPETGetCount,
        .GetFrequency = __HPETGetFrequency,
};
HPET_t g_hpet = { 0 };

ACPI_TABLE_HPET*
__GetHPETTable(void)
{
    ACPI_TABLE_HEADER* header;
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &header))) {
        return (ACPI_TABLE_HPET*)header;
    }
    return NULL;
}

static oserr_t
__RemapHPET(
        _In_ HPET_t*   hpet,
        _In_ uintptr_t address)
{
    uintptr_t remappedAddress;
    oserr_t oserr = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &(struct MemorySpaceMapOptions) {
                .Pages = &address,
                .Length = GetMemorySpacePageSize(),
                .Mask = __MASK,
                .Flags = MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_NOCACHE,
                .PlacementFlags = MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED
            },
            &remappedAddress
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    hpet->BaseAddress = remappedAddress;
    return OS_EOK;
}

static oserr_t
__StartupHPET(
        _In_ HPET_t* hpet)
{
    reg32_t configValue;

    // AMD SB700 Systems initialise HPET on first register access,
    // wait for it to set up HPET, its config register reads 0xFFFFFFFF meanwhile
    for (int i = 0; i < 10000; i++) {
        HPET_READ_32(hpet, HPET_REGISTER_CONFIG, configValue);
        if (configValue != 0xFFFFFFFF) {
            break;
        }
    }

    // Did system fail to initialize
    if (configValue == 0xFFFFFFFF) {
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

static oserr_t
__ProcessCapabilities(
        _In_ HPET_t* hpet)
{
    reg32_t caps;
    TRACE("__ProcessCapabilities()");

    HPET_READ_32(hpet, HPET_REGISTER_CAPABILITIES, caps);

    // Get the 64 bit capability, it doesn't matter whether we're in
    // 32 bit mode or not, we just want to use the full 64 bit counter if
    // available.
    hpet->Is64Bit = (caps & HPET_64BITSUPPORT) ? true : false;
    hpet->LegacySupport = (caps & HPET_LEGACYMODESUPPORT) ? true : false;
    hpet->TimerCount = (int)HPET_TIMERCOUNT(caps);
    TRACE("__ProcessCapabilities caps=0x%x, timer-count=%i", caps, hpet->TimerCount);

    if (hpet->LegacySupport && hpet->TimerCount < 2) {
        ERROR("__ProcessCapabilities failed to initialize HPET, legacy is available but not enough timers.");
        return OS_EDEVFAULT;
    }

    // Sanitize the number of timers, must be above 0
    if (hpet->TimerCount == 0) {
        ERROR("__ProcessCapabilities there was no timers present in HPET");
        return OS_EDEVFAULT;
    }

    // Enable the legacy routings if its are supported
    if (hpet->LegacySupport) {
        HPET_READ_32(hpet, HPET_REGISTER_CONFIG, caps);
        caps |= HPET_CONFIG_LEGACY;
        HPET_WRITE_32(hpet, HPET_REGISTER_CONFIG, caps);
    }
    return OS_EOK;
}

void
HPETInitialize(void)
{
    ACPI_TABLE_HPET* hpetTable;
    oserr_t          oserr;
    TRACE("HPETInitialize()");

    hpetTable = __GetHPETTable();
    if (!hpetTable) {
        WARNING("HPETInitialize no HPET detected.");
        return;
    }

    // Store some values from the ACPI header for easier access.
    g_hpet.TickMinimum = hpetTable->MinimumTick;

    TRACE("HPETInitialize phys=0x%" PRIxIN ", sequence=%u",
          (uintptr_t)(hpetTable->Address.Address), hpetTable->Sequence);

    oserr = __RemapHPET(&g_hpet, (uintptr_t)hpetTable->Address.Address);
    if (oserr != OS_EOK) {
        ERROR("HPETInitialize failed to remap hpet device address: %u", oserr);
        return;
    }

    oserr = __StartupHPET(&g_hpet);
    if (oserr != OS_EOK) {
        ERROR("HPETInitialize failed to initialize the hpet device");
        return;
    }

    // Check the period for a sane value
    HPET_READ_32(&g_hpet, HPET_REGISTER_CAPABILITIES + 4, g_hpet.Period);
    TRACE("HPETInitialize mintick=0x%x, period=0x%x", g_hpet.TickMinimum, g_hpet.Period);

    if ((g_hpet.Period == 0) || (g_hpet.Period > HPET_MAXPERIOD)) {
        ERROR("HPETInitialize failed to initialize HPET, period is invalid.");
        return;
    }

    // Process the capabilities
    oserr = __ProcessCapabilities(&g_hpet);
    if (oserr != OS_EOK) {
        return;
    }

    // Loop through all comparators and configurate them
    for (int i = 0; i < g_hpet.TimerCount; i++) {
        if (__HPETInitializeComparator(&g_hpet, i) != OS_EOK) {
            ERROR("HPETInitialize HPET Failed to initialize comparator %i", i);
            g_hpet.Timers[i].Present = 0;
        }
    }

    // Reset the HPET counter
    __HPETWriteMainCounter(&(UInteger64_t) { .QuadPart = 0 });

    // Register the HPET as an available HPC
    oserr = SystemTimerRegister(
            "acpi-hpet",
            &g_hpetOperations,
            SystemTimeAttributes_COUNTER | SystemTimeAttributes_HPC,
            &g_hpet
    );
    if (oserr != OS_EOK) {
        WARNING("HPETInitialize failed to register platform timer");
    }

    // Select it as primary source
    SystemTimerRefreshTimers();
}

bool
HPETIsPresent(void)
{
    ACPI_TABLE_HPET* hpetTable;

    // Short-cut the check in case we've already set up the HPET.
    if (g_hpet.BaseAddress != 0) {
        return true;
    }

    // Otherwise this is called pre hpet-init. So we check for the
    // ACPI table.
    hpetTable = __GetHPETTable();
    if (hpetTable == NULL) {
        return false;
    }
    return hpetTable->Address.Address != 0;
}

bool
HPETIsEmulatingLegacyController(void)
{
    size_t hpetConfig;

    if (!HPETIsPresent()) {
        return false;
    }

    if (g_hpet.BaseAddress == 0) {
        ERROR("HPETIsEmulatingLegacyController: called before HPETInitialize()");
        return false;
    }

    HPET_READ_32(&g_hpet, HPET_REGISTER_CONFIG, hpetConfig);
    if (hpetConfig & HPET_CONFIG_LEGACY) {
        return true;
    }
    return false;
}

void
__HPETStop(void)
{
    reg32_t config;
    HPET_READ_32(&g_hpet, HPET_REGISTER_CONFIG, config);
    config &= ~HPET_CONFIG_ENABLED;
    HPET_WRITE_32(&g_hpet, HPET_REGISTER_CONFIG, config);

    // Wait for it to actually stop
    HPET_READ_32(&g_hpet, HPET_REGISTER_CONFIG, config);
    while (config & HPET_CONFIG_ENABLED) {
        HPET_READ_32(&g_hpet, HPET_REGISTER_CONFIG, config);
    }
}

void
__HPETStart(void)
{
    reg32_t config;
    HPET_READ_32(&g_hpet, HPET_REGISTER_CONFIG, config);
    config |= HPET_CONFIG_ENABLED;
    HPET_WRITE_32(&g_hpet, HPET_REGISTER_CONFIG, config);
}

void
__HPETReadMainCounter(
        _In_ UInteger64_t* value)
{
    // Reuse the method for reading the main counter
    __HPETGetCount(&g_hpet, value);
}

void
__HPETWriteMainCounter(
        _In_ UInteger64_t* value)
{
    irqstate_t irqState = InterruptDisable();
    __HPETStop();
#if __BITS == 64
    if (g_hpet.Is64Bit) {
        HPET_WRITE_64(&g_hpet, HPET_REGISTER_MAINCOUNTER, value->QuadPart);
    } else {
        HPET_WRITE_32(&g_hpet, HPET_REGISTER_MAINCOUNTER, value->u.LowPart);
    }
#else
    HPET_WRITE_32(&g_hpet, HPET_REGISTER_MAINCOUNTER, value->u.LowPart);
    if (g_hpet.Is64Bit) {
        HPET_WRITE_32(&g_hpet, HPET_REGISTER_MAINCOUNTER + 4, value->u.HighPart);
    }
#endif
    __HPETStart();
    InterruptRestoreState(irqState);
}

static void
__HPETGetCount(
        _In_ void*         context,
        _In_ UInteger64_t* value)
{
    HPET_t* hpet = context;
#if __BITS == 64
    if (hpet->Is64Bit) {
        HPET_READ_64(hpet, HPET_REGISTER_MAINCOUNTER, value->QuadPart);
    } else {
        HPET_READ_32(hpet, HPET_REGISTER_MAINCOUNTER, value->u.LowPart);
        value->u.HighPart = 0;
    }
#else
    if (hpet->Is64Bit) {
        // This requires us to synchronize with the upper 32 bits from each read to ensure
        // we don't encounter a rollover. Keep a direct pointer for more convenient reading
        volatile reg32_t* counterRegister = (volatile reg32_t*)(hpet->BaseAddress + HPET_REGISTER_MAINCOUNTER);
        do {
            value->u.HighPart = *(counterRegister + 1);
            value->u.LowPart  = *counterRegister;
        } while (value->u.HighPart != *(counterRegister + 1));
    } else {
        value->u.HighPart = 0;
        HPET_READ_32(hpet, HPET_REGISTER_MAINCOUNTER, value->u.LowPart);
    }
#endif
}

static void
__HPETGetFrequency(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    HPET_t* hpet = context;
    frequency->QuadPart = FSEC_PER_SEC / (uint64_t)hpet->Period;
}
