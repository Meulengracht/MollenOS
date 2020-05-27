/* MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Acpi Interrupt Interface
 *   - Implementation for using acpi as a backend for interrupt routings.
 */

#define __MODULE "ACPI"
//#define __TRACE

#include <acpiinterface.h>
#include <assert.h>
#include <debug.h>
#include <ddk/device.h>
#include <interrupts.h>

/* AcpiGetPolarityMode
 * Returns whether or not the polarity is Active Low or Active High.
 * For Active Low = 1, Active High = 0 */
int
AcpiGetPolarityMode(
    _In_ uint16_t IntiFlags,
    _In_ int      Source)
{
    // Parse the different polarity flags
    switch (IntiFlags & ACPI_MADT_POLARITY_MASK) {
        case ACPI_MADT_POLARITY_CONFORMS: {
            if (Source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_POLARITY_ACTIVE_HIGH:
            return 0;
        case ACPI_MADT_POLARITY_ACTIVE_LOW:
            return 1;
    }
    return 0;
}

/* AcpiGetTriggerMode
 * Returns whether or not the trigger mode of the interrup is level or edge.
 * For Level = 1, Edge = 0 */
int
AcpiGetTriggerMode(
    _In_ uint16_t IntiFlags,
    _In_ int      Source)
{
    // Parse the different trigger mode flags
    switch (IntiFlags & ACPI_MADT_TRIGGER_MASK) {
        case ACPI_MADT_TRIGGER_CONFORMS: {
            if (Source == (int)AcpiGbl_FADT.SciInterrupt)
                return 1;
            else
                return 0;
        } break;
        case ACPI_MADT_TRIGGER_EDGE:
            return 0;
        case ACPI_MADT_TRIGGER_LEVEL:
            return 1;
    }
    return 0;
}

/* ConvertAcpiFlagsToConformFlags
 * Converts acpi interrupt flags to the system interrupt conform flags. */
unsigned int
ConvertAcpiFlagsToConformFlags(
    _In_ uint16_t IntiFlags,
    _In_ int      Source)
{
    unsigned int ConformFlags = 0;

    if (AcpiGetPolarityMode(IntiFlags, Source) == 1) {
        ConformFlags |= INTERRUPT_ACPICONFORM_POLARITY;
    }
    if (AcpiGetTriggerMode(IntiFlags, Source) == 1) {
        ConformFlags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    return ConformFlags;
}

/* AcpiDeriveInterrupt
 * Derives an interrupt by consulting the bus of the device, 
 * and spits out flags in AcpiConform and returns irq */
int
AcpiDeriveInterrupt(
    _In_  unsigned int Bus, 
    _In_  unsigned int Device,
    _In_  int          Pin,
    _Out_ unsigned int*     AcpiConform)
{
    AcpiDevice_t* Dev;
    unsigned      rIndex  = (Device * 4) + (Pin - 1);

    // Trace
    TRACE("AcpiDeriveInterrupt(Bus %" PRIuIN ", Device %" PRIuIN ", Pin %" PRIiIN ")", Bus, Device, Pin);

    // Start by checking if we can find the
    // routings by checking the given device
    Dev = AcpiDeviceLookupBusRoutings(Bus);

    // Make sure there was a bus device for it
    if (Dev != NULL) {
        TRACE("Found bus-device <%s>, accessing index %" PRIuIN "", 
            &Dev->HId[0], rIndex);
        if (Dev->Routings->ActiveIrqs[rIndex] != INTERRUPT_NONE
            && Dev->Routings->InterruptEntries[rIndex] != NULL) {
            PciRoutingEntry_t* RoutingEntry = NULL;

            // Lookup entry in list
            foreach(i, Dev->Routings->InterruptEntries[rIndex]) {
                RoutingEntry = (PciRoutingEntry_t*)i->value;
                if (RoutingEntry->Irq == Dev->Routings->ActiveIrqs[rIndex]) {
                    break;
                }
            }
            assert(RoutingEntry != NULL);

            // Update IRQ Information
            *AcpiConform = INTERRUPT_ACPICONFORM_PRESENT;
            if (RoutingEntry->Trigger == ACPI_LEVEL_SENSITIVE) {
                *AcpiConform |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
            }

            if (RoutingEntry->Polarity == ACPI_ACTIVE_LOW) {
                *AcpiConform |= INTERRUPT_ACPICONFORM_POLARITY;
            }

            if (RoutingEntry->Shareable != 0) {
                *AcpiConform |= INTERRUPT_ACPICONFORM_SHAREABLE;
            }

            if (RoutingEntry->Fixed != 0) {
                *AcpiConform |= INTERRUPT_ACPICONFORM_FIXED;
            }

            // Return found interrupt
            TRACE("Found interrupt %" PRIiIN "", RoutingEntry->Irq);
            return RoutingEntry->Irq;
        }
    }

    TRACE("No interrupt found");
    return INTERRUPT_NONE;
}
