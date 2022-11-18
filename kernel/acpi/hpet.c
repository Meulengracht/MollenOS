/**
 * Copyright 2017, Philip Meulengracht
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
 *
 * High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */

#define __MODULE "HPET"
//#define __TRACE

#include <arch/io.h>
#include <component/timer.h>
#include <memoryspace.h>
#include <interrupts.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>
#include <stdlib.h>

typedef struct HpetComparator {
    uuid_t Interrupt;
    int	   Present;
    int	   Enabled;
    int	   SystemTimer;
    int	   Is64Bit;
    int	   MsiSupport;
    int	   PeriodicSupport;

    // Normal interrupt
    int		Irq;
    reg32_t	InterruptMap;

    // Msi interrupt
    reg32_t	MsiAddress;
    reg32_t	MsiValue;
} HpetComparator_t;

typedef struct HpetController {
    uintptr_t        BaseAddress;
    HpetComparator_t Timers[HPET_MAXTIMERCOUNT];

    int	            Is64Bit;
    size_t          TickMinimum;
    size_t          Period;
    UInteger64_t Frequency;
    clock_t	        Clock;
} HpetController_t;

static void HpetGetCount(void*, UInteger64_t*);
static void HpetGetFrequency(void*, UInteger64_t*);

/**
 * Recalibrate is registered as a no-op as the TSC never needs to be calibrated again
 */
static SystemTimerOperations_t g_hpetOperations = {
        .Enable = NULL,
        .Configure = NULL,
        .GetFrequencyRange = NULL,
        .Recalibrate = NULL,
        .Read = HpetGetCount,
        .GetFrequency = HpetGetFrequency,
};
static HpetController_t g_hpet = { 0 };

#define HP_READ_32(Offset, ValueOut) ReadVolatileMemory((const volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)(ValueOut), 4)
#define HP_READ_64(Offset, ValueOut) ReadVolatileMemory((const volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)(ValueOut), 8)

#define HP_WRITE_32(Offset, Value)   WriteVolatileMemory((volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)&(Value), 4)
#define HP_WRITE_64(Offset, Value)   WriteVolatileMemory((volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)&(Value), 8)

static void
__ReadComparatorValue(
    _In_  size_t   offset,
    _Out_ reg64_t* valueOut)
{
#if __BITS == 64
    HP_READ_64(offset, valueOut);
#else
    *valueOut = 0;
    HP_READ_32(offset, (reg32_t*)valueOut);
#endif
}

static void
__WriteComparatorValue(
    _In_ size_t  offset,
    _In_ reg64_t value)
{
#if __BITS == 64
    HP_WRITE_64(offset, value);
#else
    HP_WRITE_32(offset, value);
#endif
}

static void
__StopHpet(void)
{
    size_t Config;
    HP_READ_32(HPET_REGISTER_CONFIG, &Config);
    Config &= ~HPET_CONFIG_ENABLED;
    HP_WRITE_32(HPET_REGISTER_CONFIG, Config);

    // Wait for it to actually stop
    HP_READ_32(HPET_REGISTER_CONFIG, &Config);
    while (Config & HPET_CONFIG_ENABLED) {
        HP_READ_32(HPET_REGISTER_CONFIG, &Config);
    }
}

static void
__StartHpet(void)
{
    size_t Config;
    HP_READ_32(HPET_REGISTER_CONFIG, &Config);
    Config |= HPET_CONFIG_ENABLED;
    HP_WRITE_32(HPET_REGISTER_CONFIG, Config);
}

bool
HPETIsPresent(void)
{
    if (g_hpet.BaseAddress != 0) {
        return true;
    }
    return false;
}

bool
HPETIsEmulatingLegacyController(void)
{
    if (g_hpet.BaseAddress != 0) {
        size_t hpetConfig;
        HP_READ_32(HPET_REGISTER_CONFIG, &hpetConfig);
        if (hpetConfig & HPET_CONFIG_LEGACY) {
            return true;
        }     
    }
    return false;
}

static void
__ReadMainCounter(
        _Out_ UInteger64_t* Value)
{
#if __BITS == 64
    if (g_hpet.Is64Bit) {
        HP_READ_64(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->QuadPart);
    } else {
        Value->QuadPart = 0;
        HP_READ_32(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->QuadPart);
    }
#else
    if (g_hpet.Is64Bit) {
        // This requires us to synchronize with the upper 32 bits from each read to ensure
        // we don't encounter a rollover. Keep a direct pointer for more convenient reading
        volatile reg32_t *Register = (volatile reg32_t*)(g_hpet.BaseAddress + HPET_REGISTER_MAINCOUNTER);
        do {
            Value->u.HighPart = *(Register + 1);
            Value->u.LowPart  = *Register;
        } while (Value->u.HighPart != *(Register + 1));
    } else {
        Value->u.HighPart = 0;
        HP_READ_32(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->u.LowPart);
    }
#endif
}

