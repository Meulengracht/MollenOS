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

static void HpetGetCount(void*, LargeUInteger_t*);
static void HpetGetFrequency(void*, LargeUInteger_t*);
static void HpetNoOperation(void*);

/**
 * Recalibrate is registered as a no-op as the TSC never needs to be calibrated again
 */
static SystemTimerOperations_t g_hpetOperations = {
        .Read = HpetGetCount,
        .GetFrequency = HpetGetFrequency,
        .Recalibrate = HpetNoOperation
};
static HpController_t g_hpet = { 0 };

#define HP_READ_32(Offset, ValueOut) ReadVolatileMemory((const volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)(ValueOut), 4)
#define HP_READ_64(Offset, ValueOut) ReadVolatileMemory((const volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)(ValueOut), 8)

#define HP_WRITE_32(Offset, Value)   WriteVolatileMemory((volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)&(Value), 4)
#define HP_WRITE_64(Offset, Value)   WriteVolatileMemory((volatile void*)(g_hpet.BaseAddress + (Offset)), (void*)&(Value), 8)

void
HpReadCounter(
    _In_  size_t   Offset,
    _Out_ reg64_t* ValueOut)
{
#if __BITS == 64
    HP_READ_64(Offset, ValueOut);
#else
    *ValueOut = 0;
    HP_READ_32(Offset, (reg32_t*)ValueOut);
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
    HP_WRITE_32(Offset, Value);
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
HpetIsEmulatingLegacyController(void)
{
    size_t Config;
    if (g_hpet.BaseAddress != 0) {
       HP_READ_32(HPET_REGISTER_CONFIG, &Config);
        if (Config & HPET_CONFIG_LEGACY) {
            return OsSuccess;
        }     
    }
    return OsNotSupported;    
}

void
HpReadFrequency(
    _Out_ LargeUInteger_t *Value)
{
    Value->QuadPart = g_hpet.Frequency.QuadPart;
}

void
HpReadMainCounter(
    _Out_ LargeUInteger_t* Value)
{
#if __BITS == 64
    if (g_hpet.Is64Bit) {
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
        return InterruptNotHandled;
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
    return InterruptHandled;
}

static OsStatus_t
HpComparatorInitialize(
    _In_ int Index,
    _In_ int Legacy)
{
    HpTimer_t *Timer     = &g_hpet.Timers[Index];
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
    HpetInterrupt.ResourceTable.Handler = HpInterrupt;
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
    LargeUInteger_t   Now;
    uint64_t          Delta;
    size_t            TempValue;
    size_t            TempValue2;
    HpTimer_t *Timer = &g_hpet.Timers[Index];

    TRACE("HpComparatorStart(%" PRIiIN ", %" PRIuIN ", %" PRIiIN ")", Index, LODWORD(Frequency), Periodic);

    // Calculate the delta
    Delta = (uint64_t)g_hpet.Frequency.QuadPart / Frequency;
    if (Delta < g_hpet.TickMinimum) {
        Delta = g_hpet.TickMinimum;
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
    TempValue2 = (1 << Index);
    HP_WRITE_32(HPET_REGISTER_INTSTATUS, TempValue2);
    TRACE(" > Writing config value 0x%" PRIxIN "", TempValue);
    HP_WRITE_32(HPET_TIMER_CONFIG(Index), TempValue);
    HpWriteCounter(HPET_TIMER_COMPARATOR(Index), (reg64_t)Now.QuadPart);
    if (Timer->PeriodicSupport && Periodic) {
        HpWriteCounter(HPET_TIMER_COMPARATOR(Index), Delta);
    }
    HpStart();
    return OsSuccess;
}


/* AcpiDeviceInstallFixed
 * Scans for fixed devices and initializes them. */
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
HpetInitialize(void)
{
    ACPI_TABLE_HPET* hpetTable;
    int              legacy;
    OsStatus_t       osStatus;
    uintptr_t        updatedAddress;
    size_t           TempValue;
    int              numTimers;
    int              i;
    TRACE("HpetInitialize()");

    hpetTable = __GetHpetTable();
    if (!hpetTable) {
        WARNING("HpetInitialize no HPET detected.");
        return;
    }

    TRACE("HpetInitialize address 0x%" PRIxIN ", sequence %" PRIuIN ")",
          (uintptr_t)(hpetTable->Address.Address), hpetTable->Sequence);

    memset(&g_hpet, 0, sizeof(HpController_t));
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
    if (osStatus != OsSuccess) {
        ERROR("HpInitialize failed to map address for hpet.");
        return OsError;
    }
    g_hpet.BaseAddress = updatedAddress;

    // AMD SB700 Systems initialise HPET on first register access,
    // wait for it to set up HPET, its config register reads 0xFFFFFFFF meanwhile
    for (i = 0; i < 10000; i++) {
        HP_READ_32(HPET_REGISTER_CONFIG, &TempValue);
        if (TempValue != 0xFFFFFFFF) {
            break;
        }
    }

    // Did system fail to initialize
    if (TempValue == 0xFFFFFFFF) {
        ERROR("HpInitialize failed to initialize HPET (AMD SB700).");
        return AE_ERROR;
    }

    // Check the period for a sane value
    HP_READ_32(HPET_REGISTER_CAPABILITIES + 4, &g_hpet.Period);
    TRACE("HpInitialize Minimum Tick 0x%" PRIxIN ", Period 0x%" PRIxIN "", g_hpet.TickMinimum, g_hpet.Period);

    if ((g_hpet.Period == 0) || (g_hpet.Period > HPET_MAXPERIOD)) {
        ERROR("HpInitialize failed to initialize HPET, period is invalid.");
        return AE_ERROR;
    }

    // Stop timer, zero out counter
    HpStop();
    TempValue = 0;
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER, TempValue);
    HP_WRITE_32(HPET_REGISTER_MAINCOUNTER + 4, TempValue);

    // Calculate the frequency
    g_hpet.Frequency.QuadPart = (int64_t)DIVUP(FSEC_PER_SEC, (uint64_t)g_hpet.Period);

    // Process the capabilities
    HP_READ_32(HPET_REGISTER_CAPABILITIES, &TempValue);
    g_hpet.Is64Bit = (TempValue & HPET_64BITSUPPORT) ? 1 : 0;
    legacy    = (TempValue & HPET_LEGACYMODESUPPORT) ? 1 : 0;
    numTimers = (int)HPET_TIMERCOUNT(TempValue);
    TRACE("HpInitialize Capabilities 0x%" PRIxIN ", Timers 0x%" PRIxIN ", MHz %" PRIuIN "",
          TempValue, numTimers, (g_hpet.Frequency.u.LowPart / 1000));

    if (legacy && numTimers < 2) {
        ERROR("HpInitialize failed to initialize HPET, legacy is available but not enough timers.");
        return AE_ERROR;
    }

    // Sanitize the number of timers, must be above 0
    if (numTimers == 0) {
        ERROR("HpInitialize there was no timers present in HPET");
        return AE_ERROR;
    }

    // Enable the legacy routings if its are supported
    HP_READ_32(HPET_REGISTER_CONFIG, &TempValue);
    if (legacy) {
        TempValue |= HPET_CONFIG_LEGACY;
    }
    HP_WRITE_32(HPET_REGISTER_CONFIG, TempValue);

    // Loop through all comparators and configurate them
    for (i = 0; i < numTimers; i++) {
        if (HpComparatorInitialize(i, legacy) == OsError) {
            ERROR("HpInitialize HPET Failed to initialize comparator %" PRIiIN "", i);
            g_hpet.Timers[i].Present = 0;
        }
    }

    // Register the HPET as an available HPC
    osStatus = SystemTimerRegister(
        &g_hpetOperations,
        SystemTimeAttributes_COUNTER | SystemTimeAttributes_HPC,
        UUID_INVALID,
        &g_hpet
    );
    if (osStatus != OsSuccess) {
        WARNING("HpInitialize failed to register platform timer");
    }
    return AE_OK;
}

static void
HpetGetCount(
        _In_  void*            context,
        _Out_ LargeUInteger_t* tickOut)
{
    _CRT_UNUSED(context);
    HpReadMainCounter(tickOut);
}

static void
HpetGetFrequency(
        _In_  void*            context,
        _Out_ LargeUInteger_t* frequencyOut)
{
    _CRT_UNUSED(context);
    HpReadFrequency(frequencyOut);
}

static void
HpetNoOperation(
        _In_ void* context)
{
    _CRT_UNUSED(context);
}
