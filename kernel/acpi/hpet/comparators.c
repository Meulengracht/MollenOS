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

//#define __TRACE

#include <debug.h>
#include <interrupts.h>
#include "private.h"

extern HPET_t g_hpet;

static void
__ReadComparatorValue(
        _In_ HPET_t*       hpet,
        _In_ size_t        offset,
        _In_ UInteger64_t* value)
{
#if __BITS == 64
    HPET_READ_64(hpet, offset, value->QuadPart);
#else
    HPET_READ_32(hpet, offset, value->u.LowPart);
    value->u.HighPart = 0;
#endif
}

static void
__WriteComparatorValue(
        _In_ HPET_t*       hpet,
        _In_ size_t        offset,
        _In_ UInteger64_t* value)
{
#if __BITS == 64
    HPET_WRITE_64(hpet, offset, value->QuadPart);
#else
    HPET_WRITE_32(hpet, offset, value->u.LowPart);
#endif
}

irqstatus_t
HpInterrupt(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    reg32_t interruptStatus = 0;
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // Initiate values
    HPET_READ_32(&g_hpet, HPET_REGISTER_INTSTATUS, interruptStatus);

    // Trace
    TRACE("Interrupt - Status 0x%" PRIxIN "", interruptStatus);

    // Was the interrupt even from this controller?
    if (!interruptStatus) {
        return IRQSTATUS_NOT_HANDLED;
    }

    // Iterate the port-map and check if the interrupt
    // came from that timer
    for (int i = 0; i < HPET_MAX_COMPARATORS; i++) {
        if (interruptStatus & (1 << i) && g_hpet.Timers[i].Enabled) {
            if (!g_hpet.Timers[i].PeriodicSupport) {
                // Non-periodic timer fired, what now?
                WARNING("HPET::NON-PERIODIC TIMER FIRED");
            }
        }
    }

    // Write clear interrupt register and return
    HPET_WRITE_32(&g_hpet, HPET_REGISTER_INTSTATUS, interruptStatus);
    return IRQSTATUS_HANDLED;
}

oserr_t
__HPETInitializeComparator(
        _In_ HPET_t* hpet,
        _In_ int     index)
{
    HPETComparator_t* comparator    = &hpet->Timers[index];
    size_t            configuration = 0;
    size_t            interruptMap  = 0;

    TRACE("__HPETInitializeComparator(%i)", index);

    // Read values
    HPET_READ_32(hpet, HPET_TIMER_CONFIG(index), configuration);

    // Fixup the interrupt map that won't be present in some cases for timer
    // 0 and 1, when legacy has been enabled
    if (hpet->LegacySupport && (index <= 1)) {
        // 0 = Irq 0
        // 1 = Irq 8
        interruptMap = 1 << (index * 8);
    } else {
        HPET_READ_32(hpet, HPET_TIMER_CONFIG(index) + 4, interruptMap);
    }

    if (configuration == 0xFFFFFFFF || interruptMap == 0) {
        WARNING("__HPETInitializeComparator configuration=0x%x, map=0x%x", LODWORD(configuration), LODWORD(interruptMap));
        return OS_EUNKNOWN;
    }

    // Setup basic information
    comparator->Present      = 1;
    comparator->Irq          = INTERRUPT_NONE;
    comparator->InterruptMap = LODWORD(interruptMap);

    // Store some features
    if (configuration & HPET_TIMER_CONFIG_64BITMODESUPPORT) {
        comparator->Is64Bit = 1;
    }
    if (configuration & HPET_TIMER_CONFIG_FSBSUPPORT) {
        comparator->MsiSupport = 1;
    }
    if (configuration & HPET_TIMER_CONFIG_PERIODICSUPPORT) {
        comparator->PeriodicSupport = 1;
    }

    // Process timer configuration and disable it for now
    configuration &= ~(HPET_TIMER_CONFIG_IRQENABLED | HPET_TIMER_CONFIG_POLARITY | HPET_TIMER_CONFIG_FSBMODE);
    HPET_WRITE_32(hpet, HPET_TIMER_CONFIG(index), configuration);
    return OS_EOK;
}

