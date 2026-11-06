/**
 * MollenOS
 *
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

#include <os/shm.h>
#include <stdlib.h>
#include <string.h>

#include "transfer.h"

/**
 * @brief Tracks whether a transfer is waiting for slots or has reserved them.
 */
enum __UsbBounceReservationState {
    __USB_BOUNCE_RESERVATION_UNRESERVED,
    __USB_BOUNCE_RESERVATION_WAITING,
    __USB_BOUNCE_RESERVATION_RESERVED
};

/**
 * @brief Tracks packet copies and pending CPU access cleanup.
 * If SHMAccessEndCpu fails, retry that call without copying the data again.
 */
enum __UsbBounceCopyState {
    __USB_BOUNCE_COPY_UNPREPARED,
    __USB_BOUNCE_COPY_OUT_END_PENDING,
    __USB_BOUNCE_COPY_PREPARED,
    __USB_BOUNCE_COPY_COMPLETION_PENDING,
    __USB_BOUNCE_COPY_IN_END_PENDING,
    __USB_BOUNCE_COPY_FINISHED
};

/**
 * @brief Shared DMA buffer, free packet slots and transfers waiting for slots.
 * Waiting transfers are served in queue order.
 */
typedef struct UsbTransferBouncePool {
    struct DMAContext* Context;
    struct DMABuffer*  Buffer;
    uint8_t*           Data;
    uint64_t           Address;
    spinlock_t         Lock;
    list_t             Waiters;
    uint32_t           Capacity;
    uint32_t           Available;
    uint32_t           TransferCount;
    uint32_t           FreeSlots[];
} UsbTransferBouncePool_t;

/**
 * @brief One packet's position in the transfer and its assigned bounce slot.
 * The transfer offset includes SETUP bytes.
 */
struct __UsbBouncePacket {
    size_t   TransferOffset;
    uint32_t ElementIndex;
    uint32_t Length;
    uint32_t SlotIndex;
    bool     IsInput;
};

/**
 * @brief Packet slots, client buffer access and copy state for one transfer.
 * The copy state records where to continue after a failed CPU access call.
 */
typedef struct UsbTransferPacketBounce {
    UsbTransferBouncePool_t*         Pool;
    struct DMABuffer*                Overflow;
    OSHandle_t                       Client;
    struct SHMAccessSession          Access;
    element_t                        Waiter;
    uint32_t                         Count;
    size_t                           CompletionBytes;
    enum __UsbBounceReservationState ReservationState;
    enum __UsbBounceCopyState        CopyState;
    struct __UsbBouncePacket         Packets[];
} UsbTransferPacketBounce_t;

oserr_t
UsbTransferBouncePoolCreate(
    _In_    struct DMAContext*        context,
    _In_    uint32_t                  slots,
    _InOut_ UsbTransferBouncePool_t** poolOut)
{
    UsbTransferBouncePool_t* pool;
    const struct DMASegment* segments;
    uint32_t                 count;
    size_t                   maxSlots;
    size_t                   maxPayloadSlots;
    oserr_t                  status;

    struct DMARequirements requirements = {
        .Alignment = 4096,
        // Use contiguous memory for the bounce buffer.
        .Contiguous = true
    };

    if (*poolOut != NULL) {
        return OS_EBUSY;
    }

    maxSlots = (SIZE_MAX - sizeof(UsbTransferBouncePool_t)) / sizeof(uint32_t);
    maxPayloadSlots = SIZE_MAX / USB_BOUNCE_SLOT_SIZE;

    if (slots == 0 || slots > maxSlots || slots > maxPayloadSlots) {
        return OS_EINVALPARAMS;
    }

    pool = calloc(1, sizeof(UsbTransferBouncePool_t) + (slots * sizeof(uint32_t)));
    if (pool == NULL) {
        return OS_EOOM;
    }

    // Set the output before allocating DMA memory so the caller can clean up on failure.
    *poolOut = pool;

    pool->Context = context;
    pool->Capacity = slots;
    pool->Available = slots;
    pool->Waiters = (list_t)LIST_INIT;
    spinlock_init(&pool->Lock);
    
    for (uint32_t i = 0; i < slots; i++) {
        pool->FreeSlots[i] = i;
    }

    requirements.Length = (size_t)slots * USB_BOUNCE_SLOT_SIZE;
    status = DMAAllocate(context, &requirements, &pool->Buffer);
    if (status != OS_EOK) {
        return status;
    }

    segments = DMABufferSegments(pool->Buffer, &count);
    pool->Data = DMABufferData(pool->Buffer);
    pool->Address = segments[0].Address;
    return OS_EOK;
}

