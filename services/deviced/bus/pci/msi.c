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
 */

//#define __TRACE

#include <bus/pci/msi.h>
#include <ddk/utils.h>
#include <string.h>

// shared pci register values
#include "registers.h"

// Message Control Register
// Bit 0: MSI Enable (1 = MSI enabled, 0 = legacy INTx interrupts enabled).
// Bits 3:1: Multiple Message Capable (encoded value 0 through 5 indicating a request for \(2^{n}\) vectors).
// Bits 6:4: Multiple Message Enable (programmed by system software to match requested vectors).
// Bit 7: 64-bit Address Capable (1 = supports 64-bit message address, 0 = 32-bit only).
// Bit 8: Per-Vector Masking Capable (1 = device supports masking individual MSI vectors).
// Bits 15:9: Reserved
#define PCI_CAPABILITY_CONTROL_OFFSET         2U
#define MSI_ENABLE                            0x0001U
#define MSI_MME                               0x0070U
#define MSI_64                                0x0080U
#define MSI_MASK                              0x0100U


// Layout for 32-Bit Addressing (No Per-Vector Masking)
// Message Address (Offset +04h–+07h, 4 bytes): Lower 32 bits of the target memory address (aligned to DWORD) where the interrupt write transaction is sent.
// Message Data (Offset +08h–+09h, 2 bytes): The 16-bit data value written to the message address when generating the interrupt.

// Layout for 64-Bit Addressing (No Per-Vector Masking)
// Message Address Lower (Offset +04h–+07h, 4 bytes): Lower 32 bits of the target memory address.
// Message Address Upper (Offset +08h–+0Bh, 4 bytes): Upper 32 bits of the target memory address (present only if 64-bit Address Capable bit is set).
// Message Data (Offset +0Ch–+0Dh, 2 bytes): The 16-bit data value written to the memory address.

// Optional Per-Vector Masking Registers (PCI 3.0)
// If the Per-Vector Masking Capable bit is set, optional mask and pending bits are appended at the end of the structure:
// Mask Bits (4 bytes): A bitmask where each bit corresponds to an MSI vector to block/unmask individual interrupts.
// Pending Bits (4 bytes): Read-only bitmask showing which MSI vectors are currently pending delivery.

#define MSI_ADDRESS_LOW_OFFSET                4U
#define MSI_ADDRESS_HIGH_OFFSET               8U

#define MSI_DATA_OFFSET_32_BIT                8U
#define MSI_DATA_OFFSET_64_BIT                12U

#define MSI_MASK_OFFSET_FROM_DATA             4U
#define MSI_32_BIT_CAPABILITY_LENGTH          10U
#define MSI_64_BIT_CAPABILITY_LENGTH          14U
#define MSI_32_BIT_MASKED_CAPABILITY_LENGTH   20U
#define MSI_64_BIT_MASKED_CAPABILITY_LENGTH   24U

#define MSI_MESSAGE_CAPABLE_SHIFT             1U
#define MSI_MESSAGE_CAPABLE_MASK              0x07U
#define MSI_MESSAGE_CAPABLE_MAX_ENCODING      5U
#define MSI_MESSAGE_MAX_COUNT                 32U
#define MSI_VECTOR_MASK_BIT                   0x01U
#define MSI_MAX_RESERVED_MESSAGES             1U

#define MSIX_CONTROL_ENABLE                   0x8000U
#define MSIX_CONTROL_TABLE_SIZE_MASK           0x07FFU
#define MSIX_CAPABILITY_LENGTH                12U
#define MSIX_MESSAGE_MAX_COUNT                (MSIX_CONTROL_TABLE_SIZE_MASK + 1U)

#define PCI_CAPABILITY_ALIGNMENT              4U
#define PCI_CAPABILITY_LAST_START             (PCI_CONFIG_SPACE_SIZE - PCI_CAPABILITY_ALIGNMENT)


