/**
 * Copyright, Philip Meulengracht
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
 * MMU-500 probing, startup, and shutdown.
 * No production boot caller starts this backend yet. Callers must supply
 * powered, safe MMIO access and exclusive control of the Non-secure register
 * banks and stream match slots before starting a unit.
 */

#include "private.h"
#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

// Arguments of one SMMU500UnitPoll call, passed through ArmSMMUUnitPoll to
// __PollLocked. Count receives the number of records written to Faults.
typedef struct __SMMU500PollRequest {
    SMMU500Unit_t*   Unit;
    DMAIOMMUFault_t* Faults;
    uint32_t         Capacity;
    uint32_t         Count;
} __SMMU500PollRequest_t;

// Generic DMA IOMMU operations registered for each MMU-500 unit.
static const DMAIOMMUBackendOps_t g_operations = {
    .CreateSpace = SMMU500SpaceCreate,
    .DestroySpace = SMMU500SpaceDestroy,
    .PrepareMapping = SMMU500MappingPrepare,
    .InstallMapping = SMMU500MappingInstall,
    .RemoveMapping = SMMU500MappingRemove,
    .DestroyMapping = SMMU500MappingDestroy,
    .PrepareHardwareIDs = SMMU500HardwareIDsPrepare,
    .ConnectHardwareIDs = SMMU500HardwareIDsConnect,
    .DisconnectHardwareIDs = SMMU500HardwareIDsDisconnect,
    .DestroyHardwareIDs = SMMU500HardwareIDsDestroy
};

// Check the IDR7 revision byte (major in high nibble, minor in low nibble)
// against the tested MMU-500 r0p0 through r2p4 hardware revisions.
static bool
__IsSupportedRevision(
    _In_ uint32_t revision)
{
    static const uint32_t supported[] = { 0x00U, 0x10U, 0x20U, 0x21U, 0x22U, 0x24U };

    for (size_t index = 0; index < SIZEOF_ARRAY(supported); index++) {
        if (supported[index] == revision) {
            return true;
        }
    }
    return false;
}