oserr_t
UsbTransferBouncePoolDestroy(
    _InOut_ UsbTransferBouncePool_t** poolOut)
{
    UsbTransferBouncePool_t* pool = *poolOut;
    bool                     busy;
    oserr_t                  status;

    if (pool == NULL) {
        return OS_EOK;
    }
    
    // Requires controller teardown to have blocked new transfers.
    // Waiting transfers and transfers using overflow memory still use this pool,
    // so checking the number of reserved slots is not enough.
    spinlock_acquire(&pool->Lock);
    busy = pool->TransferCount != 0;
    spinlock_release(&pool->Lock);
    if (busy) {
        return OS_EBUSY;
    }

    status = DMAFree(&pool->Buffer);
    if (status != OS_EOK) {
        return status;
    }

    free(pool);
    *poolOut = NULL;
    return OS_EOK;
}

oserr_t
UsbTransferBounceInitialize(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce;
    struct __UsbBouncePacket*  packet;
    struct TransferElement*    element;
    uint32_t                   count = 0;
    uint32_t                   index = 0;
    size_t                     offset = 0;
    unsigned int               access = SHM_ACCESS_READ;
    size_t                     maxPackets;
    size_t                     maxPayloadPackets;
    oserr_t                    status;

    if (transfer->Dma.Packets != NULL) {
        return OS_EOK;
    }

    maxPackets = (SIZE_MAX - sizeof(UsbTransferPacketBounce_t)) / sizeof(struct __UsbBouncePacket);
    maxPayloadPackets = SIZE_MAX / USB_BOUNCE_SLOT_SIZE;

    for (int i = 0; i < transfer->ElementCount; i++) {
        element = &transfer->Elements[i];
        
        if (!element->NeedsBounce) {
            continue;
        }
        
        if (element->Length == 0 || element->Length > USB_BOUNCE_SLOT_SIZE) {
            return OS_EINVALPARAMS;
        }

        count++;
        if (element->Type == TRANSFERELEMENT_TYPE_IN) {
            access |= SHM_ACCESS_WRITE;
        }
    }

    if (count == 0) {
        return OS_EOK;
    }

    if (transfer->Dma.PacketPool == NULL || !__Transfer_IsAsync(transfer)) {
        return OS_ENOTSUPPORTED;
    }

    if (count > maxPackets || count > maxPayloadPackets) {
        return OS_EOOM;
    }
    
    bounce = calloc(1, 
        sizeof(UsbTransferPacketBounce_t) 
            + (count * sizeof(struct __UsbBouncePacket)));
    if (bounce == NULL) {
        return OS_EOOM;
    }

    transfer->Dma.Packets = bounce;
    bounce->Pool = transfer->Dma.PacketPool;
    bounce->Count = count;
    ELEMENT_INIT(&bounce->Waiter, 0, bounce);
    
    spinlock_acquire(&bounce->Pool->Lock);
    bounce->Pool->TransferCount++;
    spinlock_release(&bounce->Pool->Lock);

    // Include SETUP bytes in packet offsets. The status stage has no data.
    // Completion counts bytes from the same start, including when the transfer
    // is split into several TD batches.
    for (int i = 0; i < transfer->ElementCount; i++) {
        element = &transfer->Elements[i];
        
        if (offset > transfer->Dma.Length || element->Length > transfer->Dma.Length - offset) {
            return OS_EINVALPARAMS;
        }
        
        if (element->NeedsBounce) {
            packet = &bounce->Packets[index++];
            packet->TransferOffset = offset;
            packet->ElementIndex = i;
            packet->Length = element->Length;
            packet->IsInput = element->Type == TRANSFERELEMENT_TYPE_IN;
        }
        offset += element->Length;
    }

    // Attach separately so mapping the packet data does not change the SETUP
    // mapping. Map from offset zero and add BufferOffset when copying packets.
    status = SHMAttach(transfer->SHMHandle.ID, &bounce->Client);
    if (status != OS_EOK) {
        return status;
    }
    
    status = SHMMap(
        &bounce->Client,
        0,
        transfer->Dma.BufferOffset + transfer->Dma.Length,
        access
    );
    if (status != OS_EOK) {
        return status;
    }

    return SHMAccessOpen(
        &bounce->Client,
        transfer->Dma.BufferOffset,
        transfer->Dma.Length,
        &bounce->Access
    );
}

