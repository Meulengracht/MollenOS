/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

//#define __TRACE

#include <bus/pci/msix.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#define MSIX_ENABLE        0x8000U
#define MSIX_FUNCTION_MASK 0x4000U
#define MSIX_VECTOR_MASK   1U
#define MSIX_COMMAND_BITS  0x0402U
#define MSIX_ENTRY_SIZE    16U

/** @brief Saved entry plus an immutable requested payload for retry validation. */
struct __PciMsixEntry {
    uint32_t Original[4];
    uint32_t Message[3];
    int Supplied;
    int Programmed;
};

/**
 * @brief One function's reservation, including progress through partial cleanup.
 *
 * Entries are snapshotted once before any table write. TableRestored means table
 * access is finished, even if unmapping or final config restoration needs retry.
 */
struct PciMsixState {
    struct PciMsixLayout Layout;
    struct __PciMsixEntry* Entries;
    void* Mapping;
    uuid_t Allocation;
    uint32_t Count;
    uint16_t Command;
    int ConfigurationTouched;
    int Snapshot;
    int Prepared;
    int MayDeliver;
    int Enabled;
    int Releasing;
    int TableRestored;
};

static oserr_t
__VerifyRegionBar(
    _In_ const struct MsixDeviceOperations* access,
    _In_ uint32_t                           barIndex)
{
    const struct PciBar* bar = &access->Bars[barIndex];

    // Only firmware-assigned BARs have a usable window to map.
    if (bar->State != PciBarAssigned) {
        ERROR("__VerifyRegionBar BAR%u is not assigned (state %u)", barIndex, bar->State);
        return OS_EINVALPARAMS;
    }

    // The table and pending array must live in memory space, either 32-bit
    // (space 2) or 64-bit (space 3).
    if (bar->Space != 2 && bar->Space != 3) {
        ERROR("__VerifyRegionBar BAR%u is not a memory BAR (space %u)", barIndex, bar->Space);
        return OS_EINVALPARAMS;
    }

    // The upper half of a 64-bit BAR pair is not addressable on its own.
    if (barIndex != 0 && access->Bars[barIndex - 1].Space == 3) {
        ERROR("__VerifyRegionBar BAR%u is the upper half of a 64-bit pair", barIndex);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static oserr_t
__ReadRawBarAddress(
    _In_  const struct MsixDeviceOperations* access,
    _In_  uint32_t                           barIndex,
    _In_  uint32_t                           space,
    _Out_ uint64_t*                          addressOut)
{
    uint64_t address;
    uint32_t low;

    low = access->Config.Read(access->Config.Context, 0x10 + barIndex * 4, 4);

    // The raw encoding must still agree with the recorded BAR type, otherwise
    // the window we were handed no longer describes this register.
    if (low & 1U) {
        ERROR("__ReadRawBarAddress BAR%u now decodes as io space (0x%x)", barIndex, low);
        return OS_EINVALPARAMS;
    }

    if ((low & 6U) != (space == 3 ? 4U : 0U)) {
        ERROR("__ReadRawBarAddress BAR%u type 0x%x does not match space %u", barIndex, low, space);
        return OS_EINVALPARAMS;
    }

    address = low & ~15U;
    if (space == 3) {
        // A 64-bit BAR consumes the following register for its upper half, so
        // the last BAR can never start a pair.
        if (barIndex == 5) {
            ERROR("__ReadRawBarAddress BAR5 cannot hold a 64-bit address");
            return OS_EINVALPARAMS;
        }

        address |= (uint64_t)access->Config.Read(
            access->Config.Context, 0x14 + barIndex * 4, 4
        ) << 32U;
    }

    *addressOut = address;
    return OS_EOK;
}

static oserr_t
__MsixRegion(
    _In_  const struct MsixDeviceOperations* access,
    _In_  uint32_t                           encoded,
    _In_  size_t                             length,
    _Out_ struct PciMsixRegion*              region)
{
    const struct PciBar* bar;
    uint32_t             barIndex = encoded & 7U;
    uint32_t             offset = encoded & ~7U;
    uint64_t             address;
    oserr_t              status;

    TRACE("__MsixRegion(encoded=0x%x, length=%u)", encoded, (unsigned)length);

    if (barIndex >= 6) {
        ERROR("__MsixRegion encoding 0x%x refers to a non-existing BAR%u", encoded, barIndex);
        return OS_EINVALPARAMS;
    }

    status = __VerifyRegionBar(access, barIndex);
    if (status != OS_EOK) {
        return status;
    }

    bar = &access->Bars[barIndex];
    status = __ReadRawBarAddress(access, barIndex, bar->Space, &address);
    if (status != OS_EOK) {
        return status;
    }

    // Raw configuration writes before reservation may have moved the BAR.
    if (address != bar->BusAddress) {
        ERROR("__MsixRegion BAR%u moved from 0x%llx to 0x%llx", barIndex,
              (unsigned long long)bar->BusAddress, (unsigned long long)address);
        return OS_EINVALPARAMS;
    }

    if (offset >= bar->Size) {
        ERROR("__MsixRegion offset 0x%x is outside BAR%u (size 0x%llx)", offset, barIndex,
              (unsigned long long)bar->Size);
        return OS_EINVALPARAMS;
    }

    if (length > bar->Size - offset) {
        ERROR("__MsixRegion region of %u bytes at 0x%x does not fit BAR%u",
              (unsigned)length, offset, barIndex);
        return OS_EINVALPARAMS;
    }

    if (bar->Size - 1 > UINT64_MAX - address) {
        ERROR("__MsixRegion BAR%u window wraps the bus address space", barIndex);
        return OS_EINVALPARAMS;
    }

    // The window must be reachable by this platform's pointer width before we
    // can hand it to the mapping layer.
    if (bar->CpuAddress > UINTPTR_MAX || offset > UINTPTR_MAX - bar->CpuAddress) {
        ERROR("__MsixRegion BAR%u is not addressable by the cpu", barIndex);
        return OS_ENOTSUPPORTED;
    }

    address = bar->CpuAddress + offset;
    if (length - 1 > UINTPTR_MAX - address) {
        ERROR("__MsixRegion region at 0x%llx wraps the cpu address space",
              (unsigned long long)address);
        return OS_ENOTSUPPORTED;
    }

    // MSI-X structures are naturally aligned. Reject an unaligned start because
    // the BAR encoding may be invalid.
    if (address & 7U) {
        ERROR("__MsixRegion region at 0x%llx is not qword aligned",
              (unsigned long long)address);
        return OS_ENOTSUPPORTED;
    }

    region->Bar = barIndex;
    region->Offset = offset;
    region->Length = length;
    region->Address = address;
    TRACE("__MsixRegion bar=%u, offset=0x%x, address=0x%llx", barIndex, offset,
          (unsigned long long)address);
    return OS_EOK;
}

oserr_t
PciMsixProbe(
    _In_  const struct MsixDeviceOperations* access,
    _Out_ struct PciMsixLayout*              layout)
{
    struct PciInterruptCapabilities capabilities;
    struct PciMsixLayout            result = { 0 };
    uint32_t                        table;
    uint32_t                        pending;
    oserr_t                         status;

    status = PciInterruptProbe(&access->Config, &capabilities);
    if (status != OS_EOK) {
        TRACE("PciMsixProbe capability scan failed: %u", status);
        return status;
    }

    if (capabilities.MsixOffset == 0) {
        TRACE("PciMsixProbe function has no MSI-X capability");
        return OS_ENOTSUPPORTED;
    }

    result.Capability = capabilities.MsixOffset;
    result.Count = capabilities.MsixCapacity;
    result.Control = access->Config.Read(access->Config.Context, result.Capability + 2, 2);

    table = access->Config.Read(access->Config.Context, result.Capability + 4, 4);
    pending = access->Config.Read(access->Config.Context, result.Capability + 8, 4);
    TRACE("PciMsixProbe capability=0x%x, count=%u, table=0x%x, pending=0x%x",
          result.Capability, result.Count, table, pending);

    status = __MsixRegion(access, table, result.Count * MSIX_ENTRY_SIZE, &result.Table);
    if (status != OS_EOK) {
        ERROR("PciMsixProbe table encoding 0x%x is unusable: %u", table, status);
        return status;
    }

    status = __MsixRegion(access, pending, ((result.Count + 63U) / 64U) * 8U, &result.Pending);
    if (status != OS_EOK) {
        ERROR("PciMsixProbe pending array encoding 0x%x is unusable: %u", pending, status);
        return status;
    }

    // Compare translated ranges too: two distinct BARs can alias the same memory.
    if (result.Table.Address <= result.Pending.Address + result.Pending.Length - 1 &&
        result.Pending.Address <= result.Table.Address + result.Table.Length - 1) {
        ERROR("PciMsixProbe table and pending array overlap in memory");
        return OS_EINVALPARAMS;
    }

    *layout = result;
    return OS_EOK;
}

static oserr_t
__MsixConfigWrite(
    _In_ const struct MsixDeviceOperations* access,
    _In_ size_t                             offset,
    _In_ uint32_t                           value) 
{
    uint32_t readback;

    TRACE("__MsixConfigWrite(offset=%u, value=0x%x)", (unsigned)offset, value);

    access->Config.Write(access->Config.Context, offset, value, 2);

    // Confirm the write by reading the register back.
    readback = access->Config.Read(access->Config.Context, offset, 2);
    if (readback != value) {
        ERROR("__MsixConfigWrite offset %u did not accept 0x%x, read back 0x%x",
              (unsigned)offset, value, readback);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__MsixTableWrite(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state,
    _In_ uint32_t                           index,
    _In_ uint32_t                           field,
    _In_ uint32_t                           value)
{
    size_t   offset = index * MSIX_ENTRY_SIZE + field * 4U;
    uint32_t readback;

    access->Write(state->Mapping, offset, value);

    // Verify each table write by reading the entry back.
    readback = access->Read(state->Mapping, offset);
    if (readback != value) {
        ERROR("__MsixTableWrite entry %u field %u did not accept 0x%x, read back 0x%x",
              index, field, value, readback);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__MsixStop(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    oserr_t status;

    TRACE("__MsixStop(capability=0x%x)", state->Layout.Capability);

    // Function Mask stops delivery for every entry at once, which is what makes
    // table access safe again.
    status = __MsixConfigWrite(access, state->Layout.Capability + 2,
        state->Layout.Control | MSIX_ENABLE | MSIX_FUNCTION_MASK);
    if (status != OS_EOK) {
        ERROR("__MsixStop failed to mask the function: %u", status);
        return status;
    }

    state->MayDeliver = 0;
    state->Enabled = 0;
    return OS_EOK;
}

static void
__SnapshotTable(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    // Save the full table before writing, so Release can restore its original
    // contents.
    for (uint32_t i = 0; i < state->Layout.Count; i++) {
        for (uint32_t field = 0; field < 4; field++) {
            state->Entries[i].Original[field] = access->Read(
                state->Mapping, i * MSIX_ENTRY_SIZE + field * 4U
            );
        }
    }
    state->Snapshot = 1;
}

static oserr_t
__MaskAllEntries(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    oserr_t status;

    for (uint32_t i = 0; i < state->Layout.Count; i++) {
        status = __MsixTableWrite(access, state, i, 3,
            state->Entries[i].Original[3] | MSIX_VECTOR_MASK);
        if (status != OS_EOK) {
            ERROR("__MaskAllEntries failed to mask entry %u: %u", i, status);
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__MsixPrepare(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    uint32_t command;
    oserr_t  status;

    if (state->Prepared) {
        return OS_EOK;
    }

    TRACE("__MsixPrepare(count=%u of %u)", state->Count, state->Layout.Count);

    if (state->Mapping == NULL) {
        status = access->Map(access->Config.Context, &state->Layout.Table, &state->Mapping);
        if (status != OS_EOK) {
            ERROR("__MsixPrepare failed to map the message table: %u", status);
            return status;
        }
    }

    command = access->Config.Read(access->Config.Context, 4, 2) | MSIX_COMMAND_BITS;
    state->ConfigurationTouched = 1;

    status = __MsixConfigWrite(access, 4, command);
    if (status != OS_EOK) {
        ERROR("__MsixPrepare failed to enable memory space and disable INTx: %u", status);
        return status;
    }

    // Some devices require MSI-X Enable for table access. Function Mask keeps
    // all delivery stopped while the snapshot and per-entry masks are prepared.
    status = __MsixStop(access, state);
    if (status != OS_EOK) {
        ERROR("__MsixPrepare failed to quiesce the function: %u", status);
        return status;
    }

    if (!state->Snapshot) {
        __SnapshotTable(access, state);
    }

    status = __MaskAllEntries(access, state);
    if (status != OS_EOK) {
        return status;
    }

    state->Prepared = 1;
    return OS_EOK;
}

static oserr_t
__MsixReserve(
    _In_    const struct MsixDeviceOperations* access,
    _InOut_ struct PciMsixState**              stateOut,
    _In_    const struct PciInterruptRequest*  request)
{
    struct PciMsixLayout layout;
    struct PciMsixState* state = *stateOut;
    oserr_t              status;

    TRACE("__MsixReserve(count=%u, allocation=%u)", request->Count, request->Allocation);

    if (state != NULL) {
        // A retry must ask for the same table size, otherwise the snapshot and
        // the entry array no longer describe what the caller expects.
        if (state->Count != request->Count) {
            ERROR("__MsixReserve count changed from %u to %u between attempts",
                  state->Count, request->Count);
            return OS_EINVALPARAMS;
        }
        return __MsixPrepare(access, state);
    }

    status = PciMsixProbe(access, &layout);
    if (status != OS_EOK) {
        WARNING("__MsixReserve function does not provide usable MSI-X: %u", status);
        return status;
    }

    if (request->Count > layout.Count) {
        ERROR("__MsixReserve requested %u messages, hardware provides %u",
              request->Count, layout.Count);
        return OS_EINVALPARAMS;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        ERROR("__MsixReserve failed to allocate reservation state");
        return OS_EOOM;
    }

    state->Entries = calloc(layout.Count, sizeof(*state->Entries));
    if (state->Entries == NULL) {
        ERROR("__MsixReserve failed to allocate %u table entries", layout.Count);
        free(state);
        return OS_EOOM;
    }

    state->Layout = layout;
    state->Count = request->Count;
    state->Allocation = request->Allocation;
    state->Command = access->Config.Read(access->Config.Context, 4, 2);

    // Hand ownership of the state to the caller before preparing hardware, so a
    // failed preparation can still be retried or released.
    *stateOut = state;
    return __MsixPrepare(access, state);
}

static oserr_t
__MsixProgram(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state,
    _In_ const struct PciInterruptRequest*  request)
{
    struct __PciMsixEntry* entry;
    uint32_t               field;
    oserr_t                status;
    uint32_t               message[3] = {
        request->AddressLow,
        request->AddressHigh,
        request->Data
    };

    // A message can only be programmed after reservation has masked the table.
    // Once delivery may start, reject rewrites.
    if (!state->Prepared || state->MayDeliver) {
        WARNING("__MsixProgram rejected, prepared=%i, delivering=%i",
                state->Prepared, state->MayDeliver);
        return OS_EBUSY;
    }

    if (request->Index >= state->Count) {
        ERROR("__MsixProgram entry %u is outside the reservation of %u",
              request->Index, state->Count);
        return OS_EINVALPARAMS;
    }

    entry = &state->Entries[request->Index];

    // Keep the payload fixed across retries so the device and its owner
    // cannot end up with different message data.
    if (entry->Supplied && memcmp(entry->Message, message, sizeof(message)) != 0) {
        ERROR("__MsixProgram entry %u changed between attempts", request->Index);
        return OS_EINVALPARAMS;
    }

    memcpy(entry->Message, message, sizeof(message));
    entry->Supplied = 1;
    if (entry->Programmed) {
        return OS_EOK;
    }

    TRACE("__MsixProgram(index=%u, address=0x%x:0x%x, data=0x%x)",
          request->Index, request->AddressHigh, request->AddressLow, request->Data);

    // Mask the entry before its payload changes, so a stale address can never
    // be used by an interrupt raised midway through the update.
    status = __MsixTableWrite(access, state, request->Index, 3,
        entry->Original[3] | MSIX_VECTOR_MASK);
    if (status != OS_EOK) {
        ERROR("__MsixProgram failed to mask entry %u: %u", request->Index, status);
        return status;
    }

    for (field = 0; field < 3; field++) {
        status = __MsixTableWrite(access, state, request->Index, field, message[field]);
        if (status != OS_EOK) {
            ERROR("__MsixProgram failed to write entry %u field %u: %u",
                  request->Index, field, status);
            return status;
        }
    }

    entry->Programmed = 1;
    return OS_EOK;
}

static oserr_t
__AllEntriesProgrammed(
    _In_ const struct PciMsixState* state)
{
    for (uint32_t i = 0; i < state->Count; i++) {
        if (!state->Entries[i].Programmed) {
            WARNING("__AllEntriesProgrammed entry %u has no message yet", i);
            return OS_EBUSY;
        }
    }
    return OS_EOK;
}

static oserr_t
__MsixEnable(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    oserr_t status;

    if (!state->Prepared) {
        WARNING("__MsixEnable rejected, the reservation is not prepared");
        return OS_EBUSY;
    }

    status = __AllEntriesProgrammed(state);
    if (status != OS_EOK) {
        return status;
    }

    if (state->Enabled) {
        return OS_EOK;
    }

    TRACE("__MsixEnable(count=%u)", state->Count);

    // Keep the function masked while the reserved entries are unmasked, so
    // delivery starts only once the whole table is ready.
    status = __MsixStop(access, state);
    if (status != OS_EOK) {
        ERROR("__MsixEnable failed to mask the function before unmasking: %u", status);
        return status;
    }

    for (uint32_t i = 0; i < state->Count; i++) {
        status = __MsixTableWrite(access, state, i, 3,
            state->Entries[i].Original[3] & ~MSIX_VECTOR_MASK);
        if (status != OS_EOK) {
            ERROR("__MsixEnable failed to unmask entry %u: %u", i, status);
            return status;
        }
    }

    // The final write may take effect even if readback fails. Block programming
    // until a later Disable confirms that delivery has stopped.
    state->MayDeliver = 1;

    status = __MsixConfigWrite(access, state->Layout.Capability + 2,
        (state->Layout.Control | MSIX_ENABLE) & ~MSIX_FUNCTION_MASK);
    if (status != OS_EOK) {
        ERROR("__MsixEnable failed to unmask the function: %u", status);
        return status;
    }

    state->Enabled = 1;
    return OS_EOK;
}

static oserr_t
__RestoreEntry(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state,
    _In_ uint32_t                           index)
{
    struct __PciMsixEntry* entry = &state->Entries[index];
    oserr_t                status;

    // Mask before restoring the payload, then put the saved mask bit back last
    // so the entry is never live with a half-restored message.
    status = __MsixTableWrite(access, state, index, 3, entry->Original[3] | MSIX_VECTOR_MASK);
    if (status != OS_EOK) {
        ERROR("__RestoreEntry failed to mask entry %u: %u", index, status);
        return status;
    }

    for (uint32_t field = 0; field < 3; field++) {
        status = __MsixTableWrite(access, state, index, field, entry->Original[field]);
        if (status != OS_EOK) {
            ERROR("__RestoreEntry failed to restore entry %u field %u: %u", index, field, status);
            return status;
        }
    }

    status = __MsixTableWrite(access, state, index, 3, entry->Original[3]);
    if (status != OS_EOK) {
        ERROR("__RestoreEntry failed to restore the mask of entry %u: %u", index, status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__RestoreTable(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    oserr_t status;

    if (state->ConfigurationTouched) {
        status = __MsixStop(access, state);
        if (status != OS_EOK) {
            ERROR("__RestoreTable failed to stop delivery before restoring: %u", status);
            return status;
        }
    }

    // Without a snapshot nothing was ever written, so there is nothing to undo.
    if (state->Snapshot) {
        for (uint32_t i = 0; i < state->Layout.Count; i++) {
            status = __RestoreEntry(access, state, i);
            if (status != OS_EOK) {
                return status;
            }
        }
    }

    state->TableRestored = 1;
    return OS_EOK;
}

static oserr_t
__MsixRestoreConfiguration(
    _In_ const struct MsixDeviceOperations* access,
    _In_ struct PciMsixState*               state)
{
    uint32_t command;
    oserr_t  status;

    status = __MsixConfigWrite(access, state->Layout.Capability + 2,
        state->Layout.Control & ~MSIX_ENABLE);
    if (status != OS_EOK) {
        ERROR("__MsixRestoreConfiguration failed to disable MSI-X: %u", status);
        return status;
    }

    // Restore only the bits this reservation changed. Bus control may own the
    // rest of the command register.
    command = access->Config.Read(access->Config.Context, 4, 2);
    command = (command & ~MSIX_COMMAND_BITS) | (state->Command & MSIX_COMMAND_BITS);

    status = __MsixConfigWrite(access, 4, command);
    if (status != OS_EOK) {
        ERROR("__MsixRestoreConfiguration failed to restore the command register: %u", status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__MsixRelease(
    _In_    const struct MsixDeviceOperations* access,
    _InOut_ struct PciMsixState**              stateOut)
{
    struct PciMsixState* state = *stateOut;
    oserr_t              status;

    TRACE("__MsixRelease(capability=0x%x)", state->Layout.Capability);

    // Keep the reservation busy if cleanup fails partway through.
    state->Releasing = 1;

    if (!state->TableRestored) {
        status = __RestoreTable(access, state);
        if (status != OS_EOK) {
            WARNING("__MsixRelease table restoration incomplete, retry required: %u", status);
            return status;
        }
    }

    // Table access is finished at this point, so the mapping can go even if the
    // remaining configuration writes need another attempt.
    if (state->Mapping != NULL) {
        status = access->Unmap(access->Config.Context, state->Mapping, state->Layout.Table.Length);
        if (status != OS_EOK) {
            ERROR("__MsixRelease failed to unmap the message table: %u", status);
            return status;
        }
        state->Mapping = NULL;
    }

    if (state->ConfigurationTouched) {
        status = __MsixRestoreConfiguration(access, state);
        if (status != OS_EOK) {
            WARNING("__MsixRelease configuration restoration incomplete, retry required: %u", status);
            return status;
        }
    }

    // Discard the snapshot only after every restore step succeeds.
    free(state->Entries);
    free(state);
    *stateOut = NULL;
    return OS_EOK;
}

oserr_t
PciMsixControl(
    _In_    const struct MsixDeviceOperations* access,
    _InOut_ struct PciMsixState**              state,
    _In_    const struct PciInterruptRequest*  request)
{
    if (!PciInterruptRequestIsValid(request) || request->Strategy != INTERRUPT_STRATEGY_MSIX) {
        ERROR("PciMsixControl invalid request (operation %u, strategy %u)",
              request->Operation, request->Strategy);
        return OS_EINVALPARAMS;
    }

    TRACE("PciMsixControl(operation=%u, allocation=%u)", request->Operation, request->Allocation);

    if (*state != NULL) {
        if ((*state)->Allocation != request->Allocation) {
            WARNING("PciMsixControl allocation %u does not own this reservation",
                    request->Allocation);
            return OS_EBUSY;
        }

        // Once release begins, only disable or release may continue the cleanup.
        if ((*state)->Releasing && request->Operation != PciInterruptRelease &&
            request->Operation != PciInterruptDisable) {
            WARNING("PciMsixControl operation %u refused while releasing", request->Operation);
            return OS_EBUSY;
        }
    }

    if (request->Operation == PciInterruptReserve) {
        return __MsixReserve(access, state, request);
    }

    if (*state == NULL) {
        // Disable and Release are safe to retry after the reservation is gone.
        if (request->Operation == PciInterruptDisable || request->Operation == PciInterruptRelease) {
            return OS_EOK;
        }
        WARNING("PciMsixControl operation %u requires an active reservation", request->Operation);
        return OS_ENOENT;
    }

    switch (request->Operation) {
        case PciInterruptProgram:
            return __MsixProgram(access, *state, request);
        case PciInterruptEnable:
            return __MsixEnable(access, *state);
        case PciInterruptDisable:
            // Nothing was enabled, or the table has already been handed back.
            if (!(*state)->ConfigurationTouched || (*state)->TableRestored) {
                return OS_EOK;
            }
            return __MsixStop(access, *state);
        case PciInterruptRelease:
            return __MsixRelease(access, state);
        default:
            ERROR("PciMsixControl unhandled operation %u", request->Operation);
            return OS_EINVALPARAMS;
    }
}

void
PciMsixUpdateCommand(
    _InOut_ struct PciMsixState* state,
    _In_    uint16_t             command)
{
    state->Command = (state->Command & ~MSIX_COMMAND_BITS) | (command & MSIX_COMMAND_BITS);
    state->ConfigurationTouched = 1;
}
