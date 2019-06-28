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
    size_t           MaxSectors;
} CommandTable[] = {
    { __STORAGE_OPERATION_READ, 0, 2, AtaPIOReadExt, 0xFFFF },
    { __STORAGE_OPERATION_READ, 0, 1, AtaPIORead, 0xFF },
    { __STORAGE_OPERATION_READ, 1, 2, AtaDMAReadExt, 0xFFFF },
    { __STORAGE_OPERATION_READ, 1, 1, AtaDMARead, 0xFF },
    
    { __STORAGE_OPERATION_WRITE, 0, 2, AtaPIOWriteExt, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 0, 1, AtaPIOWrite, 0xFF },
    { __STORAGE_OPERATION_WRITE, 1, 2, AtaDMAWriteExt, 0xFFFF },
    { __STORAGE_OPERATION_WRITE, 1, 1, AtaDMAWrite, 0xFF },
    { -1, -1, -1, 0, 0 }
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
    memcpy((void*)&Transaction->ResponseAddress, Address, sizeof(MRemoteCallAddress_t));
    Transaction->Header.Key.Id = TransactionId++;
    
    // Do not bother to check return code again, things should go ok now
    MemoryGetSharedMetrics(Operation->BufferHandle, NULL, &Transaction->Frame[0]);
    
    StartOffset              = (Transaction->Frame[0] % AhciManagerGetFrameSize()) + Operation->BufferOffset);
    Transaction->Frame[0]   &= ~(AhciManagerGetFrameSize() - 1);
    Transaction->FrameIndex  = StartOffset / AhciManagerGetFrameSize();
    Transaction->FrameOffset = StartOffset % AhciManagerGetFrameSize();
    Transaction->Sector      = Operation->AbsoluteSector;
    Transaction->SectorCount = Operation->SectorCount;
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
            Transaction->Command     = CommandTable[i].Command;
            Transaction->SectorCount = MIN(Transaction->SectorCount, CommandTable[i].MaxSectors);
            break;
        }
        i++;
    }
    
    // TODO: handle this
    assert(CommandTable[i].Direction != -1);
    
    // The transaction is now prepared and ready for the dispatch
    Status = AhciDeviceQueueTransaction(Device, Transaction);
    if (Status != OsSuccess) {
        // Cleanup transaction
    }
    return Status;
}
