/**
 * MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */

#define __MODULE "HPET"
//#define __TRACE

#include <arch/io.h>
#include <memoryspace.h>
#include <interrupts.h>
#include <timers.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>
#include <stdlib.h>

static HpController_t HpetController = { 0 };

#define HP_READ_32(Offset, ValueOut) ReadVolatileMemory(HpetController.BaseAddress + Offset, 4, ValueOut)
#define HP_READ_64(Offset, ValueOut) ReadVolatileMemory(HpetController.BaseAddress + Offset, 8, ValueOut)

#define HP_WRITE_32(Offset, Value)   WriteVolatileMemory(HpetController.BaseAddress + Offset, 4, Value)
#define HP_WRITE_64(Offset, Value)   WriteVolatileMemory(HpetController.BaseAddress + Offset, 4, Value)

void
HpReadCounter(
    _In_  size_t   Offset,
    _Out_ reg64_t* ValueOut)
{
#if __BITS == 64
    HP_READ_64(Offset, ValueOut);
#else
    *ValueOut = 0;
    HP_READ_32(Offset, ValueOut);
#endif
}

void
HpWriteCounter(
    _In_ size_t  Offset,
    _In_ reg64_t Value)
{
#if __BITS == 64
    HP_WRITE_64(Offset, Value);
#else
    HP_WRITE_32(Offset, LODWORD(Value));
#endif
}

void
HpStop(void)
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

void
HpStart(void)
{
    size_t Config;
    HP_READ_32(HPET_REGISTER_CONFIG, &Config);
    Config |= HPET_CONFIG_ENABLED;
    HP_WRITE_32(HPET_REGISTER_CONFIG, Config);
}

OsStatus_t
HpHasLegacyController(void)
{
    
    size_t Config;
    if (HpetController.BaseAddress != 0) {
       HP_READ_32(HPET_REGISTER_CONFIG, &Config);
        if (Config & HPET_CONFIG_LEGACY) {
            return OsSuccess;
        }     
    }
    return OsNotSupported;    
}

void
HpResetTicks(void)
{
    HpetController.Clock = 0;
}

clock_t
HpGetTicks(void)
{
    return HpetController.Clock;
}

void
HpReadFrequency(
    _Out_ LargeInteger_t *Value)
{
    memcpy(Value, &HpetController.Frequency, sizeof(LargeInteger_t));
}

void
HpReadMainCounter(
    _Out_ LargeInteger_t* Value)
{
    // Variables
#if __BITS == 64
    if (HpetController.Is64Bit) {
        HP_READ_64(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->QuadPart);
    }
    else {
        Value->QuadPart = 0;
        HP_READ_32(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->QuadPart);
    }
#else
    if (HpetController.Is64Bit) {
        // This requires us to synchronize with the upper 32 bits from each read to ensure
        // we don't encounter a rollover. Keep a direct pointer for more convenient reading
        volatile reg32_t *Register = (volatile reg32_t*)(HpetController.BaseAddress + HPET_REGISTER_MAINCOUNTER);
        do {
            Value->u.HighPart = *(Register + 1);
            Value->u.LowPart  = *Register;
        } while (Value->u.HighPart != *(Register + 1));
    }
    else {
        Value->u.HighPart = 0;
        HP_READ_32(HPET_REGISTER_MAINCOUNTER, (size_t*)&Value->u.LowPart);
    }
#endif
}

InterruptStatus_t
HpInterrupt(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
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
        return InterruptNotHandled;
    }

    // Iterate the port-map and check if the interrupt
    // came from that timer
    for (i = 0; i < HPET_MAXTIMERCOUNT; i++) {
        if (InterruptStatus & (1 << i) && HpetController.Timers[i].Enabled) {
            if (HpetController.Timers[i].SystemTimer) {
                HpetController.Clock++;
                TimersInterrupt(HpetController.Timers[i].Interrupt);
            }
            if (!HpetController.Timers[i].PeriodicSupport) {
                // Non periodic timer fired, what now?
                WARNING("HPET::NON-PERIODIC TIMER FIRED");
            }
        }
    }

    // Write clear interrupt register and return
    HP_WRITE_32(HPET_REGISTER_INTSTATUS, InterruptStatus);
    return InterruptHandled;
}

