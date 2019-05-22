/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#define __MODULE "HPET"
//#define __TRACE

#include <memoryspace.h>
#include <interrupts.h>
#include <timers.h>
#include <debug.h>
#include <hpet.h>
#include <heap.h>
#include <stdlib.h>

static HpController_t HpetController = { 0 };

/* HpRead
 * Reads the 32-bit value from the given register offset */
void
HpRead(
    _In_  size_t    Offset,
    _Out_ reg32_t*  Value)
{
    *Value = *((reg32_t*)(HpetController.BaseAddress + Offset));
}

/* HpWrite
 * Writes the given 32-bit value to the given register offset */
void
HpWrite(
    _In_ size_t     Offset,
    _In_ reg32_t    Value)
{
    *((reg32_t*)(HpetController.BaseAddress + Offset)) = Value;
}

/* HpReadCounter
 * Reads the 32/64-bit value from the given register offset */
void
HpReadCounter(
    _In_  size_t    Offset,
    _Out_ reg64_t*  Value)
{
#if __BITS == 64
    *Value = *((reg64_t*)(HpetController.BaseAddress + Offset));
#else
    LargeInteger_t *ConcatValue = (LargeInteger_t*)Value;
    ConcatValue->u.LowPart  = *((reg32_t*)(HpetController.BaseAddress + Offset));
    ConcatValue->u.HighPart = 0;
#endif
}

/* HpWriteCounter
 * Writes the given 32/64-bit value to the given register offset */
void
HpWriteCounter(
    _In_ size_t     Offset,
    _In_ reg64_t    Value)
{
#if __BITS == 64
    *((reg64_t*)(HpetController.BaseAddress + Offset)) = Value;
#else
    *((reg32_t*)(HpetController.BaseAddress + Offset)) = LODWORD(Value);
#endif
}

/* HpStop
 * Stops the given controller's main counter */
void
HpStop(void)
{
    reg32_t Config;
    HpRead(HPET_REGISTER_CONFIG, &Config);
    Config &= ~HPET_CONFIG_ENABLED;
    HpWrite(HPET_REGISTER_CONFIG, Config);

    // Wait for it to actually stop
    HpRead(HPET_REGISTER_CONFIG, &Config);
    while (Config & HPET_CONFIG_ENABLED) {
        HpRead(HPET_REGISTER_CONFIG, &Config);
    }
}

/* HpStart
 * Starts the given controller's main counter */
