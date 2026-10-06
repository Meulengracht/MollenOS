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
 */

#ifndef DEVICED_FIRMWARE_RP1_H
#define DEVICED_FIRMWARE_RP1_H
#include "pci.h"
#include <bus/pci/bars.h>

#define FDT_RP1_MAX_REGISTERS 8
#define FDT_RP1_MAX_INTERRUPTS 8
#define FDT_RP1_INTERRUPT_COUNT 61

/** A CPU physical resource, checked against an assigned RP1 memory BAR. */
struct FdtRp1Range {
    uint64_t Base;
    uint64_t Length;
};

/** RP1-local interrupt identity. Number is never a GIC line or PCI INTx pin. */
struct FdtRp1Interrupt {
    uint32_t Controller;
    uint32_t Number;
    uint32_t Type; // DeviceTree rising edge (1) or high level (4)
};

/** One enabled child. Strings and NodeOffset refer to the retained firmware blob.
 * The node retains device-specific clocks, resets, DMA and pin configuration. */
struct FdtRp1Device {
    const char* Name;
    const char* Compatible;
    uint32_t CompatibleLength;
    uint32_t NodeOffset;
    uint32_t Controller;
    struct FdtRp1Range Registers[FDT_RP1_MAX_REGISTERS];
    uint32_t RegisterCount;
    struct FdtRp1Interrupt Interrupts[FDT_RP1_MAX_INTERRUPTS];
    uint32_t InterruptCount;
};

typedef void (*FdtRp1DeviceFn)(const struct FdtRp1Device*, void*);

/**
 * @brief Enumerates direct RP1 children beneath this exact host, validating all
 * children before callbacks. Assigned BARs use CPU physical addresses. Disabled
 * nodes are omitted; malformed or unsupported resources fail without callbacks.
 * @param bars Six PCI BAR descriptions; only assigned memory BARs are usable.
 */
extern oserr_t
FdtEnumerateRp1Children(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _In_ FdtRp1DeviceFn callback,
    _In_ void* context);


#endif