static uint8_t*
__BounceData(
    _In_ UsbTransferPacketBounce_t* bounce,
    _In_ struct __UsbBouncePacket*        packet)
{
    uint8_t* base = bounce->Overflow != NULL ?
        DMABufferData(bounce->Overflow) : bounce->Pool->Data;

    return base + (size_t)packet->SlotIndex * USB_BOUNCE_SLOT_SIZE;
}

static void
__ReleaseSlots(
    _InOut_ UsbTransferPacketBounce_t* bounce)
{
    UsbTransferBouncePool_t* pool = bounce->Pool;

    spinlock_acquire(&pool->Lock);
    
    // Free overflow memory when the transfer is destroyed. It uses no pool
    // slots, so transfers waiting for slots can continue while we keep it.
    if (bounce->ReservationState == __USB_BOUNCE_RESERVATION_WAITING) {
        list_remove(&pool->Waiters, &bounce->Waiter);
        bounce->ReservationState = __USB_BOUNCE_RESERVATION_UNRESERVED;
    }

    if (bounce->ReservationState == __USB_BOUNCE_RESERVATION_RESERVED &&
        bounce->Overflow == NULL) {
        for (uint32_t i = 0; i < bounce->Count; i++) {
            pool->FreeSlots[pool->Available++] = bounce->Packets[i].SlotIndex;
        }
    }

    bounce->ReservationState = __USB_BOUNCE_RESERVATION_UNRESERVED;
    if (bounce->CopyState != __USB_BOUNCE_COPY_FINISHED) {
        bounce->CopyState = __USB_BOUNCE_COPY_UNPREPARED;
    }

    spinlock_release(&pool->Lock);
}

