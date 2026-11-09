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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE
#define __need_minmax
#include <assert.h>
#include "dispatch.h"
#include <internal/_storage.h>
#include <os/handle.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>
#include <string.h>

#include "ctt_driver_service_server.h"
#include "ctt_storage_service_server.h"

static struct __AhciCommandTableEntry {
    int          Direction;
    int          DMA;
    int          AddressingMode;
    AtaCommand_t Command;
    size_t       SectorAlignment;
    size_t       MaxSectors;
} CommandTable[] = {
    { __STORAGE_OPERATION_READ, 0, 2, AtaPIOReadExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_READ, 0, 1, AtaPIORead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 0, 0, AtaPIORead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 1, 2, AtaDMAReadExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_READ, 1, 1, AtaDMARead, 1, 0xFF },
    { __STORAGE_OPERATION_READ, 1, 0, AtaDMARead, 1, 0xFF },
    
    { __STORAGE_OPERATION_WRITE, 0, 2, AtaPIOWriteExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 0, 1, AtaPIOWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 0, 0, AtaPIOWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 1, 2, AtaDMAWriteExt, 1, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 1, 1, AtaDMAWrite, 1, 0xFF },
    { __STORAGE_OPERATION_WRITE, 1, 0, AtaDMAWrite, 1, 0xFF },
    { -1, -1, -1, 0, 0, 0 }
};

static void
__QueueCleanup(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    if (!transaction->CleanupQueued) {
        list_append(&port->CleanupTransactions, &transaction->CleanupHeader);
        transaction->CleanupQueued = true;
    }
    AhciControllerScheduleCleanup(controller);
}

static oserr_t
__DestroyTransaction(
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    oserr_t status;
    int     slot = __GetTransferKey(transaction);

    // DMA must have ended before this call. Keep the request in the port's
    // list until unmap succeeds, so teardown can find it on failure.
    status = DMAUnmap(&transaction->Dma.Mapping);
    if (status != OS_EOK) {
        return status;
    }

    if (transaction->CleanupQueued) {
        list_remove(&port->CleanupTransactions, &transaction->CleanupHeader);
    }
    list_remove(&port->Transactions, &transaction->Header);
    
    if (slot != -1) {
        AhciPortFreeCommandSlot(port, slot);
    }
    free(transaction);
    return OS_EOK;
}

static oserr_t
__FinishTransaction(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    oserr_t status;

    // The command must have completed or the port must be stopped before
    // ending DMA. If DMAEndTransfer fails, leave DeviceOwned set for retry.
    if (transaction->Dma.DeviceOwned) {
        status = DMAEndTransfer(transaction->Dma.Mapping, transaction->Dma.Completed);
        if (status != OS_EOK) {
            goto retry;
        }
        transaction->Dma.DeviceOwned = false;
    }

    transaction->State = TransactionCleanup;
    if (!transaction->Notified) {
        TRACE("Finishing transaction %p", transaction);
        transaction->Notified = true;
        
        // Indicate that the transaction is being cleaned up
        controller->Cleaning++;
        
        if (transaction->Internal) {
            if (transaction->Result == OS_EOK && !controller->Stopping) {
                AhciManagerHandleControlResponse(port, transaction);
            }
        } else {
            ctt_storage_transfer_response(&transaction->DeferredMessage[0],
                transaction->Result, transaction->SectorsTransferred);
        }
        controller->Cleaning--;
    }
    
    status = __DestroyTransaction(port, transaction);
    if (status == OS_EOK) {
        return OS_EOK;
    }

retry:
    WARNING("AHCI port %i: request cleanup failed (%u), will retry", port->Index, status);
    __QueueCleanup(controller, port, transaction);
    return status;
}

static void
__DiscardTransaction(
    _In_ AhciDevice_t*      device,
    _In_ AhciTransaction_t* transaction,
    _In_ oserr_t            status)
{
    transaction->Notified = true;
    transaction->Result = status;
    transaction->State = TransactionCompleting;
    (void)__FinishTransaction(device->Controller, device->Port, transaction);
}

static AhciTransaction_t*
__CreateTransaction(
    _In_ AhciDevice_t*          device,
    _In_ struct gracht_message* message,
    _In_ int                   direction,
    _In_ uint64_t              sector)
{
    AhciTransaction_t* transaction;
    size_t             size = sizeof(AhciTransaction_t);

    if (message != NULL) {
        size += GRACHT_MESSAGE_DEFERRABLE_SIZE(message);
    }

    transaction = malloc(size);
    if (transaction == NULL) {
        return NULL;
    }
    
    memset(transaction, 0, sizeof(AhciTransaction_t));
    ELEMENT_INIT(&transaction->Header, (void*)(uintptr_t)-1, transaction);
    ELEMENT_INIT(&transaction->CleanupHeader, 0, transaction);
    transaction->Type = TransactionRegisterFISH2D;
    transaction->Sector = sector;
    transaction->Target.Type = device->Type;
    transaction->Target.SectorSize = device->SectorSize;
    transaction->Target.AddressingMode = device->AddressingMode;
    transaction->Direction = direction == __STORAGE_OPERATION_READ ? AHCI_XACTION_IN : AHCI_XACTION_OUT;
    
    if (message != NULL) {
        gracht_server_defer_message(message, &transaction->DeferredMessage[0]);
    }
    
    // Append the transaction to the port's transaction list before any further setup.
    list_append(&device->Port->Transactions, &transaction->Header);
    return transaction;
}