irqstatus_t
HpInterrupt(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    size_t InterruptStatus = 0;
    int i                  = 0;
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // Initiate values
    HP_READ_32(HPET_REGISTER_INTSTATUS, &InterruptStatus);

    // Trace
    TRACE("Interrupt - Status 0x%" PRIxIN "", InterruptStatus);

    // Was the interrupt even from this controller?
    if (!InterruptStatus) {
        return IRQSTATUS_NOT_HANDLED;
    }

    // Iterate the port-map and check if the interrupt
    // came from that timer
    for (i = 0; i < HPET_MAXTIMERCOUNT; i++) {
        if (InterruptStatus & (1 << i) && g_hpet.Timers[i].Enabled) {
            if (g_hpet.Timers[i].SystemTimer) {
                g_hpet.Clock++;
            }
            if (!g_hpet.Timers[i].PeriodicSupport) {
                // Non-periodic timer fired, what now?
                WARNING("HPET::NON-PERIODIC TIMER FIRED");
            }
        }
    }

    // Write clear interrupt register and return
    HP_WRITE_32(HPET_REGISTER_INTSTATUS, InterruptStatus);
    return IRQSTATUS_HANDLED;
}

static oserr_t
__InitializeComparator(
    _In_ int index,
    _In_ int legacyRoutings)
{
    HpetComparator_t* comparator    = &g_hpet.Timers[index];
    size_t            configuration = 0;
    size_t            interruptMap  = 0;

    TRACE("__InitializeComparator(%i)", index);

    // Read values
    HP_READ_32(HPET_TIMER_CONFIG(index), &configuration);
    
    // Fixup the interrupt map that won't be present in some cases for timer
    // 0 and 1, when legacy has been enabled
    if (legacyRoutings && (index <= 1)) {
        // 0 = Irq 0
        // 1 = Irq 8
        interruptMap = 1 << (index * 8);
    } else {
        HP_READ_32(HPET_TIMER_CONFIG(index) + 4, &interruptMap);
    }
    
    if (configuration == 0xFFFFFFFF || interruptMap == 0) {
        WARNING("__InitializeComparator configuration=0x%x, map=0x%x", LODWORD(configuration), LODWORD(interruptMap));
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
    HP_WRITE_32(HPET_TIMER_CONFIG(index), configuration);
    return OS_EOK;
}

static void
__AllocateInterrupt(
        _In_ HpetComparator_t* comparator)
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
    HpetComparator_t* comparator = &g_hpet.Timers[index];
    UInteger64_t      now;
    uint64_t          delta;
    size_t            tempValue;
    size_t            tempValue2;

    TRACE("HPETComparatorStart(index=%i, frequency=%" PRIuIN ", periodic=%i)",
          index, LODWORD(Frequency), periodic);

    // Calculate the delta
    delta = (uint64_t)g_hpet.Frequency.QuadPart / frequency;
    if (delta < g_hpet.TickMinimum) {
        delta = g_hpet.TickMinimum;
    }
    TRACE("HPETComparatorStart delta=0x%x", LODWORD(delta));

    // Stop main timer and calculate the next irq
    __StopHpet();
    __ReadMainCounter(&now);
    now.QuadPart += delta;
    
    // Allocate interrupt for timer?
    if (legacyIrq != INTERRUPT_NONE) {
        comparator->Irq = legacyIrq;
    }
    
    if (comparator->Irq == INTERRUPT_NONE) {
        __AllocateInterrupt(comparator);
    }
    
    // At this point all resources are ready
    comparator->Enabled = 1;

    // Process configuration
    // We must initially configure as 32 bit as I read some hw doesn't handle 64 bit that well
    HP_READ_32(HPET_TIMER_CONFIG(index), &tempValue);
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
        HP_WRITE_32(HPET_TIMER_FSB(index), comparator->MsiValue);
        HP_WRITE_32(HPET_TIMER_FSB(index) + 4, comparator->MsiAddress);
    } else {
        tempValue |= HPET_TIMER_CONFIG_IRQ(comparator->Irq);
        if (comparator->Irq > 15) {
            tempValue |= HPET_TIMER_CONFIG_POLARITY;
        }
    }

    // Update configuration and comparator
    tempValue2 = (1 << index);
    HP_WRITE_32(HPET_REGISTER_INTSTATUS, tempValue2);
    TRACE("HPETComparatorStart writing config value 0x%" PRIxIN "", tempValue);
    HP_WRITE_32(HPET_TIMER_CONFIG(index), tempValue);
    __WriteComparatorValue(HPET_TIMER_COMPARATOR(index), (reg64_t)now.QuadPart);
    if (comparator->PeriodicSupport && periodic) {
        __WriteComparatorValue(HPET_TIMER_COMPARATOR(index), delta);
    }
    __StartHpet();
    return OS_EOK;
}

oserr_t
HPETComparatorStop(
        _In_ int index)
{
    size_t tempValue = 0;
    HP_WRITE_32(HPET_TIMER_CONFIG(index), tempValue);
    __WriteComparatorValue(HPET_TIMER_COMPARATOR(index), 0);
    return OS_EOK;
}