oserr_t
UsbTransferBounceReserve(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    UsbTransferBouncePool_t*   pool;
    const struct DMASegment*   segments;
    uint64_t                   address;
    uint32_t                   count;
    uint32_t                   i;
    oserr_t                    status;

    struct DMARequirements requirements = {
        .Alignment = 4096,
        .Contiguous = true
    };

    if (bounce == NULL ||
        bounce->ReservationState == __USB_BOUNCE_RESERVATION_RESERVED) {
        return OS_EOK;
    }

    pool = bounce->Pool;
    if (bounce->Count > pool->Capacity) {
        // If the request needs more slots than the pool has, allocate a separate
        // buffer before allocating descriptors. Reuse this buffer on retry.
        if (bounce->Overflow == NULL) {
            requirements.Length = (size_t)bounce->Count * USB_BOUNCE_SLOT_SIZE;
            
            status = DMAAllocate(pool->Context, &requirements, &bounce->Overflow);
            if (status != OS_EOK) {
                return status;
            }
        }
        
        segments = DMABufferSegments(bounce->Overflow, &count);
        address = segments[0].Address;
        
        for (i = 0; i < bounce->Count; i++) {
            bounce->Packets[i].SlotIndex = i;
        }
        bounce->ReservationState = __USB_BOUNCE_RESERVATION_RESERVED;
    } else {
        // Reserve all slots for a transfer at once. Queue new requests at the
        // tail even if enough slots are free for them, otherwise smaller requests
        // could keep a larger request waiting indefinitely.
        spinlock_acquire(&pool->Lock);
        
        if (bounce->ReservationState != __USB_BOUNCE_RESERVATION_WAITING) {
            list_append(&pool->Waiters, &bounce->Waiter);
            bounce->ReservationState = __USB_BOUNCE_RESERVATION_WAITING;
        }
        
        if (pool->Waiters.head != &bounce->Waiter || pool->Available < bounce->Count) {
            spinlock_release(&pool->Lock);
            return OS_EBUSY;
        }
        list_remove(&pool->Waiters, &bounce->Waiter);
        
        bounce->ReservationState = __USB_BOUNCE_RESERVATION_UNRESERVED;
        for (i = 0; i < bounce->Count; i++) {
            bounce->Packets[i].SlotIndex = pool->FreeSlots[--pool->Available];
        }
        bounce->ReservationState = __USB_BOUNCE_RESERVATION_RESERVED;
        
        spinlock_release(&pool->Lock);
        address = pool->Address;
    }

    // Update addresses for packets that need bounce slots. Other packets use
    // the original DMA mapping, including those that cross page boundaries.
    for (i = 0; i < bounce->Count; i++) {
        transfer->Elements[bounce->Packets[i].ElementIndex].Data.Address =
            (uintptr_t)(address +
                (uint64_t)bounce->Packets[i].SlotIndex * USB_BOUNCE_SLOT_SIZE);
    }
    return OS_EOK;
}

bool
UsbTransferBounceCanRetry(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    bool                       ready;

    if (bounce == NULL) {
        return true;
    }
    
    spinlock_acquire(&bounce->Pool->Lock);
    ready = bounce->ReservationState != __USB_BOUNCE_RESERVATION_WAITING ||
        (bounce->Pool->Waiters.head == &bounce->Waiter &&
            bounce->Pool->Available >= bounce->Count);
    spinlock_release(&bounce->Pool->Lock);
    return ready;
}

static oserr_t
__EndCpuCopy(
    _InOut_ UsbTransferPacketBounce_t* bounce)
{
    oserr_t status;

    if (bounce->CopyState != __USB_BOUNCE_COPY_OUT_END_PENDING &&
        bounce->CopyState != __USB_BOUNCE_COPY_IN_END_PENDING) {
        return OS_EOK;
    }

    status = SHMAccessEndCpu(&bounce->Access);
    if (status != OS_EOK) {
        return status;
    }

    if (bounce->CopyState == __USB_BOUNCE_COPY_OUT_END_PENDING) {
        bounce->CopyState = __USB_BOUNCE_COPY_PREPARED;
    } else {
        bounce->CopyState = __USB_BOUNCE_COPY_COMPLETION_PENDING;
    }
    return OS_EOK;
}

oserr_t
UsbTransferBouncePrepare(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    struct __UsbBouncePacket*  packet;
    const uint8_t*             source;
    oserr_t                    status;

    if (bounce == NULL || bounce->CopyState == __USB_BOUNCE_COPY_PREPARED) {
        return OS_EOK;
    }

    if (bounce->ReservationState != __USB_BOUNCE_RESERVATION_RESERVED) {
        return OS_EBUSY;
    }

    if (bounce->CopyState == __USB_BOUNCE_COPY_UNPREPARED) {
        status = SHMAccessBeginCpu(
            &bounce->Access,
            SHMCpuAccessRead,
            SHMWaitNonBlocking,
            NULL
        );
        if (status != OS_EOK) {
            return status;
        }

        source = (const uint8_t*)SHMBuffer(&bounce->Client) + transfer->Dma.BufferOffset;
        for (uint32_t i = 0; i < bounce->Count; i++) {
            packet = &bounce->Packets[i];
            if (!packet->IsInput) {
                memcpy(
                    __BounceData(bounce, packet),
                    source + packet->TransferOffset,
                    packet->Length
                );
            }
        }

        bounce->CopyState = __USB_BOUNCE_COPY_OUT_END_PENDING;
    } else if (bounce->CopyState != __USB_BOUNCE_COPY_OUT_END_PENDING) {
        return OS_EBUSY;
    }

    status = __EndCpuCopy(bounce);
    return status;
}