static void
__AllocateInterrupt(
        _In_ HPETComparator_t* comparator)
{
    DeviceInterrupt_t hpetInterrupt;
    int               i, j;

    memset(&hpetInterrupt, 0, sizeof(DeviceInterrupt_t));
    hpetInterrupt.ResourceTable.Handler = HpInterrupt;
    hpetInterrupt.Context               = comparator;
    hpetInterrupt.Line                  = INTERRUPT_NONE;
    hpetInterrupt.Pin                   = INTERRUPT_NONE;
    TRACE("__AllocateInterrupt Gathering interrupts from irq-map 0x%" PRIxIN "", comparator->InterruptMap);

    // From the interrupt map, calculate possible int's
    for (i = 0, j = 0; i < 32; i++) {
        if (comparator->InterruptMap & (1 << i)) {
            hpetInterrupt.Vectors[j++] = i;
            if (j == INTERRUPT_MAXVECTORS) {
                break;
            }
        }
    }

    // Place an end marker
    if (j != INTERRUPT_MAXVECTORS) {
        hpetInterrupt.Vectors[j] = INTERRUPT_NONE;
    }

    // Handle MSI interrupts > normal
    if (comparator->MsiSupport) {
        comparator->Interrupt  = InterruptRegister(
                &hpetInterrupt,
                INTERRUPT_MSI | INTERRUPT_KERNEL
        );
        comparator->MsiAddress = (reg32_t)hpetInterrupt.MsiAddress;
        comparator->MsiValue   = (reg32_t)hpetInterrupt.MsiValue;
        TRACE("__AllocateInterrupt Using msi interrupts (untested)");
    }
    else {
        comparator->Interrupt = InterruptRegister(
                &hpetInterrupt,
                INTERRUPT_VECTOR | INTERRUPT_KERNEL
        );
        comparator->Irq       = hpetInterrupt.Line;
        TRACE("__AllocateInterrupt Using irq interrupt %" PRIiIN "", comparator->Irq);
    }
}

oserr_t
HPETComparatorStart(
        _In_ int      index,
        _In_ uint64_t frequency,
        _In_ int      periodic,
        _In_ int      legacyIrq)
{
    HPETComparator_t* comparator = &g_hpet.Timers[index];
    UInteger64_t      now;
    UInteger64_t      delta;
    size_t            tempValue;
    size_t            tempValue2;

    TRACE("HPETComparatorStart(index=%i, frequency=%" PRIuIN ", periodic=%i)",
          index, frequency, periodic);

    // Calculate the delta
    delta.QuadPart = (FSEC_PER_SEC / (uint64_t)g_hpet.Period) / frequency;
    if (delta.QuadPart < g_hpet.TickMinimum) {
        delta.QuadPart = g_hpet.TickMinimum;
    }
    TRACE("HPETComparatorStart delta=0x%x", delta.u.LowPart);

    // Allocate interrupt for timer?
    if (legacyIrq != INTERRUPT_NONE) {
        comparator->Irq = legacyIrq;
    }

    if (comparator->Irq == INTERRUPT_NONE) {
        __AllocateInterrupt(comparator);
    }

    // At this point all resources are ready
    comparator->Enabled = 1;

    // Stop main timer and calculate the next irq
    __HPETStop();
    __HPETReadMainCounter(&now);
    now.QuadPart += delta.QuadPart;

    // Process configuration
    // We must initially configure as 32 bit as I read some hw doesn't handle 64 bit that well
    HPET_READ_32(&g_hpet, HPET_TIMER_CONFIG(index), tempValue);
    tempValue |= HPET_TIMER_CONFIG_IRQENABLED | HPET_TIMER_CONFIG_32BITMODE;

    // Set some extra bits if periodic
    if (comparator->PeriodicSupport && periodic) {
        TRACE("HPETComparatorStart configuring for periodic");
        tempValue |= HPET_TIMER_CONFIG_PERIODIC;
        tempValue |= HPET_TIMER_CONFIG_SET_CMP_VALUE;
    }

    // Set interrupt vector
    // MSI must be set to edge-triggered
    if (comparator->MsiSupport) {
        tempValue |= HPET_TIMER_CONFIG_FSBMODE;
        HPET_WRITE_32(&g_hpet, HPET_TIMER_FSB(index), comparator->MsiValue);
        HPET_WRITE_32(&g_hpet, HPET_TIMER_FSB(index) + 4, comparator->MsiAddress);
    } else {
        tempValue |= HPET_TIMER_CONFIG_IRQ(comparator->Irq);
        if (comparator->Irq > 15) {
            tempValue |= HPET_TIMER_CONFIG_POLARITY;
        }
    }

    // Update configuration and comparator
    tempValue2 = (1 << index);
    HPET_WRITE_32(&g_hpet, HPET_REGISTER_INTSTATUS, tempValue2);
    TRACE("HPETComparatorStart writing config value 0x%" PRIxIN "", tempValue);
    HPET_WRITE_32(&g_hpet, HPET_TIMER_CONFIG(index), tempValue);
    __WriteComparatorValue(&g_hpet, HPET_TIMER_COMPARATOR(index), &now);
    if (comparator->PeriodicSupport && periodic) {
        __WriteComparatorValue(&g_hpet, HPET_TIMER_COMPARATOR(index), &delta);
    }
    __HPETStart();
    return OS_EOK;
}

oserr_t
HPETComparatorStop(
        _In_ int index)
{
    size_t tempValue = 0;
    HPET_WRITE_32(&g_hpet, HPET_TIMER_CONFIG(index), tempValue);
    __WriteComparatorValue(
            &g_hpet,
            HPET_TIMER_COMPARATOR(index),
            &(UInteger64_t){ .QuadPart = 0 }
    );
    return OS_EOK;
}