void
HpStart(void)
{
    reg32_t Config;
    HpRead(HPET_REGISTER_CONFIG, &Config);
    Config |= HPET_CONFIG_ENABLED;
    HpWrite(HPET_REGISTER_CONFIG, Config);
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

/* HpReadFrequency
 * Reads the main frequency value into the given structure */
void
HpReadFrequency(
    _Out_ LargeInteger_t *Value)
{
    memcpy(Value, &HpetController.Frequency, sizeof(LargeInteger_t));
}

/* HpReadMainCounter
 * Safely reads the main counter by using 32 bit reads and making sure that
 * the upper bits don't change */
void
HpReadMainCounter(
    _Out_ LargeInteger_t *Value)
{
    // Variables
#if __BITS == 64
    if (HpetController.Is64Bit) {
        Value->QuadPart     = *((int64_t*)(HpetController.BaseAddress + HPET_REGISTER_MAINCOUNTER));
    }
    else {
        Value->u.LowPart    = *((reg32_t*)(HpetController.BaseAddress + HPET_REGISTER_MAINCOUNTER));
        Value->u.HighPart   = 0;
    }
#else
    if (HpetController.Is64Bit) {
        volatile reg32_t *Register = (volatile reg32_t*)(HpetController.BaseAddress + HPET_REGISTER_MAINCOUNTER);
        Value->QuadPart = 0;
        do {
            Value->u.HighPart = *(Register + 1);
            Value->u.LowPart  = *Register;
        } while (Value->u.HighPart != *(Register + 1));
    }
    else {
        Value->u.LowPart    = *((reg32_t*)(HpetController.BaseAddress + HPET_REGISTER_MAINCOUNTER));
        Value->u.HighPart   = 0;
    }
#endif
}

/* HpInterrupt
 * HPET Interrupt handler */
InterruptStatus_t
HpInterrupt(
    _In_ FastInterruptResources_t*  NotUsed,
    _In_ void*                      Context)
{
    // Variables
    reg32_t InterruptStatus = 0;
    int i                   = 0;
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // Initiate values
    HpRead(HPET_REGISTER_INTSTATUS, &InterruptStatus);

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
    HpWrite(HPET_REGISTER_INTSTATUS, InterruptStatus);
    return InterruptHandled;
}

/* HpComparatorInitialize 
 * Initializes a given comparator in the HPET controller */
OsStatus_t
HpComparatorInitialize(
    _In_ int        Index)
{
    // Variables
    HpTimer_t *Timer        = &HpetController.Timers[Index];
    reg32_t Configuration   = 0;
    reg32_t InterruptMap    = 0;

    // Debug
    TRACE("HpComparatorInitialize(%" PRIiIN ")", Index);

    // Read values
    HpRead(HPET_TIMER_CONFIG(Index), &Configuration);
    HpRead(HPET_TIMER_CONFIG(Index) + 4, &InterruptMap);
    if (Configuration == 0xFFFFFFFF || InterruptMap == 0) {
        return OsError;
    }

    // Setup basic information
    Timer->Present      = 1;
    Timer->Irq          = INTERRUPT_NONE;
    Timer->InterruptMap = InterruptMap;

    // Store some features
    if (Configuration & HPET_TIMER_CONFIG_64BITMODESUPPORT) {
        Timer->Is64Bit          = 1;
    }
    if (Configuration & HPET_TIMER_CONFIG_FSBSUPPORT) {
        Timer->MsiSupport       = 1;
    }
    if (Configuration & HPET_TIMER_CONFIG_PERIODICSUPPORT) {
        Timer->PeriodicSupport  = 1;
    }
    
    // Process timer configuration and disable it for now
    Configuration &= ~(HPET_TIMER_CONFIG_IRQENABLED | HPET_TIMER_CONFIG_POLARITY | HPET_TIMER_CONFIG_FSBMODE);
    HpWrite(HPET_TIMER_CONFIG(Index), Configuration);
    return OsSuccess;
}

/* HpComparatorStart
 * Starts a given comparator in the HPET controller with the
 * given frequency (hz) */
OsStatus_t
HpComparatorStart(
    _In_ int        Index,
    _In_ uint64_t   Frequency,
    _In_ int        Periodic)
{
    // Variables
    DeviceInterrupt_t HpetInterrupt;
    LargeInteger_t Now;
    uint64_t Delta;
    reg32_t TempValue;
    int i, j;
    HpTimer_t *Timer = &HpetController.Timers[Index];

    // Debug
    TRACE("HpComparatorStart(%" PRIiIN ", %" PRIuIN ", %" PRIiIN ")", Index, LODWORD(Frequency), Periodic);

    // Calculate the delta
    Delta            = (uint64_t)HpetController.Frequency.QuadPart / Frequency;
    if (Delta < HpetController.TickMinimum) {
        Delta        = HpetController.TickMinimum;
    }
    TRACE(" > Delta 0x%" PRIxIN "", LODWORD(Delta));

    // Stop main timer and calculate the next irq
    HpStop();
    HpReadMainCounter(&Now);
    Now.QuadPart    += Delta;

    // Allocate interrupt for timer?
    if (Timer->Irq == INTERRUPT_NONE) {
        // Setup interrupt
        memset(&HpetInterrupt, 0, sizeof(DeviceInterrupt_t));
        HpetInterrupt.FastInterrupt.Handler     = HpInterrupt;
        HpetInterrupt.Context                   = Timer;
        HpetInterrupt.Line                      = INTERRUPT_NONE;
        HpetInterrupt.Pin                       = INTERRUPT_NONE;
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
            Timer->Interrupt    = 
                InterruptRegister(&HpetInterrupt, INTERRUPT_MSI | INTERRUPT_KERNEL);
            Timer->MsiAddress   = (reg32_t)HpetInterrupt.MsiAddress;
            Timer->MsiValue     = (reg32_t)HpetInterrupt.MsiValue;
            TRACE(" > Using msi interrupts (untested)");
        }
        else {
            Timer->Interrupt    =
                InterruptRegister(&HpetInterrupt, INTERRUPT_VECTOR | INTERRUPT_KERNEL);
            Timer->Irq          = HpetInterrupt.Line;
            TRACE(" > Using irq interrupt %" PRIiIN "", Timer->Irq);
        }
    }
    
    // At this point all resources are ready
    Timer->Enabled = 1;

    // Process configuration
    // We must initially configure as 32 bit as I read some hw doesn't handle 64 bit that well
    HpRead(HPET_TIMER_CONFIG(Index), &TempValue);
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
        HpWrite(HPET_TIMER_FSB(Index),      Timer->MsiValue);
        HpWrite(HPET_TIMER_FSB(Index) + 4,  Timer->MsiAddress);
    }
    else {
        TempValue |= HPET_TIMER_CONFIG_IRQ(Timer->Irq);
        if (Timer->Irq > 15) {
            TempValue |= HPET_TIMER_CONFIG_POLARITY;
        }
    }

    // Update configuration and comparator
    HpWrite(HPET_REGISTER_INTSTATUS,        (1 << Index));
    TRACE(" > Writing config value 0x%" PRIxIN "",   TempValue);
    HpWrite(HPET_TIMER_CONFIG(Index),       TempValue);
    HpWriteCounter(HPET_TIMER_COMPARATOR(Index),    (reg64_t)Now.QuadPart);
    if (Timer->PeriodicSupport && Periodic) {
        HpWriteCounter(HPET_TIMER_COMPARATOR(Index), Delta);
    }
    HpStart();
    return OsSuccess;
}

