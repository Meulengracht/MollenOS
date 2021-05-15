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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */

//#define __TRACE

#include <assert.h>
#include "dispatch.h"
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>
#include <string.h>

#include "ctt_driver_service_server.h"
#include "ctt_storage_service_server.h"

static UUId_t g_nextTransactionId = 0;

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

static OsStatus_t __AllocateCommandSlot(
        _In_ AhciPort_t*        port,
        _In_ AhciTransaction_t* transaction)
{
    int        portSlot = -1;
    OsStatus_t status;

    // OK so the transaction we just recieved needs to be queued up,
    // so we must initally see if we can allocate a new slot on the port
    status = AhciPortAllocateCommandSlot(port, &portSlot);

    __SetTransferKey(transaction, portSlot);
    if (status == OsSuccess) {
        transaction->State = TransactionInProgress;
    }
    else {
        transaction->State = TransactionQueued;
    }
    list_append(&port->Transactions, &transaction->Header);
    return status;
}

static OsStatus_t __QueueTransaction(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    OsStatus_t status = OsSuccess;

    TRACE("__QueueTransaction(controller=0x%" PRIxIN ", port=0x%" PRIxIN ", transaction=0x%" PRIxIN ")",
          controller, port, transaction);

    // update header with the slot
    if (__GetTransferKey(transaction) == -1) {
        if (__AllocateCommandSlot(port, transaction) != OsSuccess) {
            goto exit;
        }
    }

    // If we reach here we've successfully allocated a slot, now we should dispatch 
    // the transaction
    TRACE("__QueueTransaction transaction->Type=%u", transaction->Type);
    switch (transaction->Type) {
        case TransactionRegisterFISH2D: {
            status = AhciDispatchRegisterFIS(controller, port, transaction);
        } break;
        
        default: {
            assert(0);
        } break;
    }

exit:
    TRACE("__QueueTransaction returns=%u (transaction->State=%u)",
          status, transaction != NULL ? transaction->State : 0);
    return status;
}

static void
AhciTransactionDestroy(
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    int portSlot;

    if (!transaction) {
        return;
    }

    portSlot = __GetTransferKey(transaction);
    if (portSlot != -1) {
        // we need port
    }

    // Detach from our buffer reference
    list_remove(&port->Transactions, &transaction->Header);
    AhciPortFreeCommandSlot(port, portSlot);
    dma_detach(&transaction->DmaAttachment);
    free(transaction->DmaTable.entries);
    free(transaction);
}

static AhciTransaction_t* __CreateTransaction(
        _In_ AhciDevice_t*          device,
        _In_ struct gracht_message* message,
        _In_ int                    direction,
        _In_ uint64_t               sector,
        _In_ struct dma_attachment* dmaAttachment,
        _In_ unsigned int           bufferOffset)
{
    UUId_t             transactionId;
    AhciTransaction_t* transaction;
    size_t             transactionSize = message ?
            sizeof(AhciTransaction_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message) :
            sizeof(AhciTransaction_t);

    transaction = (AhciTransaction_t*)malloc(transactionSize);
    if (!transaction) {
        return NULL;
    }

    transactionId = g_nextTransactionId++;

    // Do not bother about zeroing the array
    memset(transaction, 0, sizeof(AhciTransaction_t));
    memcpy(&transaction->DmaAttachment, dmaAttachment, sizeof(struct dma_attachment));

    ELEMENT_INIT(&transaction->Header, (void*)(uintptr_t)-1, transaction);
    transaction->Id      = transactionId;
    transaction->Type    = TransactionRegisterFISH2D;
    transaction->Sector  = sector;
    transaction->State   = TransactionCreated;

    transaction->Target.Type = device->Type;
    transaction->Target.SectorSize = device->SectorSize;
    transaction->Target.AddressingMode = device->AddressingMode;

    if (direction == __STORAGE_OPERATION_READ) {
        transaction->Direction = AHCI_XACTION_IN;
    }
    else {
        transaction->Direction = AHCI_XACTION_OUT;
    }

    if (message) {
        gracht_server_defer_message(message, &transaction->DeferredMessage[0]);
    }

    // Do not bother to check return code again, things should go ok now
    dma_get_sg_table(dmaAttachment, &transaction->DmaTable, -1);
    dma_sg_table_offset(&transaction->DmaTable, bufferOffset, &transaction->SgIndex, &transaction->SgOffset);
    return transaction;
}