static oserr_t
__QueueTransaction(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    if (controller->Stopping) {
        return OS_EBUSY;
    }

    if (__GetTransferKey(transaction) == -1) {
        int     slot;
        oserr_t status = AhciPortAllocateCommandSlot(port, &slot);
        if (status != OS_EOK) {
            return status;
        }
        __SetTransferKey(transaction, slot);
    }
    
    return AhciDispatchRegisterFIS(controller, port, transaction);
}

oserr_t
AhciTransactionControlCreate(
    _In_ AhciDevice_t* device,
    _In_ AtaCommand_t command,
    _In_ size_t       length,
    _In_ int          direction)
{
    AhciTransaction_t* transaction;
    oserr_t            status;

    if (device->Controller->Stopping) {
        return OS_EBUSY;
    }

    if (length == 0 || length > DMABufferSize(device->Port->Dma.InternalBuffer)) {
        return OS_EINVALPARAMS;
    }
    
    foreach(node, &device->Port->Transactions) {
        transaction = node->value;
        if (transaction->Internal) {
            return OS_EBUSY;
        }
    }
    
    transaction = __CreateTransaction(device, NULL, direction, 0);
    if (transaction == NULL) {
        return OS_EOOM;
    }
    
    // Identification uses the port's persistent DMA buffer. It needs no SHM
    // attachment, streaming map or cache handoff on any architecture.
    transaction->Internal = 1;
    transaction->Command = command;
    transaction->BytesLeft = length;
    transaction->Dma.Length = length;
    transaction->Dma.Segments = DMABufferSegments(device->Port->Dma.InternalBuffer,
        &transaction->Dma.SegmentCount);
    
    status = __QueueTransaction(device->Controller, device->Port, transaction);
    if (status != OS_EOK) {
        __DiscardTransaction(device, transaction, status);
    }
    return status;
}

static struct __AhciCommandTableEntry*
__GetCommand(
    _In_ AhciDevice_t* device,
    _In_ int           direction)
{
    int i = 0;

    // Select the appropriate command
    while (CommandTable[i].Direction != -1) {
        if (CommandTable[i].Direction      == direction &&
            CommandTable[i].DMA            == device->HasDMAEngine &&
            CommandTable[i].AddressingMode == device->AddressingMode) {
            return &CommandTable[i];
        }
        i++;
    }
    return NULL;
}

oserr_t
AhciTransactionStorageCreate(
    _In_ AhciDevice_t*          device,
    _In_ struct gracht_message* message,
    _In_ int                   direction,
    _In_ uint64_t              sector,
    _In_ uuid_t                bufferHandle,
    _In_ size_t                bufferOffset,
    _In_ size_t                sectorCount)
{
    struct __AhciCommandTableEntry* command;
    AhciTransaction_t*              transaction;
    OSHandle_t                      buffer = { .ID = bufferHandle, .Type = OSHANDLE_SHM };
    struct DMAMapRequirements       requirements = {
        .AddressAlignment = 2,
        .LengthAlignment = 2
    };
    size_t                          length;
    oserr_t                         status;

    if (device == NULL || device->SectorSize == 0 || sector >= device->SectorCount) {
        return OS_EINVALPARAMS;
    }
    if (device->Controller->Stopping) {
        return OS_EBUSY;
    }
    command = __GetCommand(device, direction);
    if (command == NULL || sectorCount == 0) {
        return OS_EINVALPARAMS;
    }

    sectorCount = MIN(sectorCount, device->SectorCount - sector);
    sectorCount = MIN(sectorCount, command->MaxSectors);
    if (sectorCount > SIZE_MAX / device->SectorSize) {
        return OS_EINVALPARAMS;
    }
    
    length = sectorCount * device->SectorSize;
    transaction = __CreateTransaction(device, message, direction, sector);
    if (transaction == NULL) {
        return OS_EOOM;
    }
    
    transaction->Command = command->Command;
    transaction->SectorAlignment = command->SectorAlignment;
    transaction->BytesLeft = length;
    transaction->Dma.Length = length;

    // Map the SHM ID supplied over IPC. DMA keeps the kernel reference for
    // this mapping, so AHCI needs no separate SHM attachment or physical lookup.
    status = DMAMapSHM(
        device->Controller->DmaContext,
        &buffer,
        bufferOffset,
        length,
        direction == __STORAGE_OPERATION_READ ? DMAFromDevice : DMAToDevice,
        &requirements,
        &transaction->Dma.Mapping
    );
    if (status != OS_EOK) {
        __DiscardTransaction(device, transaction, status);
        return status;
    }

    transaction->Dma.Segments = DMAMappingSegments(
        transaction->Dma.Mapping,
        &transaction->Dma.SegmentCount
    );
    
    status = __QueueTransaction(device->Controller, device->Port, transaction);
    if (status != OS_EOK) {
        __DiscardTransaction(device, transaction, status);
    }
    return status;
}