static OsStatus_t
HpComparatorInitialize(
    _In_ int Index,
    _In_ int Legacy)
{
    HpTimer_t *Timer     = &HpetController.Timers[Index];
    size_t Configuration = 0;
    size_t InterruptMap  = 0;

    TRACE("HpComparatorInitialize(%" PRIiIN ")", Index);

    // Read values
    HP_READ_32(HPET_TIMER_CONFIG(Index), &Configuration);
    
    // Fixup the interrupt map that won't be present in some cases for timer
    // 0 and 1, when legacy has been enabled
    if (Legacy && (Index <= 1)) {
        // 0 = Irq 0
        // 1 = Irq 8
        InterruptMap = 1 << (Index * 8);
    }
    else {
        HP_READ_32(HPET_TIMER_CONFIG(Index) + 4, &InterruptMap);
    }
    
    if (Configuration == 0xFFFFFFFF || InterruptMap == 0) {
        WARNING("Configuration: 0x%x, Map 0x%x", LODWORD(Configuration), LODWORD(InterruptMap));
        return OsError;
    }

    // Setup basic information
    Timer->Present      = 1;
    Timer->Irq          = INTERRUPT_NONE;
    Timer->InterruptMap = LODWORD(InterruptMap);

    // Store some features
    if (Configuration & HPET_TIMER_CONFIG_64BITMODESUPPORT) {
        Timer->Is64Bit = 1;
    }
    if (Configuration & HPET_TIMER_CONFIG_FSBSUPPORT) {
        Timer->MsiSupport = 1;
    }
    if (Configuration & HPET_TIMER_CONFIG_PERIODICSUPPORT) {
        Timer->PeriodicSupport = 1;
    }
    
    // Process timer configuration and disable it for now
    Configuration &= ~(HPET_TIMER_CONFIG_IRQENABLED | HPET_TIMER_CONFIG_POLARITY | HPET_TIMER_CONFIG_FSBMODE);
    HP_WRITE_32(HPET_TIMER_CONFIG(Index), Configuration);
    return OsSuccess;
}

static void
HpAllocateInterrupt(
    _In_ HpTimer_t* Timer)
{
    DeviceInterrupt_t HpetInterrupt;
    int i, j;
    
    memset(&HpetInterrupt, 0, sizeof(DeviceInterrupt_t));
    HpetInterrupt.FastInterrupt.Handler = HpInterrupt;
    HpetInterrupt.Context               = Timer;
    HpetInterrupt.Line                  = INTERRUPT_NONE;
    HpetInterrupt.Pin                   = INTERRUPT_NONE;
    TRACE(" > Gathering interrupts from irq-map 0x%" PRIxIN "", Timer->InterruptMap);

    // From the interrupt map, calculate possible int's
    for (i = 0, j = 0; i < 32; i++) {
        if (Timer->InterruptMap & (1 << i)) {
            HpetInterrupt.Vectors[j++] = i;
            if (j == INTERRUPT_MAXVECTORS) {
                break;
            }
        }
    }

    // Place an end marker
    if (j != INTERRUPT_MAXVECTORS) {
        HpetInterrupt.Vectors[j] = INTERRUPT_NONE;
    }

    // Handle MSI interrupts > normal
    if (Timer->MsiSupport) {
        Timer->Interrupt  = 
            InterruptRegister(&HpetInterrupt, INTERRUPT_MSI | INTERRUPT_KERNEL);
        Timer->MsiAddress = (reg32_t)HpetInterrupt.MsiAddress;
        Timer->MsiValue   = (reg32_t)HpetInterrupt.MsiValue;
        TRACE(" > Using msi interrupts (untested)");
    }
    else {
        Timer->Interrupt =
            InterruptRegister(&HpetInterrupt, INTERRUPT_VECTOR | INTERRUPT_KERNEL);
        Timer->Irq       = HpetInterrupt.Line;
        TRACE(" > Using irq interrupt %" PRIiIN "", Timer->Irq);
    }
}

