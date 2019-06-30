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

#include "manager.h"

static UUId_t TransactionId = 0;

static struct {
    int              Direction;
    int              DMA;
    int              AddressingMode,
    ATACommandType_t Command;
    size_t           SectorAlignment;
    size_t           MaxSectors;
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
}

OsStatus_t
AhciTransactionCreate(
    _In_ AhciDevice_t*         Device,
    _In_ MRemoteCallAddress_t* Address,
    _In_ StorageOperation_t*   Operation)
{
    AhciTransaction_t* Transaction;
    int                FrameCount;
    OsStatus_t         Status;
    size_t             StartOffset;
    int                i;
    
    if (!Device || !Address || !Operation) {
        return OsInvalidParameters;
    }
    
    Status = MemoryGetSharedMetrics(Operation->BufferHandle, &FrameCount, NULL);
    if (Status != OsSuccess) {
        return OsInvalidParameters;
    }
    
    Transaction = (AhciTransaction_t*)malloc(
        sizeof(AhciTransaction_t) + (sizeof(uintptr_t) * FrameCount));
    if (!Transactions) {
        return OsOutOfMemory;
    }
    
    // Do not bother about zeroing the array
    memset(Transaction, 0, sizeof(AhciTransaction_t));
    memcpy((void*)&Transaction->ResponAtaPIOIdentifyDeviceseAddress, Address, sizeof(MRemoteCallAddress_t));
    Transaction->Header.Key.Id = TransactionId++;
    
    // Do not bother to check return code again, things should go ok now
    MemoryGetSharedMetrics(Operation->BufferHandle, NULL, &Transaction->Frame[0]);
    
    StartOffset              = ((Transaction->Frame[0] % AhciManagerGetFrameSize()) + Operation->BufferOffset);
    Transaction->Frame[0]   &= ~(AhciManagerGetFrameSize() - 1);
    Transaction->FrameIndex  = StartOffset / AhciManagerGetFrameSize();
    Transaction->FrameOffset = StartOffset % AhciManagerGetFrameSize();
    Transaction->Sector      = Operation->AbsoluteSector;
    Transaction->Device      = Device;
    Transaction->State       = TransactionCreated;
    Transaction->Slot        = -1;
    
    // Set upper bound on transaction
    if ((Transaction->Sector + Transaction->SectorCount) >= Device->SectorsLBA) {
        Transaction->SectorCount = Device->SectorsLBA - Transaction->Sector;
    }
    
    // Select the appropriate command
    i = 0
    while (CommandTable[i].Direction != -1) {
        if (CommandTable[i].Direction      == Operation->Direction &&
            CommandTable[i].DMA            == Device->UseDMA &&
            CommandTable[i].AddressingMode == Device->AddressingMode) {
            // Found the appropriate command
            Transaction->Command         = CommandTable[i].Command;
            Transaction->SectorAlignment = CommandTable[i].SectorAlignment;
            Transaction->BytesLeft       = MIN(Operation->SectorCount, CommandTable[i].MaxSectors) * Device->SectorSize;
            break;
        }
        i++;
    }
    
    // TODO: handle this
    assert(CommandTable[i].Direction != -1);
    assert(Transaction->BytesLeft != 0);
    
    // The transaction is now prepared and ready for the dispatch
    Status = AhciDeviceQueueTransaction(Device, Transaction);
    if (Status != OsSuccess) {
        // TODO: Cleanup transaction
    }
    return Status;
}

static OsStatus_t
VerifyRegisterFIS(
    _In_ AhciTransaction_t *Transaction)
{
    AHCIFis_t* Fis = &Transaction->Device->Port->RecievedFisTable[Transaction->Slot];

    // Is the error bit set?
    if (Fis->RegisterD2H.Status & ATA_STS_DEV_ERROR) {
        PrintTaskDataErrorString(Fis->RegisterD2H.Error);
        return OsError;
    }

    // Is the fault bit set?
    if (Fis->RegisterD2H.Status & ATA_STS_DEV_FAULT) {
        ERROR("AHCI::Port (%i): Device Fault, error 0x%x",
            Transaction->Device->Port->Id, (size_t)Fis->RegisterD2H.Error);
        return OsError;
    }
    return OsSuccess;
}

OsStatus_t
AhciTransactionHandleResponse(
    _In_ AhciTransaction_t* Transaction)
{
    StorageOperationResult_t Result = { 0 };
    TRACE("AhciCommandFinish()");

    // Verify the command execution
    // if transaction == register_fis_h2d
    // verify_register_fis_d2h
    Result.Status             = VerifyRegisterFIS(Transaction);
    Result.SectorsTransferred = Transaction->SectorCount;
    
    // Release it, and handle callbacks
    if (Transaction->ResponseAddress.Thread == UUID_INVALID) {
        AhciManagerCreateDeviceCallback(Transaction->Device);
    }
    else {
        RPCRespond(&Transaction->ResponseAddress, (void*)&Result, sizeof(StorageOperationResult_t));
    }
    free(Transaction);
    return Result.Status;
}
