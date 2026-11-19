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
 * SMMUv3 controller probe, startup, polling, and checked teardown.
 * Handles reading hardware capability registers, bringing up the command and
 * event queues, enabling stage-2 translation, and shutting down the unit
 * safely. Before starting a unit, the caller must stop all device DMA traffic
 * and make sure firmware is no longer touching its registers.
 */

#include "private.h"
#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

// Dispatch table registered with the generic kernel DMA IOMMU layer.
static const DMAIOMMUBackendOps_t g_operations = {
    .CreateSpace = SMMUv3SpaceCreate,
    .DestroySpace = SMMUv3SpaceDestroy,
    .PrepareMapping = SMMUv3MappingPrepare,
    .InstallMapping = SMMUv3MappingInstall,
    .RemoveMapping = SMMUv3MappingRemove,
    .DestroyMapping = SMMUv3MappingDestroy,
    .PrepareHardwareIDs = SMMUv3HardwareIDsPrepare,
    .ConnectHardwareIDs = SMMUv3HardwareIDsConnect,
    .DisconnectHardwareIDs = SMMUv3HardwareIDsDisconnect,
    .DestroyHardwareIDs = SMMUv3HardwareIDsDestroy
};

// Verify that this controller supports every feature the driver relies on.
// Each check logs which capability is missing so failed probes are easy to
// diagnose from boot logs.
static oserr_t
__CheckHardwareSupport(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      id0,
    _In_ uint32_t      id1,
    _In_ uint32_t      id5,
    _In_ uint32_t      architectureRevision)
{
    uint32_t required = SMMUV3_IDR0_STAGE2 | SMMUV3_IDR0_AARCH64 | SMMUV3_IDR0_COHERENT;
    uint32_t tableEndianness = (id0 >> SMMUV3_IDR0_ENDIAN_SHIFT) & 3U;
    uint32_t stallMode = (id0 >> SMMUV3_IDR0_STALL_SHIFT) & 3U;
    uint32_t streamTableFormat = (id0 >> SMMUV3_IDR0_TABLE_SHIFT) & 3U;
    uint32_t outputAddressEncoding = id5 & SMMUV3_IDR5_OAS_MASK;

    // AIDR encodes 0..4 for SMMUv3.0 through SMMUv3.4. Higher values are unknown.
    if (architectureRevision > 4U) {
        ERROR("__CheckHardwareSupport: unknown architecture revision %x at %llx",
              architectureRevision, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if (!unit->Common.Description.TableCoherent || (id0 & required) != required) {
        ERROR("__CheckHardwareSupport: stage-2, AArch64 tables and coherent table walks are required at %llx",
              unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    // Require little-endian table support (0 = mixed, 2 = little-endian only),
    // non-stalling fault support (stall mode 2 forces stalling on faults, which
    // this driver cannot resume), and two-level stream tables (format 1).
    if ((tableEndianness != 0 && tableEndianness != 2) || stallMode > 1 || streamTableFormat != 1) {
        ERROR("__CheckHardwareSupport: unsupported endian %u, stall %u or table format %u at %llx",
              tableEndianness, stallMode, streamTableFormat, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    // Reject implementations with hardcoded preset table or queue addresses,
    // because this driver allocates all control memory dynamically in RAM.
    if ((id1 & SMMUV3_IDR1_PRESET) != 0) {
        ERROR("__CheckHardwareSupport: fixed table or queue bases are not supported at %llx",
              unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    // Require 4 KiB page support and a valid physical output width (0..6).
    if ((id5 & SMMUV3_IDR5_GRANULE4K) == 0 || outputAddressEncoding >= 7U) {
        ERROR("__CheckHardwareSupport: 4 KiB pages or a known output address size are missing at %llx, IDR5 %x",
              unit->Common.Description.RegisterBase, id5);
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

// Read the hardware Stream ID width and queue limits from IDR1, reject
// reserved encodings, and cap them to the sizes this backend allocates.
static oserr_t
__ReadCapacities(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      id1)
{
    unit->SIDBits = id1 & SMMUV3_IDR1_SID_MASK;
    unit->CommandBits = (id1 >> SMMUV3_IDR1_CMD_SHIFT) & SMMUV3_FIELD5_MASK;
    unit->EventBits = (id1 >> SMMUV3_IDR1_EVENT_SHIFT) & SMMUV3_FIELD5_MASK;

    // SIDBits must cover at least one 64-entry L2 page (6 bits) up to 32 bits.
    // Queue log2 sizes above 19 are reserved by the architecture.
    if (unit->SIDBits < SMMUV3_STREAM_SPLIT || unit->SIDBits > 32U ||
        unit->CommandBits < 2U || unit->CommandBits > 19U ||
        unit->EventBits < 1U || unit->EventBits > 19U) {
        ERROR("__ReadCapacities: unsupported SID or queue size at %llx, IDR1 %x",
              unit->Common.Description.RegisterBase, id1);
        return OS_ENOTSUPPORTED;
    }

    // Cap SIDBits at 16 (65,536 streams = 1,024 L1 slots) to bound metadata size.
    if (unit->SIDBits > 16U) {
        unit->SIDBits = 16U;
    }

    // Fit each ring inside one 4 KiB page: 256 command entries (8 bits x 16
    // bytes) and 128 event entries (7 bits x 32 bytes). Smaller hardware maxima
    // reduce the usable entry count while keeping the 4 KiB page allocation.
    if (unit->CommandBits > 8U) {
        unit->CommandBits = 8U;
    }

    if (unit->EventBits > 7U) {
        unit->EventBits = 7U;
    }
    return OS_EOK;
}

// Read hardware identification registers without writing anything to the unit,
// then compute address limits, stage-2 walk depth, VMID count, and ATS flags.
static oserr_t
__ProbeHardware(
    _In_ SMMUv3Unit_t* unit)
{
    static const uint32_t addressBits[] = { 32, 36, 40, 42, 44, 48, 52 };
    uint32_t              id0 = __SMMUv3Read32(unit->Common.Registers, SMMUV3_IDR0);
    uint32_t              id1 = __SMMUv3Read32(unit->Common.Registers, SMMUV3_IDR1);
    uint32_t              id5 = __SMMUv3Read32(unit->Common.Registers, SMMUV3_IDR5);
    uint32_t              architectureRevision = __SMMUv3Read32(unit->Common.Registers, SMMUV3_AIDR);
    uint32_t              outputAddressEncoding = id5 & SMMUV3_IDR5_OAS_MASK;
    oserr_t               status;

    TRACE("__ProbeHardware: unit %llx, IIDR %x, AIDR %x, IDR0 %x, IDR1 %x, IDR5 %x",
          unit->Common.Description.RegisterBase, __SMMUv3Read32(unit->Common.Registers, SMMUV3_IIDR),
          architectureRevision, id0, id1, id5);
    
    status = __CheckHardwareSupport(unit, id0, id1, id5, architectureRevision);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadCapacities(unit, id1);
    if (status != OS_EOK) {
        return status;
    }

    // The shared stage-2 page-table code handles up to 48-bit addresses (level 0
    // for 40..48 bits, level 1 for 32..39 bits), so 52-bit hardware runs at 48.
    unit->AddressEncoding = outputAddressEncoding > 5U ? 5U : outputAddressEncoding;
    unit->AddressBits = addressBits[unit->AddressEncoding];
    unit->Stage2.AddressLimit = (1ULL << unit->AddressBits) - 1ULL;
    unit->Stage2.PhysicalLimit = unit->Stage2.AddressLimit;
    unit->Stage2.RootLevel = unit->AddressBits > 39U ? 0U : 1U;
    unit->VMIDCount = (id0 & SMMUV3_IDR0_VMID16) != 0 ? 65536U : 256U;
    unit->ATSSupported = (id0 & SMMUV3_IDR0_ATS) != 0;
    return OS_EOK;
}

// Allocate the per-L1-slot tracking array and the VMID allocation bitmap.
// Mark VMID 0 used up front so zeroed hardware entries never match a live space.
static oserr_t
__AllocateSoftwareState(
    _In_ SMMUv3Unit_t* unit)
{
    size_t tableCount = 1U << (unit->SIDBits - SMMUV3_STREAM_SPLIT);

    unit->StreamTables = kmalloc(tableCount * sizeof(*unit->StreamTables));
    if (unit->StreamTables == NULL) {
        ERROR("__AllocateSoftwareState: allocating %u stream table records at %llx failed",
              (uint32_t)tableCount, unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(unit->StreamTables, 0, tableCount * sizeof(*unit->StreamTables));

    unit->VMIDs = kmalloc(unit->VMIDCount / 8U);
    if (unit->VMIDs == NULL) {
        ERROR("__AllocateSoftwareState: allocating VMID bitmap at %llx failed",
              unit->Common.Description.RegisterBase);
        kfree(unit->StreamTables);
        unit->StreamTables = NULL;
        return OS_EOOM;
    }

    memset(unit->VMIDs, 0, unit->VMIDCount / 8U);
    unit->VMIDs[0] = 1ULL;
    return OS_EOK;
}

oserr_t
SMMUv3UnitCreate(
    _In_    const ArmSMMUDescription_t* description,
    _InOut_ SMMUv3Unit_t**              unitOut)
{
    SMMUv3Unit_t*        unit;
    DMAIOMMUUnitConfig_t config;
    oserr_t              status;

    if (description == NULL || unitOut == NULL) {
        ERROR("SMMUv3UnitCreate: missing description or output pointer");
        return OS_EINVALPARAMS;
    }

    if (*unitOut != NULL) {
        WARNING("SMMUv3UnitCreate: output already contains a unit");
        return OS_EBUSY;
    }

    // Require the standard 128 KiB two-page register window aligned to 64 KiB,
    // and check for address wraparound before mapping MMIO.
    if (description->Kind != ArmSMMUv3 || description->RegisterLength != SMMUV3_REGISTER_SIZE ||
        (description->RegisterBase & 0xFFFFULL) != 0 ||
        description->RegisterBase > UINT64_MAX - (SMMUV3_REGISTER_SIZE - 1U)) {
        ERROR("SMMUv3UnitCreate: unsupported register window %llx, length %llx",
              description->RegisterBase, description->RegisterLength);
        return OS_ENOTSUPPORTED;
    }

    unit = kmalloc(sizeof(SMMUv3Unit_t));
    if (unit == NULL) {
        ERROR("SMMUv3UnitCreate: allocating unit at %llx failed", description->RegisterBase);
        return OS_EOOM;
    }

    memset(unit, 0, sizeof(SMMUv3Unit_t));

    // Map registers and read hardware IDs without writing anything or exposing
    // RAM to the controller.
    status = ArmSMMUUnitCommonInitialize(&unit->Common, description);
    if (status != OS_EOK) {
        kfree(unit);
        return status;
    }

    SMMUv3Stage2Initialize(unit);

    status = __ProbeHardware(unit);
    if (status != OS_EOK) {
        kfree(unit);
        return status;
    }

    status = __AllocateSoftwareState(unit);
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
        ERROR("SMMUv3UnitCreate: creating generic unit failed: %u", status);
        kfree(unit->VMIDs);
        kfree(unit->StreamTables);
        kfree(unit);
        return status;
    }

    *unitOut = unit;
    TRACE("SMMUv3UnitCreate: probed unit at %llx, %u SID bits, %u address bits, %u VMIDs",
          description->RegisterBase, unit->SIDBits, unit->AddressBits, unit->VMIDCount);
    return OS_EOK;
}

// Poll a hardware acknowledgement register until the masked bits match expected.
// Runs SMMUv3FaultCheck on every pass, even if the register already matches,
// so a matching handshake bit never hides a global error that broke reliability.
static oserr_t
__WaitRegister(
    _In_ SMMUv3Unit_t*            unit,
    _In_ uint32_t                 offset,
    _In_ uint32_t                 mask,
    _In_ uint32_t                 expected,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    for (;;) {
        status = SMMUv3FaultCheck(unit);
        if (status != OS_EOK) {
            return status;
        }
        
        if ((__SMMUv3Read32(unit->Common.Registers, offset) & mask) == expected) {
            __SMMUv3Barrier();
            return OS_EOK;
        }

        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__WaitRegister: timeout at %llx, offset %x, expected %x",
                    unit->Common.Description.RegisterBase, offset, expected);
            return OS_ETIMEOUT;
        }
    }
}

// Write a new value to CR0 and wait for hardware to echo it in CR0ACK.
// Save Control and StartProgress before the write so SMMUv3UnitDestroy knows
// what acknowledgement is still pending if this call times out.
static oserr_t
__WriteControl(
    _In_ SMMUv3Unit_t*            unit,
    _In_ uint32_t                 control,
    _In_ enum SMMUv3StartProgress progress,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    unit->Control = control;
    unit->StartProgress = progress;
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CR0, unit->Control);
    return __WaitRegister(unit, SMMUV3_CR0ACK, UINT32_MAX, unit->Control, deadline);
}

// Build a command batch that flushes all cached stream entries and all stage-2
// TLB entries, ending with CMD_SYNC so we wait for completion.
static void
__PrepareFullInvalidation(
    _Out_ SMMUv3CommandBatch_t* batch)
{
    memset(batch, 0, sizeof(*batch));
    batch->Count = 3;
    batch->Commands[0].Words[0] = SMMUV3_CMD_CFGI_ALL;
    batch->Commands[0].Words[1] = SMMUV3_CMD_CFGI_ALL_RANGE;
    batch->Commands[1].Words[0] = SMMUV3_CMD_TLBI_ALL;
    batch->Commands[2].Words[0] = SMMUV3_CMD_SYNC;
}

// Create the control-memory pool and carve out the three DMA-visible buffers
// needed by hardware: the L1 stream table root, command ring, and event ring.
static oserr_t
__AllocateControlMemory(
    _In_ SMMUv3Unit_t* unit)
{
    size_t  rootLength = (1U << (unit->SIDBits - SMMUV3_STREAM_SPLIT)) * sizeof(uint64_t);
    oserr_t status;

    status = ArmSMMUMemoryInitialize(
        &unit->Common.Memory,
        ARM_SMMU_MEMORY_MAX_SIZE,
        unit->Stage2.PhysicalLimit
    );
    if (status != OS_EOK) {
        ERROR("__AllocateControlMemory: allocating control memory failed: %u", status);
        return status;
    }

    status = ArmSMMUMemoryAllocate(
        &unit->Common.Memory,
        rootLength,
        rootLength,
        &unit->StreamRoot
    );
    if (status != OS_EOK) {
        ERROR("__AllocateControlMemory: allocating stream root failed: %u", status);
        return status;
    }

    status = ArmSMMUMemoryAllocate(
        &unit->Common.Memory,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        &unit->Commands
    );
    if (status != OS_EOK) {
        ERROR("__AllocateControlMemory: allocating command ring failed: %u", status);
        return status;
    }

    status = ArmSMMUMemoryAllocate(
        &unit->Common.Memory,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        &unit->Events
    );
    if (status != OS_EOK) {
        ERROR("__AllocateControlMemory: allocating event ring failed: %u", status);
        return status;
    }
    return OS_EOK;
}

// Switch the controller to abort-all mode, turn off interrupts, and disable
// translation and queues before changing table/queue base registers.
// GBPA.ABORT stays set for the entire unit lifetime so any DMA arriving while
// translation is off gets aborted instead of bypassing straight to RAM.
static oserr_t
__DisableHardware(
    _In_ SMMUv3Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    uint32_t gbpa;
    oserr_t  status;

    status = __WaitRegister(unit, SMMUV3_GBPA, SMMUV3_GBPA_UPDATE, 0, deadline);
    if (status != OS_EOK) {
        return status;
    }

    gbpa = __SMMUv3Read32(unit->Common.Registers, SMMUV3_GBPA);
    unit->StartProgress = SMMUv3AbortIssued;
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_GBPA, gbpa | SMMUV3_GBPA_ABORT | SMMUV3_GBPA_UPDATE);
    
    status = __WaitRegister(unit, SMMUV3_GBPA, SMMUV3_GBPA_UPDATE, 0, deadline);
    if (status != OS_EOK) {
        return status;
    }
    unit->StartProgress = SMMUv3IrqDisableIssued;
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_IRQ_CTRL, 0);
    
    status = __WaitRegister(unit, SMMUV3_IRQ_CTRLACK, UINT32_MAX, 0, deadline);
    if (status != OS_EOK) {
        return status;
    }
    return __WriteControl(unit, 0, SMMUv3DisableIssued, deadline);
}

// Program memory attributes (CR1/CR2) along with the stream table, command
// queue, and event queue base addresses and reset their ring indices to zero.
// Only safe to call once __DisableHardware has turned off translation and queues.
static void
__WriteTableAndQueueRegisters(
    _In_ SMMUv3Unit_t* unit)
{
    uint32_t cacheControl = SMMUV3_CR1_OUTER_SHARED;
    uint64_t queueAttributes = 0;

    if (unit->Common.Memory.Allocation.Cached) {
        cacheControl |= SMMUV3_CR1_WRITE_BACK;
        queueAttributes = SMMUV3_QUEUE_CACHEABLE;
    }

    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CR1, cacheControl);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CR2, SMMUV3_CR2_RECORD_BAD_SID);
    __SMMUv3Write64(unit->Common.Registers, SMMUV3_STRTAB_BASE,
                   unit->StreamRoot.PhysicalAddress | queueAttributes);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_STRTAB_CFG,
                   SMMUV3_STRTAB_TWO_LEVEL | (SMMUV3_STREAM_SPLIT << SMMUV3_STRTAB_SPLIT_SHIFT) | unit->SIDBits);
    __SMMUv3Write64(unit->Common.Registers, SMMUV3_CMDQ_BASE,
                   unit->Commands.PhysicalAddress | queueAttributes | unit->CommandBits);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CMDQ_PROD, 0);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CMDQ_CONS, 0);
    __SMMUv3Write64(unit->Common.Registers, SMMUV3_EVTQ_BASE,
                   unit->Events.PhysicalAddress | queueAttributes | unit->EventBits);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_EVTQ_PROD, 0);
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_EVTQ_CONS, 0);
    __SMMUv3Barrier();
    unit->StartProgress = SMMUv3BasesWritten;
}