ACPI_TABLE_HPET*
__GetHpetTable(void)
{
    ACPI_TABLE_HEADER* header;
    if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_HPET, 0, &header))) {
        return (ACPI_TABLE_HPET*)header;
    }
    return NULL;
}

void
HPETInitialize(void)
{
    ACPI_TABLE_HPET* hpetTable;
    int              legacy;
    oserr_t       osStatus;
    uintptr_t        updatedAddress;
    size_t           tempValue;
    int              numTimers;
    int              i;
    TRACE("HPETInitialize()");

    hpetTable = __GetHpetTable();
    if (!hpetTable) {
        WARNING("HPETInitialize no HPET detected.");
        return;
    }

    TRACE("HPETInitialize address 0x%" PRIxIN ", sequence %" PRIuIN ")",
          (uintptr_t)(hpetTable->Address.Address), hpetTable->Sequence);

    memset(&g_hpet, 0, sizeof(HpetController_t));
    g_hpet.BaseAddress = (uintptr_t)(hpetTable->Address.Address);
    g_hpet.TickMinimum = hpetTable->MinimumTick;

    osStatus = MemorySpaceMap(
            GetCurrentMemorySpace(),
            &updatedAddress,
            &g_hpet.BaseAddress,
            GetMemorySpacePageSize(),
            0,
            MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_NOCACHE,
            MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED
    );
    if (osStatus != OS_EOK) {
        ERROR("HPETInitialize failed to map address for hpet.");
        return;
    }
    g_hpet.BaseAddress = updatedAddress;

    // AMD SB700 Systems initialise HPET on first register access,
    // wait for it to set up HPET, its config register reads 0xFFFFFFFF meanwhile
    for (i = 0; i < 10000; i++) {
        HP_READ_32(HPET_REGISTER_CONFIG, &tempValue);
        if (tempValue != 0xFFFFFFFF) {
            break;
        }
    }

    // Did system fail to initialize
    if (tempValue == 0xFFFFFFFF) {
        ERROR("HPETInitialize failed to initialize HPET (AMD SB700).");
        return;
    }

    // Check the period for a sane value
    HP_READ_32(HPET_REGISTER_CAPABILITIES + 4, &g_hpet.Period);
    TRACE("HPETInitialize Minimum Tick 0x%" PRIxIN ", Period 0x%" PRIxIN "", g_hpet.TickMinimum, g_hpet.Period);

    if ((g_hpet.Period == 0) || (g_hpet.Period > HPET_MAXPERIOD)) {
        ERROR("HPETInitialize failed to initialize HPET, period is invalid.");
        return;
    }

    // Stop timer, zero out counter
    __StopHpet();
    tempValue = 0;
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER, tempValue);
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER + 4, tempValue);

    // Calculate the frequency
    g_hpet.Frequency.QuadPart = (int64_t)DIVUP(FSEC_PER_SEC, (uint64_t)g_hpet.Period);

    // Process the capabilities
    HP_READ_32(HPET_REGISTER_CAPABILITIES, &tempValue);
    g_hpet.Is64Bit = (tempValue & HPET_64BITSUPPORT) ? 1 : 0;
    legacy    = (tempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
    numTimers = (int)HPET_TIMERCOUNT(tempValue);
    TRACE("HPETInitialize Capabilities 0x%" PRIxIN ", Timers 0x%" PRIxIN ", MHz %" PRIuIN "",
          TempValue, numTimers, (g_hpet.Frequency.u.LowPart / 1000));

    if (legacy && numTimers < 2) {
        ERROR("HPETInitialize failed to initialize HPET, legacy is available but not enough timers.");
        return;
    }

    // Sanitize the number of timers, must be above 0
    if (numTimers == 0) {
        ERROR("HPETInitialize there was no timers present in HPET");
        return;
    }

    // Enable the legacy routings if its are supported
    HP_READ_32(HPET_REGISTER_CONFIG, &tempValue);
    if (legacy) {
        tempValue |= HPET_CONFIG_LEGACY;
    }
    HP_WRITE_32(HPET_REGISTER_CONFIG, tempValue);

    // Loop through all comparators and configurate them
    for (i = 0; i < numTimers; i++) {
        if (__InitializeComparator(i, legacy) == OS_EUNKNOWN) {
            ERROR("HPETInitialize HPET Failed to initialize comparator %" PRIiIN "", i);
            g_hpet.Timers[i].Present = 0;
        }
    }

    // Register the HPET as an available HPC
    osStatus = SystemTimerRegister(
            "acpi-hpet",
        &g_hpetOperations,
        SystemTimeAttributes_COUNTER | SystemTimeAttributes_HPC,
        &g_hpet
    );
    if (osStatus != OS_EOK) {
        WARNING("HPETInitialize failed to register platform timer");
    }
}

static void
HpetGetCount(
        _In_  void*            context,
        _Out_ UInteger64_t* tickOut)
{
    _CRT_UNUSED(context);
    __ReadMainCounter(tickOut);
}

static void
HpetGetFrequency(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    _CRT_UNUSED(context);
    frequency->QuadPart = g_hpet.Frequency.QuadPart;
}