oserr_t
UsbTransferBounceComplete(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                     bytes)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    struct __UsbBouncePacket*  packet;
    uint8_t*                   destination;
    size_t                     length;
    bool                       input = false;
    oserr_t                    status;

    if (bounce == NULL || bounce->CopyState == __USB_BOUNCE_COPY_FINISHED) {
        return OS_EOK;
    }

    if (bounce->CopyState != __USB_BOUNCE_COPY_COMPLETION_PENDING &&
        bounce->CopyState != __USB_BOUNCE_COPY_IN_END_PENDING) {
        bounce->CompletionBytes = bytes;
        bounce->CopyState = __USB_BOUNCE_COPY_COMPLETION_PENDING;
    }

    if (bounce->CopyState == __USB_BOUNCE_COPY_COMPLETION_PENDING) {
        for (uint32_t i = 0; i < bounce->Count; i++) {
            if (bounce->Packets[i].IsInput &&
                bounce->Packets[i].TransferOffset < bounce->CompletionBytes) {
                input = true;
                break;
            }
        }

        if (input) {
            status = SHMAccessBeginCpu(
                &bounce->Access,
                SHMCpuAccessWrite,
                SHMWaitNonBlocking,
                NULL
            );
            if (status != OS_EOK) {
                return status;
            }
            
            destination = (uint8_t*)SHMBuffer(&bounce->Client) + transfer->Dma.BufferOffset;
            for (uint32_t i = 0; i < bounce->Count; i++) {
                packet = &bounce->Packets[i];
                if (!packet->IsInput ||
                    packet->TransferOffset >= bounce->CompletionBytes) {
                    continue;
                }

                // If a short IN ends inside this packet, copy only the received
                // bytes. Leave the rest of the client buffer unchanged.
                length = bounce->CompletionBytes - packet->TransferOffset;
                if (length > packet->Length) {
                    length = packet->Length;
                }

                memcpy(
                    destination + packet->TransferOffset,
                    __BounceData(bounce, packet),
                    length
                );
            }
            bounce->CopyState = __USB_BOUNCE_COPY_IN_END_PENDING;
        }
    }

    status = __EndCpuCopy(bounce);
    if (status != OS_EOK) {
        return status;
    }
    
    __ReleaseSlots(bounce);
    bounce->CopyState = __USB_BOUNCE_COPY_FINISHED;
    return OS_EOK;
}

oserr_t
UsbTransferBounceAbort(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    oserr_t                    status;

    if (bounce == NULL) {
        return OS_EOK;
    }

    // Requires DMA to be inactive. Return the slots so a failed submission
    // waiting for CPU access does not block other transfers.
    status = __EndCpuCopy(bounce);
    if (status != OS_EOK) {
        return status;
    }

    __ReleaseSlots(bounce);
    return OS_EOK;
}

oserr_t
UsbTransferBounceDestroy(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    UsbTransferPacketBounce_t* bounce = transfer->Dma.Packets;
    oserr_t                    status;

    if (bounce == NULL) {
        return OS_EOK;
    }

    status = UsbTransferBounceAbort(transfer);
    if (status != OS_EOK) {
        return status;
    }

    // Close the access session before releasing the client attachment.
    // Successful calls clear their fields, so cleanup can retry after a failure.
    status = SHMAccessClose(&bounce->Access);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRelease(&bounce->Client);
    if (status != OS_EOK) {
        return status;
    }

    status = DMAFree(&bounce->Overflow);
    if (status != OS_EOK) {
        return status;
    }

    spinlock_acquire(&bounce->Pool->Lock);
    bounce->Pool->TransferCount--;
    spinlock_release(&bounce->Pool->Lock);

    free(bounce);
    transfer->Dma.Packets = NULL;
    return OS_EOK;
}