OsStatus_t
AhciTransactionControlCreate(
    _In_ AhciDevice_t* ahciDevice,
    _In_ AtaCommand_t  ataCommand,
    _In_ size_t        length,
    _In_ int           direction)
{
    AhciTransaction_t*    transaction;
    OsStatus_t            status;
    struct dma_attachment dmaAttachment;

    TRACE("AhciTransactionControlCreate(ahciDevice=0x%" PRIxIN ", ataCommand=0x%x, length=0x%" PRIxIN ", direction=%i)",
          ahciDevice, ataCommand, length, direction);
    
    if (!ahciDevice) {
        status = OsInvalidParameters;
        goto exit;
    }

    status = dma_attach(ahciDevice->Port->InternalBuffer.handle, &dmaAttachment);
    if (status != OsSuccess) {
        goto exit;
    }

    transaction = __CreateTransaction(ahciDevice, NULL, direction, 0, &dmaAttachment, 0);
    if (!transaction) {
        dma_detach(&dmaAttachment);
        status = OsOutOfMemory;
        goto exit;
    }

    // setup extra members for controlrecv_message
    transaction->Internal  = 1;
    transaction->Command   = ataCommand;
    transaction->BytesLeft = length;

    // The transaction is now prepared and ready for the dispatch
    status = __QueueTransaction(ahciDevice->Controller, ahciDevice->Port, transaction);
    if (status != OsSuccess) {
        AhciTransactionDestroy(ahciDevice->Port, transaction);
    }

exit:
    TRACE("AhciTransactionControlCreate returns=%u", status);
    return status;
}

