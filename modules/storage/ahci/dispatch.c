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

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "manager.h"
#include <threads.h>
#include <stdlib.h>

static void
DumpCurrentState(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    // When trace is disabled
    _CRT_UNUSED(Controller);
    _CRT_UNUSED(Port);

    // Dump registers
    WARNING("AHCI.GlobalHostControl 0x%x",
        Controller->Registers->GlobalHostControl);
    WARNING("AHCI.InterruptStatus 0x%x",
        Controller->Registers->InterruptStatus);
    WARNING("AHCI.CcControl 0x%x",
        Controller->Registers->CcControl);

    WARNING("AHCI.Port[%i].CommandAndStatus 0x%x", Port->Id,
        Port->Registers->CommandAndStatus);
    WARNING("AHCI.Port[%i].InterruptEnable 0x%x", Port->Id,
        Port->Registers->InterruptEnable);
    WARNING("AHCI.Port[%i].InterruptStatus 0x%x", Port->Id,
        Port->Registers->InterruptStatus);
    WARNING("AHCI.Port[%i].CommandIssue 0x%x", Port->Id,
        Port->Registers->CommandIssue);
    WARNING("AHCI.Port[%i].TaskFileData 0x%x", Port->Id,
        Port->Registers->TaskFileData);

    WARNING("AHCI.Port[%i].AtaError 0x%x", Port->Id,
        Port->Registers->AtaError);
    WARNING("AHCI.Port[%i].AtaStatus 0x%x", Port->Id,
        Port->Registers->AtaStatus);
}

static void
BuildPRDTTable()
{
    
}

static OsStatus_t
DispatchCommand(
    _In_ AhciTransaction_t* Transaction,
    _In_ Flags_t            Flags,
    _In_ void*              Command, 
    _In_ size_t             CommandLength,
    _In_ void*              AtapiCommand, 
    _In_ size_t             AtapiCommandLength)
{
    AHCICommandHeader_t* CommandHeader;
    AHCICommandTable_t*  CommandTable;
    uintptr_t            BufferPointer;
    size_t               BytesLeft = Transaction->SectorCount * Transaction->Device->SectorSize;
    int                  PrdtIndex = 0;

    TRACE("AhciCommandDispatch(Port %u, Flags 0x%x, Length %u, TransferSize 0x%x)",
        Transaction->Device->Port->Id, Flags, CommandLength, BytesLeft);

    // Assert that buffer is DWORD aligned, this must be true
    if (((uintptr_t)Transaction->Address & 0x3) != 0) {
        ERROR("AhciCommandDispatch::Buffer was not dword aligned (0x%x)",
            Transaction->Device->Port->Id, Transaction->Address);
        goto Error;
    }

    // Assert that buffer length is an even byte-count requested
    if ((BytesLeft & 0x1) != 0) {
        ERROR("AhciCommandDispatch::BufferLength is odd, must be even",
            Transaction->Device->Port->Id);
        goto Error;
    }

    // Get a reference to the command slot and reset the data in the command table
    CommandHeader = &Transaction->Device->Port->CommandList->Headers[Transaction->Slot];
    CommandTable  = (AHCICommandTable_t*)((uint8_t*)Transaction->Device->Port->CommandTable
            + (AHCI_COMMAND_TABLE_SIZE * Transaction->Slot));
    memset(CommandTable, 0, AHCI_COMMAND_TABLE_SIZE);

    // Sanitizie packet lenghts
    if (CommandLength > 64 || AtapiCommandLength > 16) {
        ERROR("AHCI::Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)",
            CommandLength, AtapiCommandLength);
        goto Error;
    }

    // Copy data over into the packets based on type
    if (Command != NULL) {
        memcpy(&CommandTable->FISCommand[0], Command, CommandLength);
    }
    if (AtapiCommand != NULL) {
        memcpy(&CommandTable->FISAtapi[0], AtapiCommand, AtapiCommandLength);
    }

    // Build PRDT entries
    TRACE("Building PRDT Table");
    BufferPointer = Transaction->Address;
    while (BytesLeft > 0) {
        AHCIPrdtEntry_t* Prdt = &CommandTable->PrdtEntry[PrdtIndex];
        size_t TransferLength = MIN(AHCI_PRDT_MAX_LENGTH, BytesLeft);

        // Set buffer information and transfer sizes
        Prdt->DataBaseAddress      = LODWORD(BufferPointer);
        Prdt->DataBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(BufferPointer) : 0;
        Prdt->Descriptor           = TransferLength - 1; // N - 1

        TRACE("PRDT %u, Address 0x%x, Length 0x%x", PrdtIndex, Prdt->DataBaseAddress, Prdt->Descriptor);

        // Adjust counters
        BufferPointer += TransferLength;
        BytesLeft     -= TransferLength;
        PrdtIndex++;

        // If this is the last PRDT packet, set IOC
        if (BytesLeft == 0) {
            Prdt->Descriptor |= AHCI_PRDT_IOC;
        }
    }

    // Update command table to the new command
    CommandHeader->PRDByteCount = 0;
    CommandHeader->TableLength  = (uint16_t)PrdtIndex;
    CommandHeader->Flags        = (uint16_t)(CommandLength >> 2);
    TRACE("PRDT Count %u, Number of DW's %u", CommandHeader->TableLength, CommandHeader->Flags);

    // Update transfer with the dispatch flags
    if (Flags & DISPATCH_ATAPI) {
        CommandHeader->Flags |= (1 << 5);
    }
    if (Flags & DISPATCH_WRITE) {
        CommandHeader->Flags |= (1 << 6);
    }
    if (Flags & DISPATCH_PREFETCH) {
        CommandHeader->Flags |= (1 << 7);
    }
    if (Flags & DISPATCH_CLEARBUSY) {
        CommandHeader->Flags |= (1 << 10);
    }

    // Set the port multiplier
    CommandHeader->Flags |= (DISPATCH_MULTIPLIER(Flags) << 12);
    Transaction->Header.Key.Value.Integer = Transaction->Slot;

    // Add transaction to list
    CollectionAppend(Transaction->Device->Port->Transactions, &Transaction->Header);
    TRACE("Enabling command on slot %u", Transaction->Slot);
    AhciPortStartCommandSlot(Transaction->Device->Port, Transaction->Slot);

#ifdef __TRACE
    // Dump state
    thrd_sleepex(5000);
    AhciDumpCurrentState(Transaction->Device->Controller, Transaction->Device->Port);
#endif
    return OsSuccess;

Error:
    return OsError;
}