OsStatus_t
HpComparatorStart(
    _In_ int      Index,
    _In_ uint64_t Frequency,
    _In_ int      Periodic,
    _In_ int      LegacyIrq)
{
    LargeInteger_t    Now;
    uint64_t          Delta;
    size_t            TempValue;
    HpTimer_t *Timer = &HpetController.Timers[Index];

    TRACE("HpComparatorStart(%" PRIiIN ", %" PRIuIN ", %" PRIiIN ")", Index, LODWORD(Frequency), Periodic);

    // Calculate the delta
    Delta = (uint64_t)HpetController.Frequency.QuadPart / Frequency;
    if (Delta < HpetController.TickMinimum) {
        Delta = HpetController.TickMinimum;
    }
    TRACE(" > Delta 0x%" PRIxIN "", LODWORD(Delta));

    // Stop main timer and calculate the next irq
    HpStop();
    HpReadMainCounter(&Now);
    Now.QuadPart += Delta;
    
    // Allocate interrupt for timer?
    if (LegacyIrq != INTERRUPT_NONE) {
        Timer->Irq = LegacyIrq;
    }
    
    if (Timer->Irq == INTERRUPT_NONE) {
        HpAllocateInterrupt(Timer);
    }
    
    // At this point all resources are ready
    Timer->Enabled = 1;

    // Process configuration
    // We must initially configure as 32 bit as I read some hw doesn't handle 64 bit that well
    HP_READ_32(HPET_TIMER_CONFIG(Index), &TempValue);
    TempValue |= HPET_TIMER_CONFIG_IRQENABLED | HPET_TIMER_CONFIG_32BITMODE;
    
    // Set some extra bits if periodic
    if (Timer->PeriodicSupport && Periodic) {
        TRACE(" > Configuring for periodic");
        TempValue |= HPET_TIMER_CONFIG_PERIODIC;
        TempValue |= HPET_TIMER_CONFIG_SET_CMP_VALUE;
    }

    // Set interrupt vector
    // MSI must be set to edge-triggered
    if (Timer->MsiSupport) {
        TempValue |= HPET_TIMER_CONFIG_FSBMODE;
        HP_WRITE_32(HPET_TIMER_FSB(Index),     Timer->MsiValue);
        HP_WRITE_32(HPET_TIMER_FSB(Index) + 4, Timer->MsiAddress);
    }
    else {
        TempValue |= HPET_TIMER_CONFIG_IRQ(Timer->Irq);
        if (Timer->Irq > 15) {
            TempValue |= HPET_TIMER_CONFIG_POLARITY;
        }
    }

    // Update configuration and comparator
    HP_WRITE_32(HPET_REGISTER_INTSTATUS, (1 << Index));
    TRACE(" > Writing config value 0x%" PRIxIN "", TempValue);
    HP_WRITE_32(HPET_TIMER_CONFIG(Index), TempValue);
    HpWriteCounter(HPET_TIMER_COMPARATOR(Index), (reg64_t)Now.QuadPart);
    if (Timer->PeriodicSupport && Periodic) {
        HpWriteCounter(HPET_TIMER_COMPARATOR(Index), Delta);
    }
    HpStart();
    return OsSuccess;
}