void
ctt_storage_transfer_invocation(
    _In_ struct gracht_message*         message,
    _In_ uuid_t                         deviceId,
    _In_ enum sys_transfer_direction    direction,
    _In_ unsigned int                   sectorLow,
    _In_ unsigned int                   sectorHigh,
    _In_ const struct sys_shm_transfer* buffer,
    _In_ size_t                         sectorCount)
{
    OSHandle_t    attachment = { 0 };
    size_t        offset;
    AhciDevice_t* device = AhciManagerGetDevice(deviceId);
    oserr_t       status;
    UInteger64_t  sector;
    
    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;
    
    if (direction != SYS_TRANSFER_DIRECTION_READ &&
        direction != SYS_TRANSFER_DIRECTION_WRITE) {
        ctt_storage_transfer_response(message, OS_EINVALPARAMS, 0);
        return;
    }

    if (device == NULL) {
        ctt_storage_transfer_response(message, OS_ENOENT, 0);
        return;
    }

    status = __StorageAttachTransfer(
        buffer,
        device->SectorSize,
        sectorCount,
        direction == SYS_TRANSFER_DIRECTION_WRITE,
        &attachment,
        &offset
    );
    if (status != OS_EOK) {
        ctt_storage_transfer_response(message, status, 0);
        return;
    }

    // Protocol directions (0/1) differ from internal storage operations (1/2).
    status = AhciTransactionStorageCreate(
        device,
        message,
        direction == SYS_TRANSFER_DIRECTION_READ ?
            __STORAGE_OPERATION_READ : __STORAGE_OPERATION_WRITE,
        sector.QuadPart,
        attachment.ID,
        offset,
        sectorCount
    );
    OSHandleDestroy(&attachment);
    if (status != OS_EOK) {
        ctt_storage_transfer_response(message, status, 0);
    }
}

static oserr_t
__VerifyRegisterFISD2H(
    _In_ AhciTransaction_t* transaction)
{
    FISRegisterD2H_t* result = (FISRegisterD2H_t*)&transaction->Response.RegisterD2H;
    TRACE("__VerifyRegisterFISD2H(transaction=0x%" PRIxIN ")", transaction);

    // Is the error bit set?
    TRACE("__VerifyRegisterFISD2H result->Status=0x%x", result->Status);
    if (result->Status & (ATA_STS_DEV_ERROR | ATA_STS_DEV_FAULT)) {
        TRACE("__VerifyRegisterFISD2H result->Error=0x%x", result->Error);
        if (result->Status & ATA_STS_DEV_ERROR) { PrintTaskDataErrorString(result->Error); }
        return OS_EDEVFAULT;
    }
    return OS_EOK;
}

void
AhciTransactionHandleResponse(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction,
    _In_ size_t            bytesTransferred)
{
    oserr_t status;

    // Repeated interrupt status must not account for the same completion twice
    // while DMAEndTransfer or DMAUnmap is waiting for a cleanup retry.
    if (transaction->State != TransactionInProgress) {
        return;
    }

    status = __VerifyRegisterFISD2H(transaction);
    if (bytesTransferred > transaction->CommandBytes) {
        bytesTransferred = 0;
        status = OS_EDEVFAULT;
    }
    
    transaction->Dma.Completed += bytesTransferred;
    transaction->SectorsTransferred = transaction->Dma.Completed / transaction->Target.SectorSize;
    if (status == OS_EOK && bytesTransferred != transaction->CommandBytes) {
        // A short command leaves a hole before the next chunk. End at the
        // completed prefix instead of copying back bytes the device never wrote.
        status = OS_EDEVFAULT;
    }
    
    if (status == OS_EOK && transaction->BytesLeft != 0) {
        status = __QueueTransaction(controller, port, transaction);
        if (status == OS_EOK) {
            return;
        }
    }
    
    transaction->Result = status;
    transaction->State = TransactionCompleting;
    (void)__FinishTransaction(controller, port, transaction);
}

oserr_t
AhciTransactionsCancel(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    // Both port engines must be stopped before cancellation. Count only data
    // from completed commands, since the current command may be incomplete.
    while (port->Transactions.head != NULL) {
        oserr_t            status;
        AhciTransaction_t* transaction = port->Transactions.head->value;
        if (transaction->State != TransactionCleanup) {
            transaction->Result = OS_ECANCELLED;
            transaction->State = TransactionCompleting;
        }

        status = __FinishTransaction(controller, port, transaction);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

void
AhciTransactionsRetry(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    AhciTransaction_t* transaction;
    int                remaining = port->CleanupTransactions.count;

    // Only failures enter this queue. Limit each pass to the original count so
    // a persistent error cannot spin or repeatedly notify the storage client.
    while (remaining-- > 0 && port->CleanupTransactions.head != NULL) {
        transaction = port->CleanupTransactions.head->value;
        list_remove(&port->CleanupTransactions, &transaction->CleanupHeader);
        transaction->CleanupQueued = false;
        (void)__FinishTransaction(controller, port, transaction);
    }
}