void
PrintTaskDataErrorString(uint8_t TaskDataError)
{
    if (TaskDataError & ATA_ERR_DEV_EOM) {
        ERROR("AHCI::Transmission Error, Invalid LBA(sector) range given, end of media.");
    }
    else if (TaskDataError & ATA_ERR_DEV_IDNF) {
        ERROR("AHCI::Transmission Error, Invalid sector range given.");
    }
    else {
        ERROR("AHCI::Transmission Error, error 0x%x", TaskDataError);
    }
}

OsStatus_t
AhciVerifyRegisterFIS(
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

static void
ComposeRegisterFIS(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction,
    _In_ FISRegisterH2D_t*  Fis)
{
    int Device = 0; // TODO: what is this again?
    
    Fis.Type    = LOBYTE(FISRegisterH2D);
    Fis.Flags  |= FIS_HOST_TO_DEVICE;
    Fis.Command = LOBYTE(Transaction->Command);
    Fis.Device  = 0x40 | ((LOBYTE(Device) & 0x1) << 4);
    
    // Handle LBA to CHS translation if disk uses
    // the CHS scheme
    if (Device->AddressingMode == 0) {
        //uint16_t Head = 0, Cylinder = 0, Sector = 0;

        // Step 1 -> Transform LBA into CHS

        // Set CHS params

        // Set count
        Fis.Count = (uint16_t)(Transaction->SectorCount & 0xFF);
    }
    else if (Device->AddressingMode == 1 || Device->AddressingMode == 2) {
        // Set LBA 28 parameters
        Fis.SectorNo            = LOBYTE(SectorLBA);
        Fis.CylinderLow         = (uint8_t)((SectorLBA >> 8) & 0xFF);
        Fis.CylinderHigh        = (uint8_t)((SectorLBA >> 16) & 0xFF);
        Fis.SectorNoExtended    = (uint8_t)((SectorLBA >> 24) & 0xFF);

        // If it's an LBA48, set LBA48 params as well
        if (Device->AddressingMode == 2) {
            Fis.CylinderLowExtended     = (uint8_t)((SectorLBA >> 32) & 0xFF);
            Fis.CylinderHighExtended    = (uint8_t)((SectorLBA >> 40) & 0xFF);

            // Count is 16 bit here
            Fis.Count = (uint16_t)(Transaction->SectorCount & 0xFFFF);
        }
        else {
            // Count is 8 bit in lba28
            Fis.Count = (uint16_t)(Transaction->SectorCount & 0xFF);
        }
    }
}

OsStatus_t
AhciDispatchRegisterFIS(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction)
{
    FISRegisterH2D_t Fis = { 0 };
    OsStatus_t       Status;
    Flags_t          Flags;

    TRACE("AhciDispatchRegisterFIS(Cmd 0x%x, Sector 0x%x)",
        LOBYTE(Transaction->Command), LODWORD(Transaction->Sector));
    
    ComposeRegisterFIS(Device, Transaction, &Fis);
    
    // Start out by building dispatcher flags here
    Flags = DISPATCH_MULTIPLIER(0);
    
    // Atapi device?
    if (ReadVolatile32(&Device->Port->Registers->Signature) == SATA_SIGNATURE_ATAPI) {
        Flags |= DISPATCH_ATAPI;
    }

    // Determine direction of operation
    if (Write) {
        Flags |= DISPATCH_WRITE;
    }
    
    // Execute command - we do this asynchronously
    // so we must handle the rest of this later on
    return AhciCommandDispatch(Transaction, Flags, &Fis, sizeof(FISRegisterH2D_t), NULL, 0);
}

OsStatus_t 
AhciCommandFinish(
    _In_ AhciTransaction_t* Transaction)
{
    StorageOperationResult_t Result = { 0 };
    TRACE("AhciCommandFinish()");

    // Verify the command execution
    Result.Status             = AhciVerifyRegisterFIS(Transaction);
    Result.SectorsTransferred = Transaction->SectorCount;
    
    // Release it, and handle callbacks
    AhciPortReleaseCommandSlot(Transaction->Device->Port, Transaction->Slot);
    if (Transaction->ResponseAddress.Thread == UUID_INVALID) {
        AhciManagerCreateDeviceCallback(Transaction->Device);
    }
    else {
        RPCRespond(&Transaction->ResponseAddress, (void*)&Result, sizeof(StorageOperationResult_t));
    }
    free(Transaction);
    return Result.Status;
}