ACPI_STATUS
HpInitialize(
    _In_ ACPI_TABLE_HPET *Table)
{
    // Variables
    SystemPerformanceTimerOps_t PerformanceOps = { 
        HpReadFrequency, HpReadMainCounter
    };
    
    int        Legacy        = 0;
    int        FoundPeriodic = 0;
    OsStatus_t Status;
    uintptr_t  UpdatedAddress;
    size_t     TempValue;
    int        NumTimers;
    int        i;

    TRACE("HpInitialize(Address 0x%" PRIxIN ", Sequence %" PRIuIN ")",
        (uintptr_t)(Table->Address.Address), Table->Sequence);

    memset(&HpetController, 0, sizeof(HpController_t));
    HpetController.BaseAddress = (uintptr_t)(Table->Address.Address);
    HpetController.TickMinimum = Table->MinimumTick;

    Status = CreateMemorySpaceMapping(GetCurrentMemorySpace(), 
        &UpdatedAddress, &HpetController.BaseAddress, 
        GetMemorySpacePageSize(),
        MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_NOCACHE, 
        MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED, __MASK);
    if (Status != OsSuccess) {
        ERROR("Failed to map address for hpet.");
        return OsError;
    }
    HpetController.BaseAddress = UpdatedAddress;

    // AMD SB700 Systems initialise HPET on first register access,
    // wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile
    for (i = 0; i < 10000; i++) {
        HP_READ_32(HPET_REGISTER_CONFIG, &TempValue);
        if (TempValue != 0xFFFFFFFF) {
            break;
        }
    }

    // Did system fail to initialize
    if (TempValue == 0xFFFFFFFF) {
        ERROR("Failed to initialize HPET (AMD SB700).");
        return AE_ERROR;
    }

    // Check the period for a sane value
    HP_READ_32(HPET_REGISTER_CAPABILITIES + 4, &HpetController.Period);
    TRACE("Minimum Tick 0x%" PRIxIN ", Period 0x%" PRIxIN "", HpetController.TickMinimum, HpetController.Period);

    if ((HpetController.Period == 0) || (HpetController.Period > HPET_MAXPERIOD)) {
        ERROR("Failed to initialize HPET, period is invalid.");
        return AE_ERROR;
    }

    // Stop timer, zero out counter
    HpStop();
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER, 0);
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER + 4, 0);

    // Calculate the frequency
    HpetController.Frequency.QuadPart = (int64_t)DIVUP(FSEC_PER_SEC, (uint64_t)HpetController.Period);

    // Process the capabilities
    HP_READ_32(HPET_REGISTER_CAPABILITIES, &TempValue);
    HpetController.Is64Bit = (TempValue & HPET_64BITSUPPORT) ? 1 : 0;
    Legacy                 = (TempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
    NumTimers              = (int)HPET_TIMERCOUNT(TempValue);
    TRACE("Capabilities 0x%" PRIxIN ", Timers 0x%" PRIxIN ", MHz %" PRIuIN "", 
        TempValue, NumTimers, (HpetController.Frequency.u.LowPart / 1000));

    if (Legacy && NumTimers < 2) {
        ERROR("Failed to initialize HPET, legacy is available but not enough timers.");
        return AE_ERROR;
    }

    // Sanitize the number of timers, must be above 0
    if (NumTimers == 0) {
        ERROR("There was no timers present in HPET");
        return AE_ERROR;
    }

    // Enable the legacy routings if its are supported
    HP_READ_32(HPET_REGISTER_CONFIG, &TempValue);
    if (Legacy) {
        TempValue |= HPET_CONFIG_LEGACY;
    }
    HP_WRITE_32(HPET_REGISTER_CONFIG, TempValue);

    // Loop through all comparators and configurate them
    for (i = 0; i < NumTimers; i++) {
        if (HpComparatorInitialize(i, Legacy) == OsError) {
            ERROR("HPET Failed to initialize comparator %" PRIiIN "", i);
            HpetController.Timers[i].Present = 0;
        }
    }

    // If the system is using legacy replacmenet options, then don't initialize
    // any of the hpet timers just yet. Let the base drivers initialize them correctly.
    for (i = 0; !Legacy && i < NumTimers; i++) {
        if (HpetController.Timers[i].Present && HpetController.Timers[i].PeriodicSupport) {
            if (HpComparatorStart(i, 1000, 1, INTERRUPT_NONE) != OsSuccess) {
                ERROR("Failed to initialize periodic timer %" PRIiIN "", i);
            }
            else {
                if (TimersRegisterSystemTimer(HpetController.Timers[i].Interrupt, 1000, HpGetTicks, HpResetTicks) != OsSuccess) {
                    ERROR("Failed register timer %" PRIiIN " as the system timer", i);
                }
                else {
                    HpetController.Timers[i].SystemTimer = 1;
                    FoundPeriodic = 1;
                    break;
                }
            }
        }
    }

    // Register high performance timers
    if (TimersRegisterPerformanceTimer(&PerformanceOps) != OsSuccess) {
        ERROR("Failed to register the performance handlers");
    }    
    return AE_OK;
}