// Verify that this MMU-500 supports every feature the driver relies on:
// stage-2 translation, stream matching, 4 KiB pages, standard 15-bit IDs,
// valid 32..48-bit address encodings, and a known revision. Logs the specific
// missing capability on rejection.
static oserr_t
__CheckCapabilities(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       idr0,
    _In_ uint32_t       idr2)
{
    uint32_t required = SMMU500_IDR0_STAGE2 | SMMU500_IDR0_MATCHING;
    uint32_t input = idr2 & 15U;  // IDR2.IAS [3:0]: device input address width index.
    uint32_t output = (idr2 >> 4) & 15U; // IDR2.OAS [7:4]: physical output address width index.

    if ((idr0 & required) != required) {
        ERROR("__CheckCapabilities: stage-2 translation and stream matching are required at %llx",
              unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if ((idr0 & SMMU500_IDR0_EXTENDED) != 0) {
        ERROR("__CheckCapabilities: extended stream IDs and VMIDs are not supported at %llx",
              unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if ((idr2 & SMMU500_IDR2_GRANULE4K) == 0) {
        ERROR("__CheckCapabilities: 4 KiB pages are not supported at %llx", unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    // Encodings 0..5 correspond to 32, 36, 40, 42, 44, and 48 bits.
    if (input >= 6U || output >= 6U) {
        ERROR("__CheckCapabilities: unknown address size, input %u, output %u at %llx",
              input, output, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if (!__IsSupportedRevision(unit->Revision)) {
        ERROR("__CheckCapabilities: unsupported revision %x at %llx",
              unit->Revision, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if ((idr0 & SMMU500_IDR0_CTTW) == 0) {
        // CTTW is an SoC tie-off pin that can be miswired. Firmware tables are
        // the source of truth for table coherency, so warn without changing
        // transaction attributes or failing the probe.
        WARNING("__CheckCapabilities: firmware specifies coherent walks with CTTW clear at %llx",
                unit->Common.Description.RegisterBase);
    }
    return OS_EOK;
}

// Decode register page size, context bank offsets, resource counts, Stream ID
// mask, and address widths from IDR0..IDR2. Reject units whose resource counts
// exceed SMMU500_MAX_RESOURCES or whose register window is too small or unaligned.
static oserr_t
__ReadHardwareLayout(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       idr0,
    _In_ uint32_t       idr1,
    _In_ uint32_t       idr2)
{
    static const uint32_t addressBits[] = { 32, 36, 40, 42, 44, 48 };
    uint32_t              sidBits = (idr0 >> 9) & 15U;    // IDR0.NUMSIDB [12:9]: Stream ID bit width.
    uint32_t              input = idr2 & 15U;             // IDR2.IAS [3:0].
    uint32_t              output = (idr2 >> 4) & 15U;     // IDR2.OAS [7:4].
    uint32_t              pageIndex = (idr1 >> 28) & 7U;  // IDR1.NUMPAGENDXB [30:28]: global space pages = 2^(pageIndex+1).

    // IDR1 bit 31 (PAGESIZE) selects 64 KiB register pages (shift 16) vs 4 KiB (shift 12).
    unit->PageShift = (idr1 & (1U << 31)) != 0 ? 16U : 12U;
    unit->ContextBase = 1U << (unit->PageShift + pageIndex + 1U);
    unit->ContextCount = idr1 & 255U;          // IDR1.NUMCB [7:0].
    unit->Stage2Count = (idr1 >> 16) & 255U;   // IDR1.NUMS2CB [23:16].
    unit->SlotCount = idr0 & 255U;             // IDR0.NUMSMRG [7:0].

    // NUMS2CB counts dedicated stage-2 banks. When zero, all general context
    // banks still support stage 2. Dedicated banks sit at the lowest indices
    // and are picked first by __FindFreeContextBank.
    if (unit->ContextCount == 0 || unit->ContextCount > SMMU500_MAX_RESOURCES ||
        unit->Stage2Count > unit->ContextCount || unit->SlotCount == 0 ||
        unit->SlotCount > SMMU500_MAX_RESOURCES) {
        ERROR("__ReadHardwareLayout: unsupported counts at %llx: %u context banks, "
              "%u dedicated stage-2 banks, %u stream slots",
              unit->Common.Description.RegisterBase, unit->ContextCount, unit->Stage2Count, unit->SlotCount);
        return OS_ENOTSUPPORTED;
    }

    // Ensure the firmware MMIO length covers both the global region (ContextBase
    // bytes) and all context bank pages (another ContextBase bytes).
    if (unit->ContextCount > (1U << (pageIndex + 1U)) ||
        (unit->Common.Description.RegisterBase & ((1ULL << unit->PageShift) - 1ULL)) != 0 ||
        unit->Common.Description.RegisterLength < 2ULL * unit->ContextBase) {
        ERROR("__ReadHardwareLayout: invalid register window at %llx, length %llx: "
              "%u context banks, bank base %x, page shift %u",
              unit->Common.Description.RegisterBase, unit->Common.Description.RegisterLength,
              unit->ContextCount, unit->ContextBase, unit->PageShift);
        return OS_ENOTSUPPORTED;
    }

    unit->SIDMask = (1U << sidBits) - 1U;
    unit->InputBits = addressBits[input];
    unit->OutputEncoding = output;
    unit->Stage2.AddressLimit = (1ULL << unit->InputBits) - 1ULL;
    unit->Stage2.PhysicalLimit = (1ULL << addressBits[output]) - 1ULL;
    // Walks above 39 bits require 4 levels starting at level 0. Walks up to 39
    // bits use 3 levels starting at level 1.
    unit->Stage2.RootLevel = unit->InputBits > 39U ? 0U : 1U;
    return OS_EOK;
}

// Read hardware ID registers without writing anything to the controller.
static oserr_t
__Probe(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t idr0 = __SMMU500Read32(unit->Common.Registers, SMMU500_IDR0);
    uint32_t idr1 = __SMMU500Read32(unit->Common.Registers, SMMU500_IDR1);
    uint32_t idr2 = __SMMU500Read32(unit->Common.Registers, SMMU500_IDR2);
    uint32_t idr7 = __SMMU500Read32(unit->Common.Registers, SMMU500_IDR7);
    oserr_t  status;

    TRACE("__Probe: unit %llx, IDR0 %x, IDR1 %x, IDR2 %x, IDR7 %x",
          unit->Common.Description.RegisterBase, idr0, idr1, idr2, idr7);
    unit->Revision = idr7 & 0xFFU;

    status = __CheckCapabilities(unit, idr0, idr2);
    if (status != OS_EOK) {
        return status;
    }
    return __ReadHardwareLayout(unit, idr0, idr1, idr2);
}

// Verify the firmware description before mapping registers: must describe an
// MMU-500 with cache-coherent table walks and a 4 KiB-aligned, non-wrapping
// MMIO window of at least one 4 KiB page.
static bool
__IsSupportedDescription(
    _In_ const ArmSMMUDescription_t* description)
{
    return description->Kind == ArmSMMU500 && description->TableCoherent &&
           description->RegisterLength >= 4096U && (description->RegisterLength & 4095U) == 0 &&
           (description->RegisterBase & 4095U) == 0 &&
           description->RegisterBase <= UINT64_MAX - (description->RegisterLength - 1U);
}

// Allocate zeroed tracking arrays for context banks and stream match slots.
static oserr_t
__AllocateRecords(
    _In_ SMMU500Unit_t* unit)
{
    unit->Contexts = kmalloc(unit->ContextCount * sizeof(*unit->Contexts));
    if (unit->Contexts == NULL) {
        ERROR("__AllocateRecords: allocating %u context bank records at %llx failed",
              unit->ContextCount, unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(unit->Contexts, 0, unit->ContextCount * sizeof(*unit->Contexts));
    
    unit->Slots = kmalloc(unit->SlotCount * sizeof(*unit->Slots));
    if (unit->Slots == NULL) {
        ERROR("__AllocateRecords: allocating %u stream slot records at %llx failed",
              unit->SlotCount, unit->Common.Description.RegisterBase);
        kfree(unit->Contexts);
        unit->Contexts = NULL;
        return OS_EOOM;
    }

    memset(unit->Slots, 0, unit->SlotCount * sizeof(*unit->Slots));
    return OS_EOK;
}

oserr_t
SMMU500UnitCreate(
    _In_    const ArmSMMUDescription_t* description,
    _InOut_ SMMU500Unit_t**             unitOut)
{
    SMMU500Unit_t*       unit;
    DMAIOMMUUnitConfig_t config;
    oserr_t              status;

    if (description == NULL || unitOut == NULL) {
        WARNING("SMMU500UnitCreate: missing description or output");
        return OS_EINVALPARAMS;
    }

    if (*unitOut != NULL) {
        WARNING("SMMU500UnitCreate: output already contains a unit");
        return OS_EBUSY;
    }

    if (!__IsSupportedDescription(description)) {
        ERROR("SMMU500UnitCreate: unsupported unit description, kind %u, register window %llx, length %llx",
              (uint32_t)description->Kind, description->RegisterBase, description->RegisterLength);
        return OS_ENOTSUPPORTED;
    }

    unit = kmalloc(sizeof(SMMU500Unit_t));
    if (unit == NULL) {
        ERROR("SMMU500UnitCreate: allocating unit at %llx failed", description->RegisterBase);
        return OS_EOOM;
    }

    memset(unit, 0, sizeof(SMMU500Unit_t));

    // Map MMIO registers and connect shared stage-2 hooks before probing.
    status = ArmSMMUUnitCommonInitialize(&unit->Common, description);
    if (status != OS_EOK) {
        kfree(unit);
        return status;
    }

    SMMU500Stage2Initialize(unit);

    status = __Probe(unit);
    if (status != OS_EOK) {
        kfree(unit);
        return status;
    }

    status = __AllocateRecords(unit);
    if (status != OS_EOK) {
        kfree(unit);
        return status;
    }

    config.Granularity = ARM_SMMU_MEMORY_PAGE_SIZE;
    config.AddressLimit = unit->Stage2.AddressLimit;
    config.PhysicalLimit = unit->Stage2.PhysicalLimit;
    config.CompletionTimeoutNs = ARM_SMMU_TIMEOUT_NS;
    
    status = DMAIOMMUUnitCreate(
        &config,
        &g_operations,
        unit,
        &unit->Common.Backend
    );
    if (status != OS_EOK) {
        ERROR("SMMU500UnitCreate: creating generic unit failed: %u", status);
        kfree(unit->Slots);
        kfree(unit->Contexts);
        kfree(unit);
        return status;
    }

    TRACE("SMMU500UnitCreate: unit %llx has %u context banks, %u stream slots, SID mask %x",
          description->RegisterBase, unit->ContextCount, unit->SlotCount, unit->SIDMask);
    *unitOut = unit;
    return OS_EOK;
}

// Configure CR0 so all unmatched streams fault, global faults are recorded, and
// each context bank keeps private VMIDs. CLIENTPD stays 0 because setting it
// lets all DMA bypass translation and fault routes.
static oserr_t
__WriteFaultingControls(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t control = __SMMU500Read32(unit->Common.Registers, SMMU500_CR0);

    control = (control & ~SMMU500_CR0_FIELDS) | SMMU500_CR0_FAULTS |
              SMMU500_CR0_UNMATCHED_FAULT | SMMU500_CR0_PRIVATE_VMID;
    return SMMU500WriteChecked(unit, SMMU500_CR0, control, SMMU500_CR0_FIELDS);
}

// Clear MMU-500 cache bypass bits in ACR so table walks and TLBs cache
// normally. Hardware from r2p0 onward also has a cache lock bit to clear.
static oserr_t
__ClearCacheControls(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t mask = SMMU500_ACR_BYPASS_CACHE;
    uint32_t control;

    if (unit->Revision >= 0x20U) {
        mask |= SMMU500_ACR_CACHE_LOCK;
    }
    control = __SMMU500Read32(unit->Common.Registers, SMMU500_ACR);
    return SMMU500WriteChecked(unit, SMMU500_ACR, control & ~mask, mask);
}

// Put every hardware stream slot into fault mode and clear its match mask.
static oserr_t
__ResetSlots(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    for (uint32_t index = 0; index < unit->SlotCount; index++) {
        oserr_t status;

        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__ResetSlots: unit %llx timed out at slot %u of %u",
                    unit->Common.Description.RegisterBase, index, unit->SlotCount);
            return OS_ETIMEOUT;
        }

        status = SMMU500SlotSetFault(unit, index);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Test that SMR(0) accepts every bit in SIDMask while VALID is 0 and S2CR(0)
// is already set to fault. Any SID bit that hardware drops cannot be matched
// safely later. Restore SMR(0) to 0 afterward.
static oserr_t
__CheckSIDBits(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t readBack;

    __SMMU500Write32(unit->Common.Registers, SMMU500_SMR(0), unit->SIDMask);
    __SMMU500Barrier();

    readBack = __SMMU500Read32(unit->Common.Registers, SMMU500_SMR(0));
    if ((readBack & SMMU500_SMR_FIELDS) != unit->SIDMask) {
        ERROR("__CheckSIDBits: SID field rejected at %llx: expected %x, read %x",
              unit->Common.Description.RegisterBase, unit->SIDMask, readBack);
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
        return OS_ECANCELLED;
    }
    return SMMU500WriteChecked(
        unit,
        SMMU500_SMR(0),
        0,
        SMMU500_SMR_FIELDS
    );
}

// Disable every context bank and turn off speculative page-table prefetch.
static oserr_t
__ResetContextBanks(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    uint32_t bankOffset;
    uint32_t control;
    oserr_t  status;

    for (uint32_t index = 0; index < unit->ContextCount; index++) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__ResetContextBanks: unit %llx timed out at bank %u of %u",
                    unit->Common.Description.RegisterBase, index, unit->ContextCount);
            return OS_ETIMEOUT;
        }

        bankOffset = __SMMU500ContextBankOffset(unit, index);
        status = SMMU500WriteChecked(
            unit,
            bankOffset + SMMU500_CB_SCTLR,
            0,
            SMMU500_SCTLR_FIELDS
        );
        if (status != OS_EOK) {
            return status;
        }

        control = __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_ACTLR);
        status = SMMU500WriteChecked(
            unit,
            bankOffset + SMMU500_CB_ACTLR,
            control & ~SMMU500_ACTLR_PREFETCH,
            SMMU500_ACTLR_PREFETCH
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Reset hardware into a clean faulting baseline before marking the unit Ready.
// Order matters: drain any firmware TLB sync and check old faults first so no
// stalled transfer survives bank reconfiguration, configure CR0 and ACR before
// resetting all stream slots to fault, verify writable SID mask bits on slot 0,
// turn off all context banks, check again for any init-time unit failures,
// and finish with a full TLB sync.
static oserr_t
__StartHardware(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    status = SMMU500SyncWait(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    if (ArmSMMUDeadlineExpired(deadline)) {
        WARNING("__StartHardware: unit %llx timed out before initialization", unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
    }

    unit->StartProgress = SMMU500Initializing;

    // Check for unit failures left by firmware before changing any bank
    // rules, so no stalled transfer survives bank reconfiguration. Device
    // faults left by firmware stay latched and the first Poll returns them.
    status = SMMU500FaultCheckAll(unit);
    if (status != OS_EOK) {
        return status;
    }

    status = __WriteFaultingControls(unit);
    if (status != OS_EOK) {
        return status;
    }

    status = __ClearCacheControls(unit);
    if (status != OS_EOK) {
        return status;
    }

    status = __ResetSlots(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckSIDBits(unit);
    if (status != OS_EOK) {
        return status;
    }

    status = __ResetContextBanks(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Check for unit failures triggered during setup before moving to Ready.
    status = SMMU500FaultCheckAll(unit);
    if (status != OS_EOK) {
        return status;
    }

    unit->StartProgress = SMMU500StartupSync;
    SMMU500SyncPrepare(&unit->StartupSync, SMMU500SyncAll, 0);

    status = SMMU500SyncRun(unit, &unit->StartupSync, deadline);
    if (status != OS_EOK) {
        return status;
    }

    unit->StartProgress = SMMU500StartupComplete;
    return OS_EOK;
}

// Allocate the control memory pool and initialize hardware within one 100 ms
// wait budget. Called with the unit mutex held.
static oserr_t
__StartLocked(
    _In_ void* context)
{
    SMMU500Unit_t*    unit = context;
    ArmSMMUDeadline_t deadline;
    oserr_t           status;

    status = DMAIOMMUUnitCheck(unit->Common.Backend);
    if (status != OS_EOK) {
        return status;
    }

    status = ArmSMMUDeadlineBegin(ARM_SMMU_TIMEOUT_NS, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = ArmSMMUMemoryInitialize(
        &unit->Common.Memory,
        ARM_SMMU_MEMORY_MAX_SIZE,
        unit->Stage2.PhysicalLimit
    );
    if (status != OS_EOK) {
        ERROR("__StartLocked: allocating control arena at %llx failed: %u",
              unit->Common.Description.RegisterBase, status);
        if (unit->Common.Memory.State == ArmSMMUMemoryFailed) {
            ArmSMMUUnitFail(&unit->Common, status);
        }
        return status;
    }

    status = __StartHardware(unit, &deadline);
    if (status != OS_EOK) {
        ERROR("__StartLocked: startup at %llx failed in phase %u: %u",
              unit->Common.Description.RegisterBase, (uint32_t)unit->StartProgress, status);
        return status;
    }

    status = DMAIOMMUUnitCheck(unit->Common.Backend);
    if (status != OS_EOK) {
        WARNING("__StartLocked: unit failed during startup at %llx", unit->Common.Description.RegisterBase);
    }
    return status;
}

oserr_t
SMMU500UnitStart(
    _In_ SMMU500Unit_t* unit)
{
    if (unit == NULL) {
        WARNING("SMMU500UnitStart: missing unit");
        return OS_EINVALPARAMS;
    }
    return ArmSMMUUnitStart(&unit->Common, __StartLocked, unit);
}

DMAIOMMUUnit_t*
SMMU500UnitGetBackend(
    _In_ SMMU500Unit_t* unit)
{
    return unit == NULL ? NULL : unit->Common.Backend;
}

// Fault poll callback run inside ArmSMMUUnitPoll with the unit mutex held.
static oserr_t
__PollLocked(
    _In_ void* context)
{
    __SMMU500PollRequest_t* request = context;

    return SMMU500FaultPoll(
        request->Unit,
        request->Faults,
        request->Capacity,
        &request->Count
    );
}

oserr_t
SMMU500UnitPoll(
    _In_  SMMU500Unit_t*   unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut)
{
    __SMMU500PollRequest_t request;
    oserr_t                status;

    if (countOut != NULL) {
        *countOut = 0;
    }

    if (unit == NULL || faults == NULL || countOut == NULL || capacity == 0) {
        ERROR("SMMU500UnitPoll: invalid arguments, unit %p, faults %p, capacity %u",
              (void*)unit, (void*)faults, capacity);
        return OS_EINVALPARAMS;
    }

    request.Unit = unit;
    request.Faults = faults;
    request.Capacity = capacity;
    request.Count = 0;
    
    status = ArmSMMUUnitPoll(&unit->Common, __PollLocked, &request);
    *countOut = request.Count;
    return status;
}

// Switch remaining stream slots to fault mode, resuming from CloseIndex if a
// previous shutdown attempt timed out.
static oserr_t
__StopSlots(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    while (unit->CloseIndex < unit->SlotCount) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__StopSlots: unit %llx timed out at slot %u of %u",
                    unit->Common.Description.RegisterBase, unit->CloseIndex, unit->SlotCount);
            return OS_ETIMEOUT;
        }

        status = SMMU500SlotSetFault(unit, unit->CloseIndex);
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseIndex++;
    }
    return OS_EOK;
}

// Turn off remaining context banks, resuming from CloseIndex after a timeout.
static oserr_t
__StopBanks(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    uint32_t bankOffset;
    oserr_t  status;

    while (unit->CloseIndex < unit->ContextCount) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__StopBanks: unit %llx timed out at bank %u of %u",
                    unit->Common.Description.RegisterBase, unit->CloseIndex, unit->ContextCount);
            return OS_ETIMEOUT;
        }

        bankOffset = __SMMU500ContextBankOffset(unit, unit->CloseIndex);
        
        status = SMMU500WriteChecked(
            unit,
            bankOffset + SMMU500_CB_SCTLR,
            0,
            SMMU500_SCTLR_FIELDS
        );
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseIndex++;
    }
    return OS_EOK;
}

// Shut down hardware in resumable steps: wait for any active sync, set global
// CR0 to fault mode, fault all stream slots, disable all context banks, and
// flush the TLB before releasing control memory.
static oserr_t
__StopHardware(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    status = SMMU500SyncWait(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    if (unit->CloseProgress == SMMU500CloseInitial) {
        status = __WriteFaultingControls(unit);
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseProgress = SMMU500CloseSlots;
    }

    if (unit->CloseProgress == SMMU500CloseSlots) {
        status = __StopSlots(unit, deadline);
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseProgress = SMMU500CloseBanks;
        unit->CloseIndex = 0;
    }

    if (unit->CloseProgress == SMMU500CloseBanks) {
        status = __StopBanks(unit, deadline);
        if (status != OS_EOK) {
            return status;
        }
        SMMU500SyncPrepare(&unit->ShutdownSync, SMMU500SyncAll, 0);
        unit->CloseProgress = SMMU500CloseSync;
    }

    status = SMMU500SyncRun(unit, &unit->ShutdownSync, deadline);
    if (status != OS_EOK) {
        return status;
    }

    unit->CloseProgress = SMMU500CloseMemory;
    return OS_EOK;
}

// Stop hardware if startup wrote any registers, then free the control memory
// pool. Held under the unit mutex. Probe-only units skip register writes.
static oserr_t
__ShutdownLocked(
    _In_ void* context)
{
    SMMU500Unit_t*    unit = context;
    ArmSMMUDeadline_t deadline;
    oserr_t           status;

    if (unit->StartProgress != SMMU500NoWrites && unit->CloseProgress != SMMU500CloseMemory) {
        status = ArmSMMUDeadlineBegin(ARM_SMMU_TIMEOUT_NS, &deadline);
        if (status != OS_EOK) {
            return status;
        }

        status = __StopHardware(unit, &deadline);
        if (status != OS_EOK) {
            ERROR("__ShutdownLocked: hardware shutdown at %llx failed: %u",
                  unit->Common.Description.RegisterBase, status);
            return status;
        }
    }

    status = ArmSMMUMemoryDestroy(&unit->Common.Memory);
    if (status != OS_EOK) {
        ERROR("__ShutdownLocked: arena destruction at %llx failed: %u",
              unit->Common.Description.RegisterBase, status);
    }
    return status;
}

oserr_t
SMMU500UnitDestroy(
    _InOut_ SMMU500Unit_t** unit)
{
    SMMU500Unit_t* unitToDestroy;
    oserr_t        status;

    if (unit == NULL || *unit == NULL) {
        return OS_EOK;
    }
    
    unitToDestroy = *unit;
    
    // Keep unitToDestroy and *unit intact if teardown times out or stays
    // pinned so callers can inspect diagnostics or retry Destroy later.
    status = ArmSMMUUnitDestroy(
        &unitToDestroy->Common,
        __ShutdownLocked,
        unitToDestroy
    );
    if (status != OS_EOK) {
        return status;
    }
    kfree(unitToDestroy->Slots);
    kfree(unitToDestroy->Contexts);
    kfree(unitToDestroy);
    
    *unit = NULL;
    return OS_EOK;
}