/* HpInitialize
 * Initializes the ACPI hpet timer from the hpet table. */
ACPI_STATUS
HpInitialize(
    _In_ ACPI_TABLE_HPET *Table)
{
    // Variables
    SystemPerformanceTimerOps_t PerformanceOps = { 
        HpReadFrequency, HpReadMainCounter
    };
    int Legacy = 0, FoundPeriodic = 0;
    OsStatus_t Status;
    reg32_t TempValue;
    int i, NumTimers;

    // Trace
    TRACE("HpInitialize(Address 0x%" PRIxIN ", Sequence %" PRIuIN ")",
        (uintptr_t)(Table->Address.Address & __MASK), Table->Sequence);

    // Initialize the structure
    memset(&HpetController, 0, sizeof(HpController_t));
    HpetController.BaseAddress = (uintptr_t)(Table->Address.Address & __MASK);
    HpetController.TickMinimum = Table->MinimumTick;

    // Map the address
    Status = CreateMemorySpaceMapping(GetCurrentMemorySpace(), 
        &HpetController.BaseAddress, &HpetController.BaseAddress, GetMemorySpacePageSize(),
        MAPPING_COMMIT | MAPPING_PERSISTENT | MAPPING_NOCACHE, 
        MAPPING_VIRTUAL_GLOBAL | MAPPING_PHYSICAL_FIXED, __MASK);
    if (Status != OsSuccess) {
        ERROR("Failed to map address for hpet.");
        return OsError;
    }

    // Stop timer, zero out counter
    HpStop();
    HpWrite(HPET_REGISTER_MAINCOUNTER, 0);
    HpWrite(HPET_REGISTER_MAINCOUNTER + 4, 0);

    // Get the period
    HpRead(HPET_REGISTER_CAPABILITIES + 4, &HpetController.Period);
    TRACE("Minimum Tick 0x%" PRIxIN ", Period 0x%" PRIxIN "", HpetController.TickMinimum, HpetController.Period);

    // AMD SB700 Systems initialise HPET on first register access,
    // wait for it to setup HPET, its config register reads 0xFFFFFFFF meanwhile
    for (i = 0; i < 10000; i++) {
        HpRead(HPET_REGISTER_CONFIG, &TempValue);
        if (TempValue != 0xFFFFFFFF) {
            break;
        }
    }

    // Did system fail to initialize
    if (TempValue == 0xFFFFFFFF || (HpetController.Period == 0)
        || (HpetController.Period > HPET_MAXPERIOD)) {
        ERROR("Failed to initialize HPET (AMD SB700) or period is invalid.");
        return AE_ERROR;
    }

    // Calculate the frequency
    HpetController.Frequency.QuadPart = (int64_t)DIVUP(FSEC_PER_SEC, (uint64_t)HpetController.Period);

    // Process the capabilities
    HpRead(HPET_REGISTER_CAPABILITIES, &TempValue);
    HpetController.Is64Bit  = (TempValue & HPET_64BITSUPPORT) ? 1 : 0;
    Legacy                  = (TempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
    NumTimers               = (int)HPET_TIMERCOUNT(TempValue);

    // Trace
    TRACE("Capabilities 0x%" PRIxIN ", Timers 0x%" PRIxIN ", MHz %" PRIuIN "", 
        TempValue, NumTimers, (HpetController.Frequency.u.LowPart / 1000));

    // Sanitize the number of timers, must be above 0
    if (NumTimers == 0) {
        ERROR("There was no timers present in HPET");
        return AE_ERROR;
    }

    // We want to disable the legacy if its supported and enabled
    HpRead(HPET_REGISTER_CONFIG, &TempValue);
    if (Legacy != 0) {
        TempValue &= ~(HPET_CONFIG_LEGACY);
    }
    HpWrite(HPET_REGISTER_CONFIG, TempValue);

    // Loop through all comparators and configurate them
    for (i = 0; i < NumTimers; i++) {
        if (HpComparatorInitialize(i) == OsError) {
            ERROR("HPET Failed to initialize comparator %" PRIiIN "", i);
            HpetController.Timers[i].Present = 0;
        }
    }

    // Iterate and find periodic timer
    // and install that one as system timer
    for (i = 0; i < NumTimers; i++) {
        if (HpetController.Timers[i].Present && HpetController.Timers[i].PeriodicSupport) {
            if (HpComparatorStart(i, 1000, 1) != OsSuccess) {
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

    // If we didn't find periodic, use the first present
    // timer as one-shot and reinit it every interrupt
    if (!FoundPeriodic) {
        WARNING("No periodic timer present!");
    }
    return AE_OK;
}
