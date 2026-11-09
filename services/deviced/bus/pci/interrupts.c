/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

//#define __TRACE

#include <bus/pci/interrupts.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <bus/pci/config.h>
#include <ddk/acpi.h>
#include <ddk/interrupts/interrupt.h>
#include <ddk/utils.h>

static inline int
__SwizzleInterruptPin(
    _In_ int device,
    _In_ int pin)
{
    // The pin must be in the range 1 (INTA) through 4 (INTD).
    return (((pin - 1) + device) % 4) + 1;
}

static oserr_t
__QueryAcpiInterrupt(
    _In_  PciDevice_t*             device,
    _In_  unsigned int             pin,
    _Out_ struct AcpiPciInterrupt* result)
{
    struct AcpiPciQuery query = { .Type = AcpiPciQueryInterrupt };
    union AcpiPciResult response;
    PciDevice_t*        bridge;
    unsigned int        count = 0;
    unsigned int        index;
    oserr_t             status;

    query.RootId = device->Host->AcpiRootId;
    query.Address.Device = device->Slot;
    query.Address.Function = device->Function;
    query.Pin = pin;

    // The query holds the device address and, separately, the bridges on the path
    // from the root down to it. This is how the kernel finds the namespace bus the
    // device sits on. Bus numbers cannot be used for this, as they do not point to
    // AML nodes and are not unique across segments.
    for (bridge = device->Parent; bridge != device->Host->RootDevice; bridge = bridge->Parent) {
        if (!bridge || count == ACPI_PCI_MAX_BRIDGES) {
            return OS_EINVALPARAMS;
        }
        count++;
    }
    
    query.BridgeCount = count;
    
    index = count;
    for (bridge = device->Parent; index > 0; bridge = bridge->Parent) {
        index--;
        query.Bridges[index].Device = bridge->Slot;
        query.Bridges[index].Function = bridge->Function;
    }
    
    status = AcpiQueryPci(&query, &response);
    if (status == OS_EOK) {
        *result = response.Interrupt;
    }
    return status;
}

void
PciResolveInterruptLineAndPin(
    _In_ PciDevice_t* parent,
    _In_ int          bus,
    _In_ int          slot,
    _In_ int          function,
    _In_ PciDevice_t* pciDevice)
{
    PciDevice_t*            iterator;
    unsigned int            pin;
    unsigned int            flags;
    int                     line;
    oserr_t                 status;
    struct AcpiPciInterrupt route;

    TRACE("PciResolveInterruptLineAndPin(bus=%i, slot=%i, function=%i)",
          bus, slot, function);

#if defined(__i386__) || defined(__amd64__)
    pciDevice->InterruptLine = pciDevice->Header->InterruptLine;
#else
    pciDevice->InterruptLine = INTERRUPT_NONE;
#endif

    if (parent->Host->Operations->ResolveInterrupt != NULL) {
        pciDevice->InterruptLine = INTERRUPT_NONE;
        pin = pciDevice->Header->InterruptPin;
        iterator = pciDevice;

        if (pin == 0 || pin > 4) {
            TRACE("PciResolveInterruptLineAndPin no usable INTx pin (%u)", pin);
            return;
        }

        // Convert the pin at each bridge to the pin seen on the root bus.
        while (iterator->Parent != parent->Host->RootDevice) {
            pin = __SwizzleInterruptPin((int)iterator->Slot, (int)pin);
            iterator = iterator->Parent;
        }

        status = parent->Host->Operations->ResolveInterrupt(
            parent->Host,
            iterator->Bus,
            iterator->Slot,
            iterator->Function,
            pin,
            &line,
            &flags
        );
        if (status != OS_EOK) {
            WARNING("PciResolveInterruptLineAndPin host route failed for %i:%i.%i pin %u: %u",
                bus, slot, function, pin, status);
            return;
        }

        pciDevice->InterruptLine = line;
        pciDevice->AcpiConform = flags;
        TRACE("PciResolveInterruptLineAndPin resolved line=%i, flags=0x%x", line, flags);
        return;
    }

    if (parent->Host->AcpiRootId != 0) {
        // InterruptLine is an 8 bit hint from firmware and cannot hold an ARM
        // GSI. Reset it, so a device without a route never looks like it has
        // an interrupt we can use.
        pciDevice->InterruptLine = INTERRUPT_NONE;
        pin = pciDevice->Header->InterruptPin;
        if (pin == 0 || pin > 4) {
            return;
        }

        for (iterator = pciDevice; iterator != parent->Host->RootDevice; iterator = iterator->Parent) {
            status = __QueryAcpiInterrupt(iterator, pin, &route);
            if (status == OS_EOK) {
                pciDevice->InterruptLine = route.Line;
                pciDevice->AcpiConform = route.Conform;
                // The GSI is not written to configuration register 0x3c, as it
                // would be cut off. Drivers get the full line from the device
                // description.
                return;
            }

            if (status != OS_ENOENT) {
                WARNING("PCI INTx: root %u device %u:%u.%u route rejected (%u)",
                    parent->Host->AcpiRootId, iterator->Bus, iterator->Slot,
                    iterator->Function, status);
                return;
            }
            
            pin = __SwizzleInterruptPin(iterator->Slot, pin);
        }
    }
}