static inline struct __AhciCommandTableEntry* __GetCommand(
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

OsStatus_t
AhciTransactionStorageCreate(
    _In_ AhciDevice_t*          device,
    _In_ struct gracht_message* message,
    _In_ int                    direction,
    _In_ uint64_t               sector,
    _In_ UUId_t                 bufferHandle,
    _In_ unsigned int           bufferOffset,
    _In_ size_t                 sectorCount)
{
    struct __AhciCommandTableEntry* command;
    struct dma_attachment           dmaAttachment;
    AhciTransaction_t*              transaction = NULL;
    OsStatus_t                      status;
    TRACE("AhciTransactionStorageCreate(device=0x%" PRIxIN ", sector=0x%" PRIxIN ", sectorCount=0x%" PRIxIN ", direction=%i)",
          device, sector, sectorCount, direction);

    if (!device) {
        status = OsInvalidParameters;
        goto exit;
    }
    
    status = dma_attach(bufferHandle, &dmaAttachment);
    if (status != OsSuccess) {
        status = OsInvalidParameters;
        goto exit;
    }

    transaction = __CreateTransaction(device, message, direction, sector, &dmaAttachment, bufferOffset);
    if (!transaction) {
        dma_detach(&dmaAttachment);
        status = OsOutOfMemory;
        goto exit;
    }

    // Set upper bound on transaction
    if ((transaction->Sector + sectorCount) >= device->SectorCount) {
        sectorCount = device->SectorCount - transaction->Sector;
        TRACE("AhciTransactionStorageCreate truncated sectorCount=" PRIxIN ", device->SectorCount=%" PRIuIN,
              sectorCount, device->SectorCount);
    }
    
    // Select the appropriate command
    command = __GetCommand(device, direction);
    if (!command) {
        status = OsInvalidParameters;
        goto exit;
    }

    transaction->Command = command->Command;
    transaction->SectorAlignment = command->SectorAlignment;
    transaction->BytesLeft = MIN(sectorCount, command->MaxSectors) * device->SectorSize;

    // The transaction is now prepared and ready for the dispatch
    status = __QueueTransaction(device->Controller, device->Port, transaction);

exit:
    if (status != OsSuccess && device && transaction) {
        AhciTransactionDestroy(device->Port, transaction);
    }
    return status;
}

void ctt_storage_transfer_invocation(struct gracht_message* message, const UUId_t deviceId,
        const enum sys_transfer_direction direction, const unsigned int sectorLow, const unsigned int sectorHigh,
        const UUId_t bufferId, const size_t offset, const size_t sectorCount)
{
    AhciDevice_t*   device = AhciManagerGetDevice(deviceId);
    OsStatus_t      status;
    LargeUInteger_t sector;
    
    sector.u.LowPart = sectorLow;
    sector.u.HighPart = sectorHigh;
    
    status = AhciTransactionStorageCreate(device, message, (int)direction, sector.QuadPart,
        bufferId, offset, sectorCount);
    if (status != OsSuccess) {
        ctt_storage_transfer_response(message, status, 0);
    }
}

OsStatus_t
AhciManagerCancelTransaction(
    _In_ AhciTransaction_t* transaction)
{
    return OsNotSupported;
}

static void __DumpD2HFis(
        _In_ AHCIFis_t* combinedFis)
{
    TRACE("RegisterD2H.Type 0x%x, RegisterD2H.Flags 0x%x",
          combinedFis->RegisterD2H.Type, combinedFis->RegisterD2H.Flags);
    TRACE("RegisterD2H.Status 0x%x, RegisterD2H.Error 0x%x",
          combinedFis->RegisterD2H.Status, combinedFis->RegisterD2H.Error);
    TRACE("RegisterD2H.Lba0 0x%x, RegisterD2H.Lba1 0x%x",
          combinedFis->RegisterD2H.Lba0, combinedFis->RegisterD2H.Lba1);
    TRACE("RegisterD2H.Device 0x%x, RegisterD2H.Lba2 0x%x",
          combinedFis->RegisterD2H.Device, combinedFis->RegisterD2H.Lba2);
    TRACE("RegisterD2H.Lba3 0x%x, RegisterD2H.Count 0x%x",
          combinedFis->RegisterD2H.Lba3, combinedFis->RegisterD2H.Count);
}

static OsStatus_t __VerifyRegisterFISD2H(
    _In_ AhciTransaction_t* transaction)
{
    FISRegisterD2H_t* result = (FISRegisterD2H_t*)&transaction->Response.RegisterD2H;
    TRACE("__VerifyRegisterFISD2H(transaction=0x%" PRIxIN ")", transaction);

    // Is the error bit set?
    TRACE("__VerifyRegisterFISD2H result->Status=0x%x", result->Status);
    if (result->Status & (ATA_STS_DEV_ERROR | ATA_STS_DEV_FAULT)) {
        TRACE("__VerifyRegisterFISD2H result->Error=0x%x", result->Error);
        if (result->Status & ATA_STS_DEV_ERROR) { PrintTaskDataErrorString(result->Error); }
        return OsDeviceError;
    }
    return OsSuccess;
}

static void __FinishTransaction(
        _In_ AhciPort_t*        port,
        _In_ AhciTransaction_t* transaction,
        _In_ OsStatus_t         status)
{
    if (transaction->Internal) {
        AhciManagerHandleControlResponse(port, transaction);
    }
    else {
        ctt_storage_transfer_response(&transaction->DeferredMessage[0],
                                      status, transaction->SectorsTransferred);
    }
    AhciTransactionDestroy(port, transaction);
}

void
AhciTransactionHandleResponse(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction,
    _In_ size_t             bytesTransferred)
{
    OsStatus_t osStatus = OsNotSupported;
    
    TRACE("AhciTransactionHandleResponse(bytesTransferred=%" PRIuIN ")", bytesTransferred);
    
    // Verify the command execution
    if (transaction->Type == TransactionRegisterFISH2D) {
#ifdef __TRACE
        __DumpD2HFis(port->RecievedFisDMA.buffer);
#endif
        osStatus = __VerifyRegisterFISD2H(transaction);
    }
    else {
        assert(0);
    }

    transaction->SectorsTransferred += (bytesTransferred / transaction->Target.SectorSize);

    // Is the transaction finished? (Or did it error?)
    if (osStatus != OsSuccess || transaction->BytesLeft == 0) {
        __FinishTransaction(port, transaction, osStatus);
        return;
    }

    osStatus = __QueueTransaction(controller, port, transaction);
    if (osStatus != OsSuccess) {
        __FinishTransaction(port, transaction, osStatus);
    }
}