// Bring hardware online in strict order:
// 1. Enable the command queue and flush any stale cached config or TLB state.
// 2. Enable the event queue so fault logging is ready before DMA starts.
// 3. Enable translation (plus ATSCHK on ATS-capable units).
static oserr_t
__EnableHardware(
    _In_ SMMUv3Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    uint32_t control;
    oserr_t  status;

    status = __WriteControl(unit, SMMUV3_CR0_COMMANDS, SMMUv3CommandEnableIssued, deadline);
    if (status != OS_EOK) {
        return status;
    }

    __PrepareFullInvalidation(&unit->StartupBatch);
    unit->StartProgress = SMMUv3StartupInvalidation;

    status = SMMUv3CommandBatchRun(unit, &unit->StartupBatch, deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = __WriteControl(
        unit,
        unit->Control | SMMUV3_CR0_EVENTS,
        SMMUv3EventEnableIssued,
        deadline
    );
    if (status != OS_EOK) {
        return status;
    }

    control = unit->Control | SMMUV3_CR0_ENABLE;
    if (unit->ATSSupported) {
        // ATSCHK is reserved-zero on units without ATS, so setting it there
        // would keep CR0ACK from matching CR0.
        control |= SMMUV3_CR0_ATS_CHECK;
    }
    return __WriteControl(unit, control, SMMUv3TranslationEnableIssued, deadline);
}

// Run the full hardware bring-up sequence: disable old state, write table and
// queue bases, flush caches, and enable translation. Each register write waits
// for its own hardware acknowledgement, and StartProgress tracks where a failed
// start stopped so teardown can finish any in-flight write cleanly.
static oserr_t
__StartHardware(
    _In_ SMMUv3Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    status = __DisableHardware(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    __WriteTableAndQueueRegisters(unit);
    status = __EnableHardware(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }
    unit->StartProgress = SMMUv3StartupComplete;
    return OS_EOK;
}

// Allocate control memory and configure the hardware within one 100 ms budget.
// Entered with the unit mutex held via ArmSMMUUnitStart.
static oserr_t
__StartLocked(
    _In_ void* context)
{
    SMMUv3Unit_t*     unit = context;
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

    status = __AllocateControlMemory(unit);
    if (status != OS_EOK) {
        if (unit->Common.Memory.State == ArmSMMUMemoryFailed) {
            ArmSMMUUnitFail(&unit->Common, status);
        }
        return status;
    }

    if (ArmSMMUDeadlineExpired(&deadline)) {
        WARNING("__StartLocked: control memory allocation used the whole budget at %llx",
                unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
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
        return status;
    }

    TRACE("__StartLocked: unit at %llx is ready, %u SID bits, %u address bits",
          unit->Common.Description.RegisterBase, unit->SIDBits, unit->AddressBits);
    return OS_EOK;
}

oserr_t
SMMUv3UnitStart(
    _In_ SMMUv3Unit_t* unit)
{
    if (unit == NULL) {
        ERROR("SMMUv3UnitStart: missing unit");
        return OS_EINVALPARAMS;
    }
    return ArmSMMUUnitStart(&unit->Common, __StartLocked, unit);
}

DMAIOMMUUnit_t*
SMMUv3UnitGetBackend(
    _In_ SMMUv3Unit_t* unit)
{
    return unit == NULL ? NULL : unit->Common.Backend;
}

// Arguments of one SMMUv3UnitPoll call, passed through ArmSMMUUnitPoll to
// __PollLocked. Count receives the number of records written to Faults.
typedef struct __SMMUv3PollRequest {
    SMMUv3Unit_t*    Unit;
    DMAIOMMUFault_t* Faults;
    uint32_t         Capacity;
    uint32_t         Count;
} __SMMUv3PollRequest_t;

// Drain the event queue once startup has enabled it, or check global errors
// if startup failed before that. Entered with the unit mutex held.
static oserr_t
__PollLocked(
    _In_ void* context)
{
    __SMMUv3PollRequest_t* request = context;
    SMMUv3Unit_t*          unit = request->Unit;
    oserr_t                status;

    if (unit->StartProgress >= SMMUv3EventEnableIssued) {
        return SMMUv3FaultDrain(unit, request->Faults, request->Capacity, &request->Count);
    }

    status = SMMUv3FaultCheck(unit);
    if (status != OS_EOK) {
        return status;
    }
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}

oserr_t
SMMUv3UnitPoll(
    _In_  SMMUv3Unit_t*    unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut)
{
    __SMMUv3PollRequest_t request;
    oserr_t               status;

    if (countOut != NULL) {
        *countOut = 0;
    }

    if (unit == NULL || faults == NULL || countOut == NULL || capacity == 0) {
        ERROR("SMMUv3UnitPoll: invalid arguments, unit %p, faults %p, capacity %u",
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

// Wait for any register writes that startup issued before teardown began.
// Finishing pending acknowledgements avoids overwriting a control register while
// hardware is still acting on the previous write, while skipping stages startup
// never reached (which may still hold firmware settings).
static oserr_t
__FinishStartupWrites(
    _In_ SMMUv3Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    status = __WaitRegister(unit, SMMUV3_GBPA, SMMUV3_GBPA_UPDATE, 0, deadline);
    if (status != OS_EOK) {
        return status;
    }

    if (unit->StartProgress >= SMMUv3IrqDisableIssued) {
        status = __WaitRegister(unit, SMMUV3_IRQ_CTRLACK, UINT32_MAX, 0, deadline);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (unit->StartProgress >= SMMUv3DisableIssued) {
        status = __WaitRegister(unit, SMMUV3_CR0ACK, UINT32_MAX, unit->Control, deadline);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

// Wait for any pending startup writes, then turn off translation in CR0 while
// keeping the queues on so the following invalidation step can drain them.
static oserr_t
__StopTranslation(
    _In_ SMMUv3Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    if (unit->CloseProgress == SMMUv3CloseInitial) {
        status = __FinishStartupWrites(unit, deadline);
        if (status != OS_EOK) {
            return status;
        }

        // If startup stopped before writing STRTAB_BASE, CMDQ_BASE, and
        // EVTQ_BASE, hardware has no pointers to our control memory. Once the
        // GBPA abort update above finishes, the memory can be freed directly.
        if (unit->StartProgress < SMMUv3BasesWritten) {
            unit->CloseProgress = SMMUv3CloseHardwareStopped;
            return OS_EOK;
        }
        unit->Control &= ~(SMMUV3_CR0_ENABLE | SMMUV3_CR0_ATS_CHECK);
        unit->CloseProgress = SMMUv3CloseTranslationIssued;
        __SMMUv3Write32(unit->Common.Registers, SMMUV3_CR0, unit->Control);
    }

    if (unit->CloseProgress == SMMUv3CloseTranslationIssued) {
        status = __WaitRegister(unit, SMMUV3_CR0ACK, UINT32_MAX, unit->Control, deadline);
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseProgress = SMMUv3CloseInvalidation;
        __PrepareFullInvalidation(&unit->ShutdownBatch);
    }
    return OS_EOK;
}

// Shut down hardware in three resumable steps:
// 1. Disable translation so incoming DMA stops using our page tables.
// 2. Flush all cached configs/TLBs and wait for CMD_SYNC if CMDQ was enabled.
// 3. Turn off the command and event queues so hardware stops touching RAM.
// CloseProgress tracks each step so timed-out Destroy calls retry cleanly.
static oserr_t
__StopHardware(
    _In_ SMMUv3Unit_t*            unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    if (unit->CloseProgress <= SMMUv3CloseTranslationIssued) {
        status = __StopTranslation(unit, deadline);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (unit->CloseProgress == SMMUv3CloseInvalidation) {
        if ((unit->Control & SMMUV3_CR0_COMMANDS) != 0) {
            // Waiting for this final CMD_SYNC guarantees every earlier command
            // (including a startup batch that timed out) has finished before
            // the command queue is turned off.
            status = SMMUv3CommandBatchRun(unit, &unit->ShutdownBatch, deadline);
            if (status != OS_EOK) {
                return status;
            }
        }
        unit->Control = 0;
        unit->CloseProgress = SMMUv3CloseQueuesIssued;
        __SMMUv3Write32(unit->Common.Registers, SMMUV3_CR0, 0);
    }
    
    if (unit->CloseProgress == SMMUv3CloseQueuesIssued) {
        status = __WaitRegister(unit, SMMUV3_CR0ACK, UINT32_MAX, 0, deadline);
        if (status != OS_EOK) {
            return status;
        }
        unit->CloseProgress = SMMUv3CloseHardwareStopped;
    }
    return OS_EOK;
}

static oserr_t
__FreeControlBlock(
    _In_ SMMUv3Unit_t*         unit,
    _In_ ArmSMMUMemoryBlock_t* block)
{
    oserr_t status;

    if (block->Length == 0) {
        return OS_EOK;
    }

    status = ArmSMMUMemoryFree(&unit->Common.Memory, block);
    if (status != OS_EOK) {
        ERROR("__FreeControlBlock: freeing control block at %llx failed: %u",
              (uint64_t)block->PhysicalAddress, status);
        ArmSMMUUnitFail(&unit->Common, status);
    }
    return status;
}

// Return all stream tables, queue rings, and the backing control-memory pool.
// Hardware must already be stopped so it can no longer access these pages.
static oserr_t
__FreeControlMemory(
    _In_ SMMUv3Unit_t* unit)
{
    oserr_t status;

    for (uint32_t index = 0; index < (1U << (unit->SIDBits - SMMUV3_STREAM_SPLIT)); index++) {
        status = __FreeControlBlock(unit, &unit->StreamTables[index].Block);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = __FreeControlBlock(unit, &unit->StreamRoot);
    if (status != OS_EOK) {
        return status;
    }

    status = __FreeControlBlock(unit, &unit->Commands);
    if (status != OS_EOK) {
        return status;
    }

    status = __FreeControlBlock(unit, &unit->Events);
    if (status != OS_EOK) {
        return status;
    }

    status = ArmSMMUMemoryDestroy(&unit->Common.Memory);
    if (status != OS_EOK) {
        ERROR("__FreeControlMemory: releasing control memory at %llx failed: %u",
              unit->Common.Description.RegisterBase, status);
        return status;
    }
    return OS_EOK;
}

// Stop hardware if Start wrote any registers, then free control memory.
// Entered with the unit mutex held via ArmSMMUUnitDestroy. A timeout can be
// retried safely from the saved CloseProgress.
static oserr_t
__ShutdownLocked(
    _In_ void* context)
{
    SMMUv3Unit_t*     unit = context;
    ArmSMMUDeadline_t deadline;
    oserr_t           status;

    if (unit->StartProgress != SMMUv3NoWrites && unit->CloseProgress != SMMUv3CloseHardwareStopped) {
        status = ArmSMMUDeadlineBegin(ARM_SMMU_TIMEOUT_NS, &deadline);
        if (status != OS_EOK) {
            return status;
        }

        status = __StopHardware(unit, &deadline);
        if (status != OS_EOK) {
            ERROR("__ShutdownLocked: hardware shutdown at %llx failed in phase %u: %u",
                  unit->Common.Description.RegisterBase, (uint32_t)unit->CloseProgress, status);
            return status;
        }
    }
    return __FreeControlMemory(unit);
}

oserr_t
SMMUv3UnitDestroy(
    _InOut_ SMMUv3Unit_t** unit)
{
    SMMUv3Unit_t* instance;
    oserr_t       status;

    if (unit == NULL || *unit == NULL) {
        return OS_EOK;
    }

    instance = *unit;

    status = ArmSMMUUnitDestroy(&instance->Common, __ShutdownLocked, instance);
    if (status != OS_EOK) {
        return status;
    }

    // Free software tracking tables only after hardware shutdown and generic
    // DMA unit teardown have both completed cleanly.
    kfree(instance->VMIDs);
    kfree(instance->StreamTables);
    kfree(instance);
    
    *unit = NULL;
    return OS_EOK;
}