static oserr_t
__WriteAndVerify(
    _In_ const struct PciConfigOperations* access,
    _In_ size_t                            offset,
    _In_ uint32_t                          value,
    _In_ size_t                            width)
{
    uint32_t readback;

    TRACE("__WriteAndVerify(offset=%u, value=0x%x, width=%u)",
          (unsigned)offset, value, (unsigned)width);

    access->Write(access->Context, offset, value, width);

    // Confirm the write by reading the register back.
    readback = access->Read(access->Context, offset, width);
    if (readback != value) {
        ERROR("__WriteAndVerify offset %u did not accept 0x%x, read back 0x%x",
              (unsigned)offset, value, readback);
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

static oserr_t
__VerifyPciDeviceType(
    _In_  const struct PciConfigOperations* access)
{
    uint32_t headerType;
    uint32_t status;

    headerType = access->Read(access->Context, PCI_HEADER_TYPE_OFFSET, 1);
    // MSI is supported on ordinary PCI functions, not PCI-to-PCI bridges.
    if ((headerType & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_NORMAL) {
        TRACE("__VerifyPciDeviceType header type 0x%x is not an ordinary function",
              headerType);
        return OS_ENOTSUPPORTED;
    }

    // Check for a capability list before following its pointer.
    status = access->Read(access->Context, PCI_STATUS_OFFSET, 2);
    if (!(status & PCI_STATUS_CAPABILITIES_LIST)) {
        TRACE("__VerifyPciDeviceType function has no capability list (status 0x%x)",
              status);
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

static oserr_t
__IsValidCapabilityLink(
    _In_ uint32_t nextOffset)
{
    // Verify that the pointer is inside bounds
    if (nextOffset < PCI_CAPABILITY_LIST_MIN_OFFSET || nextOffset > PCI_CAPABILITY_LAST_START) {
        return OS_EINVALPARAMS;
    }
    
    // Verify that it's properly aligned
    if ((nextOffset & PCI_CAPABILITY_LIST_ALIGNMENT_MASK) != 0) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

static size_t
__MsiCapabilityLength(
    _In_ uint32_t control)
{
    size_t length;

    // MSI's data and optional mask registers change the capability
    // length.
    if (control & MSI_MASK) {
        if (control & MSI_64) {
            length = MSI_64_BIT_MASKED_CAPABILITY_LENGTH;
        } else {
            length = MSI_32_BIT_MASKED_CAPABILITY_LENGTH;
        }
    } else {        
        if (control & MSI_64) {
            length = MSI_64_BIT_CAPABILITY_LENGTH;
        } else {
            length = MSI_32_BIT_CAPABILITY_LENGTH;
        }
    }
    return length;
}

oserr_t
PciInterruptProbe(
    _In_  const struct PciConfigOperations* access,
    _Out_ struct PciInterruptCapabilities*  capabilities)
{
    struct PciInterruptCapabilities result = { 0 };

    unsigned char visited[PCI_CONFIG_SPACE_SIZE] = { 0 };
    unsigned char occupied[PCI_CONFIG_SPACE_SIZE] = { 0 };
    uint32_t      next;
    oserr_t       oserr;

    TRACE("PciInterruptProbe()");

    // MSI is supported on ordinary PCI functions, not bridges. Require a
    // capability list before following its pointer.
    oserr = __VerifyPciDeviceType(access);
    if (oserr != OS_EOK) {
        return oserr;
    }

    next = access->Read(access->Context, PCI_CAPABILITY_POINTER_OFFSET, 1);
    while (next != 0) {
        uint32_t id;
        uint32_t control;
        size_t   length;

        // Check the pointer before indexing either tracking array or reading
        // the capability. Repeated pointers and overlap indicate a bad list.
        oserr = __IsValidCapabilityLink(next);
        if (oserr != OS_EOK) {
            ERROR("PciInterruptProbe capability pointer 0x%x is out of bounds or misaligned", next);
            return oserr;
        }

        if (visited[next] || occupied[next]) {
            ERROR("PciInterruptProbe capability list loops or overlaps at 0x%x", next);
            return OS_EINVALPARAMS;
        }

        // mark this offset as visited
        visited[next] = 1;

        // read the id and control values
        id = access->Read(access->Context, next + PCI_CAPABILITY_ID_OFFSET, 1);
        control = access->Read(access->Context, next + PCI_CAPABILITY_CONTROL_OFFSET, 2);
        TRACE("PciInterruptProbe capability id 0x%x at 0x%x, control 0x%x", id, next, control);

        if (id == PCI_CAPABILITY_ID_MSI) {
            uint32_t exponent;

            // If an MSI capability has already been found, report an error.
            if (result.MsiOffset != 0) {
                ERROR("PciInterruptProbe duplicate MSI capability at 0x%x", next);
                return OS_EINVALPARAMS;
            }
            
            // Length varies based on capabilities here of the msi capability.
            // Ensure we get the full length and reserve that.
            length = __MsiCapabilityLength(control);
            if (length > PCI_CONFIG_SPACE_SIZE - next) {
                ERROR("PciInterruptProbe MSI capability at 0x%x extends past configuration space", next);
                return OS_EINVALPARAMS;
            }
            
            // This should *not* happen, however if the MSI is already enabled
            // then we should not attempt to modify it.
            if (control & MSI_ENABLE) {
                WARNING("PciInterruptProbe MSI at 0x%x is already enabled by someone else", next);
                return OS_EBUSY;
            }
            
            // MMC stores log2(count), with a maximum of 32 messages.
            exponent = (control >> MSI_MESSAGE_CAPABLE_SHIFT) & MSI_MESSAGE_CAPABLE_MASK;
            if (exponent > MSI_MESSAGE_CAPABLE_MAX_ENCODING) {
                ERROR("PciInterruptProbe MSI at 0x%x reports an invalid message encoding %u", next, exponent);
                return OS_EINVALPARAMS;
            }
            
            // store the msi capability information
            result.MsiOffset   = (uint16_t)next;
            result.MsiCapacity = 1U << exponent;
        } else if (id == PCI_CAPABILITY_ID_MSIX) {
            length = MSIX_CAPABILITY_LENGTH;

            // MSI-X has a fixed 12-byte capability. 
            // If the offset is invalid or a duplicate, error out.
            if (next > PCI_CONFIG_SPACE_SIZE - MSIX_CAPABILITY_LENGTH) {
                ERROR("PciInterruptProbe MSI-X capability at 0x%x extends past configuration space", next);
                return OS_EINVALPARAMS;
            }

            if (result.MsixOffset != 0) {
                ERROR("PciInterruptProbe duplicate MSI-X capability at 0x%x", next);
                return OS_EINVALPARAMS;
            }
            
            // This should *not* happen, however if the MSI-X is already enabled
            // then we should not attempt to modify it.
            if (control & MSIX_CONTROL_ENABLE) {
                WARNING("PciInterruptProbe MSI-X at 0x%x is already enabled by someone else", next);
                return OS_EBUSY;
            }
            
            // store the msi-x capability information
            result.MsixOffset   = (uint16_t)next;
            result.MsixCapacity = (control & MSIX_CONTROL_TABLE_SIZE_MASK) + 1U;
        } else {
            // Treat this as a standard capability with only the header length.
            length = PCI_CAPABILITY_HEADER_LENGTH;
        }

        for (size_t i = 0; i < length; i++) {
            // Capability ranges may not overlap another capability or a link
            // target already visited earlier in this list.
            if (occupied[next + i] || (i != 0 && visited[next + i])) {
                ERROR("PciInterruptProbe capability at 0x%x overlaps an earlier capability", next);
                return OS_EINVALPARAMS;
            }
            occupied[next + i] = 1;
        }

        // The first byte of each capability points to the next one; zero ends
        // the list.
        next = access->Read(access->Context, next + PCI_CAPABILITY_NEXT_OFFSET, 1);
    }

    // An empty or unrelated capability list does not provide message interrupts.
    if (result.MsiOffset == 0 && result.MsixOffset == 0) {
        TRACE("PciInterruptProbe function provides no message interrupts");
        return OS_ENOTSUPPORTED;
    }

    TRACE("PciInterruptProbe msi=0x%x (%u), msi-x=0x%x (%u)",
          result.MsiOffset, result.MsiCapacity, result.MsixOffset, result.MsixCapacity);
    *capabilities = result;
    return OS_EOK;
}

oserr_t
PciMsiProbe(
    _In_  const struct PciConfigOperations* access,
    _Out_ uint16_t*                         capability)
{
    struct PciInterruptCapabilities capabilities;
    oserr_t                         status;

    // Keep the detailed scan in one place so MSI and MSI-X checks use the same
    // validation rules for the function's capability list.
    status = PciInterruptProbe(access, &capabilities);
    if (status != OS_EOK) {
        TRACE("PciMsiProbe capability scan failed: %u", status);
        return status;
    }

    if (capabilities.MsiOffset == 0) {
        TRACE("PciMsiProbe function has no MSI capability");
        return OS_ENOTSUPPORTED;
    }

    // The caller needs only MSI's location, not the rest of the scan results.
    *capability = capabilities.MsiOffset;
    return OS_EOK;
}

static size_t
__DataOffset(
    _In_ const struct PciMsiState *state)
{
    // The 64-bit layout places message data after the upper address dword.
    return state->Capability + ((state->Control & MSI_64) ? MSI_DATA_OFFSET_64_BIT : MSI_DATA_OFFSET_32_BIT);
}

static oserr_t
__SetMask(
    _In_ const struct PciConfigOperations* access,
    _In_ struct PciMsiState*               state,
    _In_ int                               enable)
{
    unsigned count;
    uint32_t supported;
    uint32_t mask;
    oserr_t  status;

    count     = 1U << ((state->Control >> MSI_MESSAGE_CAPABLE_SHIFT) & MSI_MESSAGE_CAPABLE_MASK);
    supported = count == MSI_MESSAGE_MAX_COUNT ? UINT32_MAX : (1U << count) - 1;

    // Set only the supported mask bits. Keep reserved bits as they were saved
    // so readback does not expect them to accept writes.
    mask = state->Mask | supported;
    if (enable) {
        // This implementation reserves one message, so unmask only its bit.
        mask &= ~MSI_VECTOR_MASK_BIT;
    }

    TRACE("__SetMask(enable=%i) mask=0x%x", enable, mask);
    status = __WriteAndVerify(
        access,
        __DataOffset(state) + MSI_MASK_OFFSET_FROM_DATA,
        mask,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__SetMask failed to write mask 0x%x: %u", mask, status);
    }
    return status;
}

static oserr_t
__Stop(
    _In_ const struct PciConfigOperations* access,
    _In_ struct PciMsiState*               state)
{
    oserr_t status;

    TRACE("__Stop(capability=0x%x)", state->Capability);

    // Disable MSI before changing its mask. This stops delivery during the update.
    status = __WriteAndVerify(
        access,
        state->Capability + PCI_CAPABILITY_CONTROL_OFFSET,
        state->Control & ~(MSI_ENABLE | MSI_MME),
        2
    );
    if (status != OS_EOK) {
        ERROR("__Stop failed to disable the MSI control register: %u", status);
        return status;
    }

    // Update the mask only when the device supports per-vector masking.
    if (!(state->Control & MSI_MASK)) {
        return OS_EOK;
    }

    status = __SetMask(access, state, 0);
    if (status != OS_EOK) {
        ERROR("__Stop failed to mask the reserved message: %u", status);
        return status;
    }
    return OS_EOK;
}

int
PciInterruptRequestIsValid(
    _In_ const struct PciInterruptRequest* request)
{
    uint32_t capacity;

    if (request->Strategy == INTERRUPT_STRATEGY_MSI) {
        capacity = MSI_MAX_RESERVED_MESSAGES;
    } else if (request->Strategy == INTERRUPT_STRATEGY_MSIX) {
        capacity = MSIX_MESSAGE_MAX_COUNT;
    } else {
        return 0;
    }

    // Reject invalid operation, allocation, or reserved fields.
    if (request->Operation > PciInterruptRelease ||
        request->Allocation == UUID_INVALID || request->Reserved != 0) {
        return 0;
    }
    
    if (request->Operation == PciInterruptReserve) {
        // MSI is handled as a single message here, while MSI-X has a bounded
        // table size that fits the request interface.
        if (request->Count == 0 || request->Count > capacity) {
            return 0;
        }
    } else if (request->Count != 0) {
        return 0;
    }
    
    if (request->Operation == PciInterruptProgram) {
        // Message addresses must be dword-aligned before they are written to
        // the device's MSI registers.
        return request->Index < capacity &&
            (request->AddressLow & (PCI_CAPABILITY_ALIGNMENT - 1U)) == 0;
    }
    
    // These fields are used only by Program.
    if (request->Index != 0 || request->Data != 0) {
        return 0;
    }
    return request->AddressLow == 0 && request->AddressHigh == 0;
}

static oserr_t
__MsiReserve(
    _In_    const struct PciConfigOperations* access,
    _InOut_ struct PciMsiState*               state,
    _In_    uuid_t                            allocation)
{
    uint16_t capability;
    uint32_t command;
    oserr_t  status;

    TRACE("__MsiReserve(allocation=%u)", allocation);

    if (state->Prepared) {
        // Keep the current state as is; no need to reinitialize.
        return OS_EOK;
    }

    if (!state->Active) {
        status = PciMsiProbe(access, &capability);
        if (status != OS_EOK) {
            WARNING("__MsiReserve function does not provide usable MSI: %u", status);
            return status;
        }
        
        // Save every field that Release must restore before touching hardware.
        state->Capability = capability;
        state->Control = (uint16_t)access->Read(access->Context, capability + PCI_CAPABILITY_CONTROL_OFFSET, 2);
        state->Command = (uint16_t)access->Read(access->Context, PCI_COMMAND_OFFSET, 2);
        state->Data = (uint16_t)access->Read(access->Context, __DataOffset(state), 2);
        
        // Handle both 32 and 64 bit MSI address registers
        state->AddressLow = access->Read(access->Context, capability + MSI_ADDRESS_LOW_OFFSET, 4);
        if (state->Control & MSI_64) {
            state->AddressHigh = access->Read(access->Context, capability + MSI_ADDRESS_HIGH_OFFSET, 4);
        } else {
            state->AddressHigh = 0;
        }

        // Handle mask register if supported
        if (state->Control & MSI_MASK) {
            state->Mask = access->Read(
                access->Context,
                __DataOffset(state) + MSI_MASK_OFFSET_FROM_DATA,
                sizeof(uint32_t)
            );
        } else {
            state->Mask = 0;
        }
        state->Allocation = allocation;

        // Just set active here so we don't have to regather information
        state->Active = 1;
    }

    status = __Stop(access, state);
    if (status != OS_EOK) {
        ERROR("__MsiReserve failed to quiesce MSI before reservation: %u", status);
        return status;
    }
    
    // Prevent INTx irqs while the MSI message is reserved.
    command = access->Read(access->Context, PCI_COMMAND_OFFSET, 2) | PCI_COMMAND_INTDISABLE;
    status = __WriteAndVerify(access, PCI_COMMAND_OFFSET, command, 2);
    if (status != OS_EOK) {
        ERROR("__MsiReserve failed to disable INTx delivery: %u", status);
        return status;
    }

    state->Prepared = 1;
    return OS_EOK;
}

static oserr_t
__MsiProgram(
    _In_    const struct PciConfigOperations* access,
    _InOut_ struct PciMsiState*               state,
    _In_    const struct PciInterruptRequest* request)
{
    oserr_t status;

    // A message can only be programmed after reservation has captured the old
    // settings and stopped delivery. Once delivery may start, reject rewrites.
    if (!state->Prepared || state->MayDeliver) {
        WARNING("__MsiProgram rejected, prepared=%i, delivering=%i",
                state->Prepared, state->MayDeliver);
        return OS_EBUSY;
    }

    if (request->Data > UINT16_MAX) {
        ERROR("__MsiProgram message data 0x%x exceeds the 16-bit register", request->Data);
        return OS_ENOTSUPPORTED;
    }

    // Only 64-bit capable functions carry the upper address register.
    if (!(state->Control & MSI_64) && request->AddressHigh != 0) {
        ERROR("__MsiProgram 64-bit message address supplied to a 32-bit only function");
        return OS_ENOTSUPPORTED;
    }

    if (state->Supplied) {
        // Keep the payload fixed across retries so the device and its owner
        // cannot end up with different message data.
        if (state->Message.AddressLow != request->AddressLow ||
            state->Message.AddressHigh != request->AddressHigh ||
            state->Message.Data != request->Data) {
            ERROR("__MsiProgram message changed between attempts");
            return OS_EINVALPARAMS;
        }
    } else {
        state->Message.AddressLow = request->AddressLow;
        state->Message.AddressHigh = request->AddressHigh;
        state->Message.Data = request->Data;
        state->Supplied = 1;
    }

    if (state->Programmed) {
        return OS_EOK;
    }

    TRACE("__MsiProgram(address=0x%x:0x%x, data=0x%x)",
          request->AddressHigh, request->AddressLow, request->Data);
    
    // Verify each field before advancing so a retry can safely repeat the writes.
    status = __WriteAndVerify(access, state->Capability + MSI_ADDRESS_LOW_OFFSET, request->AddressLow, 4);
    if (status != OS_EOK) {
        ERROR("__MsiProgram failed to write the lower message address: %u", status);
        return status;
    }

    if (state->Control & MSI_64) {
        status = __WriteAndVerify(access, state->Capability + MSI_ADDRESS_HIGH_OFFSET, request->AddressHigh, 4);
        if (status != OS_EOK) {
            ERROR("__MsiProgram failed to write the upper message address: %u", status);
            return status;
        }
    }

    status = __WriteAndVerify(access, __DataOffset(state), request->Data, 2);
    if (status != OS_EOK) {
        ERROR("__MsiProgram failed to write the message data: %u", status);
        return status;
    }

    state->Programmed = 1;
    return OS_EOK;
}

static oserr_t
__RestoreConfiguration(
    _In_ const struct PciConfigOperations* access,
    _In_ struct PciMsiState*        state)
{
    uint32_t command;
    oserr_t  status;

    TRACE("__RestoreConfiguration(capability=0x%x)", state->Capability);

    // Restore the saved message address, data, and mask.
    status = __WriteAndVerify(access, state->Capability + MSI_ADDRESS_LOW_OFFSET, state->AddressLow, 4);
    if (status != OS_EOK) {
        ERROR("__RestoreConfiguration failed to restore the lower message address: %u", status);
        return status;
    }

    if (state->Control & MSI_64) {
        status = __WriteAndVerify(access, state->Capability + MSI_ADDRESS_HIGH_OFFSET, state->AddressHigh, 4);
        if (status != OS_EOK) {
            ERROR("__RestoreConfiguration failed to restore the upper message address: %u", status);
            return status;
        }
    }

    status = __WriteAndVerify(access, __DataOffset(state), state->Data, 2);
    if (status != OS_EOK) {
        ERROR("__RestoreConfiguration failed to restore the message data: %u", status);
        return status;
    }

    if (state->Control & MSI_MASK) {
        status = __WriteAndVerify(
            access,
            __DataOffset(state) + MSI_MASK_OFFSET_FROM_DATA,
            state->Mask,
            4
        );
        if (status != OS_EOK) {
            ERROR("__RestoreConfiguration failed to restore the mask register: %u", status);
            return status;
        }
    }

    status = __WriteAndVerify(
        access,
        state->Capability + PCI_CAPABILITY_CONTROL_OFFSET,
        state->Control & ~MSI_ENABLE,
        2
    );
    if (status != OS_EOK) {
        ERROR("__RestoreConfiguration failed to restore the control register: %u", status);
        return status;
    }

    // Restore INTx delivery without overwriting unrelated command bits that
    // bus control may have changed during the reservation.
    command = (access->Read(access->Context, PCI_COMMAND_OFFSET, 2) & ~PCI_COMMAND_INTDISABLE) |
        (state->Command & PCI_COMMAND_INTDISABLE);

    status = __WriteAndVerify(access, PCI_COMMAND_OFFSET, command, 2);
    if (status != OS_EOK) {
        ERROR("__RestoreConfiguration failed to restore the command register: %u", status);
        return status;
    }
    return OS_EOK;
}

static oserr_t
__MsiEnable(
    _In_    const struct PciConfigOperations* access,
    _InOut_ struct PciMsiState*               state)
{
    oserr_t status;

    if (!state->Programmed) {
        WARNING("__MsiEnable rejected, no message has been programmed");
        return OS_EBUSY;
    }

    TRACE("__MsiEnable(capability=0x%x)", state->Capability);

    if (state->Control & MSI_MASK) {
        status = __SetMask(access, state, 1);
        if (status != OS_EOK) {
            ERROR("__MsiEnable failed to unmask the reserved message: %u", status);
            return status;
        }
    }

    // A failed readback does not prove the write failed, so delivery may start.
    state->MayDeliver = 1;

    status = __WriteAndVerify(
        access,
        state->Capability + PCI_CAPABILITY_CONTROL_OFFSET,
        (state->Control & ~MSI_MME) | MSI_ENABLE,
        2
    );
    if (status != OS_EOK) {
        ERROR("__MsiEnable failed to enable the MSI control register: %u", status);
        return status;
    }
    return OS_EOK;
}

oserr_t
PciMsiControl(
    _In_    const struct PciConfigOperations* access,
    _InOut_ struct PciMsiState*               state,
    _In_    const struct PciInterruptRequest* request)
{
    oserr_t status;

    // Validate direct callers too, even if the provider already checked.
    if (!PciInterruptRequestIsValid(request) || request->Strategy != INTERRUPT_STRATEGY_MSI) {
        ERROR("PciMsiControl invalid request (operation %u, strategy %u)",
              request->Operation, request->Strategy);
        return OS_EINVALPARAMS;
    }

    TRACE("PciMsiControl(operation=%u, allocation=%u)", request->Operation, request->Allocation);

    if (state->Active && state->Allocation != request->Allocation) {
        WARNING("PciMsiControl allocation %u does not own this reservation", request->Allocation);
        return OS_EBUSY;
    }

    // Once release begins, only disable or release may continue the cleanup.
    if (state->Releasing && request->Operation != PciInterruptRelease &&
        request->Operation != PciInterruptDisable) {
        WARNING("PciMsiControl operation %u refused while releasing", request->Operation);
        return OS_EBUSY;
    }
    
    if (request->Operation == PciInterruptReserve) {
        return __MsiReserve(access, state, request->Allocation);
    }
    
    if (!state->Active) {
        // Disable and Release are safe to retry after the reservation is gone.
        if (request->Operation == PciInterruptDisable || request->Operation == PciInterruptRelease) {
            return OS_EOK;
        }
        WARNING("PciMsiControl operation %u requires an active reservation", request->Operation);
        return OS_ENOENT;
    }
    
    if (request->Operation == PciInterruptProgram) {
        return __MsiProgram(access, state, request);
    }
    
    if (request->Operation == PciInterruptEnable) {
        return __MsiEnable(access, state);
    }
    
    if (request->Operation == PciInterruptRelease) {
        // Keep the reservation busy if cleanup fails partway through.
        state->Releasing = 1;
    }

    status = __Stop(access, state);
    if (status != OS_EOK) {
        ERROR("PciMsiControl failed to stop message delivery: %u", status);
        return status;
    }
    
    // Delivery is confirmed off, so it is safe to restore the old payload.
    state->MayDeliver = 0;
    if (request->Operation == PciInterruptDisable) {
        return OS_EOK;
    }

    status = __RestoreConfiguration(access, state);
    if (status != OS_EOK) {
        // Keep the saved state so a later Release can retry.
        WARNING("PciMsiControl release left the reservation partially restored: %u", status);
        return status;
    }

    // Discard the saved state only after every restore write succeeds.
    memset(state, 0, sizeof(*state));
    return OS_EOK;
}
