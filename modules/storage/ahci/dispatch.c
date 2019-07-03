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
#include "dispatch.h"
#include <threads.h>

static void
DumpCurrentState(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    _CRT_UNUSED(Controller);
    _CRT_UNUSED(Port);

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
BuildPRDTTable(
    _In_  AhciDevice_t*       Device,
    _In_  AhciTransaction_t*  Transaction,
    _In_  AHCICommandTable_t* CommandTable,
    _Out_ int*                PrdtCountOut)
{
    size_t BytesQueued = 0;
    int    i;
    TRACE("Building PRDT Table");
    
    // Build PRDT entries
    for (i = 0; i < AHCI_COMMAND_TABLE_PRDT_COUNT && Transaction->BytesLeft > 0; i++) {
        AHCIPrdtEntry_t* Prdt           = &CommandTable->PrdtEntry[i];
        uintptr_t        Address        = Transaction->SgList[Transaction->SgIndex].address + Transaction->SgOffset;
        size_t           TransferLength = MIN(AHCI_PRDT_MAX_LENGTH, 
            MIN(Transaction->BytesLeft, Transaction->SgList[Transaction->SgIndex].length - Transaction->SgOffset));
        
        // On some transfers (sector transfers) we would like to have sector alignment
        // on the transfer we read. This should only ever be neccessary if we cannot fit
        // the entire transaction into <AHCI_COMMAND_TABLE_PRDT_COUNT> prdt entries. Then 
        // we should make sure we transfer on sector boundaries between rounds. So on the last
        // PRDT entry, we must make sure that the total transfer length is on boundaries
        if (i == (AHCI_COMMAND_TABLE_PRDT_COUNT - 1) && Transaction->SectorAlignment &&
            TransferLength != Transaction->BytesLeft) {
            if ((BytesQueued + TransferLength) % Device->SectorSize != 0) {
                if (TransferLength < Device->SectorSize) {
                    // Perform a larger correctional action as no longer have space to reduce
                    // to the next boundary. Instead of correcting the number of bytes to transfer
                    // we drop the previous PRDT entry entirely and let the next transaction round
                    // take care of this.
                    i--;
                    break;
                }
                TransferLength -= (BytesQueued + TransferLength) % Device->SectorSize;
            }
        }
        
        Prdt->DataBaseAddress      = LODWORD(Address);
        Prdt->DataBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(Address) : 0;
        Prdt->Descriptor           = TransferLength - 1; // N - 1

        TRACE("PRDT %u, Address 0x%x, Length 0x%x", PrdtIndex, Prdt->DataBaseAddress, Prdt->Descriptor);

        // Adjust frame index and offset
        Transaction->SgOffset += TransferLength;
        if (Transaction->SgOffset == Transaction->SgList[Transaction->SgIndex].length) {
            Transaction->SgOffset = 0;
            Transaction->SgIndex++;
        }
        Transaction->BytesLeft -= TransferLength;
        BytesQueued            += TransferLength;
    }

    // Set IOC on the last PRDT entry
    CommandTable->PrdtEntry[i].Descriptor |= AHCI_PRDT_IOC;
    *PrdtCountOut                         = i;

    TRACE("PRDT Count %u, Number of DW's %u", CommandHeader->TableLength, CommandHeader->Flags);
}

static size_t
PrepareCommandSlot(
    _In_  AhciDevice_t*      Device,
    _In_  AhciTransaction_t* Transaction)
{
    AHCICommandHeader_t* CommandHeader;
    AHCICommandTable_t*  CommandTable;
    size_t               BytesQueued = Transaction->BytesLeft;
    int                  PrdtCount;

    // Get a reference to the command slot and reset the data in the command table
    CommandHeader = &Device->Port->CommandList->Headers[Transaction->Slot];
    CommandTable  = (AHCICommandTable_t*)((uint8_t*)Device->Port->CommandTable
            + (AHCI_COMMAND_TABLE_SIZE * Transaction->Slot));
    memset(CommandTable, 0, AHCI_COMMAND_TABLE_SIZE);

    // Build the PRDT table
    BuildPRDTTable(Device, Transaction, CommandTable, &PrdtCount);

    // Update command table to the new command
    CommandHeader->PRDByteCount = 0;
    CommandHeader->TableLength  = (uint16_t)(PrdtCount & 0xFFFF);
    return BytesQueued - Transaction->BytesLeft;
}

static OsStatus_t
DispatchCommand(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction,
    _In_ Flags_t            Flags,
    _In_ void*              AtaCommand,
    _In_ size_t             AtaCommandLength,
    _In_ void*              AtapiCommand, 
    _In_ size_t             AtapiCommandLength)
{
    AHCICommandHeader_t* CommandHeader;
    AHCICommandTable_t*  CommandTable;
    size_t               CommandLength = 0;

    TRACE("DispatchCommand(Port %u, Flags 0x%x)", Device->Port->Id, Flags);

    // Assert that buffer is WORD aligned, this must be true
    // The number of bytes to transfer must also be WORD aligned, however as
    // the storage interface is implemented one can only transfer in sectors so
    // this is always true.
    if (((uintptr_t)Transaction->SgOffset & 0x1) != 0) {
        ERROR("DispatchCommand::FrameOffset was not dword aligned (0x%x)", Transaction->SgOffset);
        return OsInvalidParameters;
    }

    if (CommandLength > 64 || AtapiCommandLength > 16) {
        ERROR("AHCI::Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)",
            CommandLength, AtapiCommandLength);
        return OsInvalidParameters;
    }

    CommandHeader = &Device->Port->CommandList->Headers[Transaction->Slot];
    CommandTable  = (AHCICommandTable_t*)((uint8_t*)Device->Port->CommandTable
            + (AHCI_COMMAND_TABLE_SIZE * Transaction->Slot));

    if (AtaCommand != NULL) {
        memcpy(&CommandTable->FISCommand[0], AtaCommand, AtaCommandLength);
        CommandLength = AtaCommandLength;
    }
    
    if (AtapiCommand != NULL) {
        memcpy(&CommandTable->FISAtapi[0], AtapiCommand, AtapiCommandLength);
        CommandLength = AtapiCommandLength;
    }

    CommandHeader->Flags = (uint16_t)(CommandLength >> 2);

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
    
    TRACE("Enabling command on slot %u", Transaction->Slot);
    AhciPortStartCommandSlot(Device->Port, Transaction->Slot);

#ifdef __TRACE
    // Dump state
    thrd_sleepex(5000);
    AhciDumpCurrentState(Device->Controller, Device->Port);
#endif
    return OsSuccess;
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

/************************************************************************
 * RegisterFIS
 ************************************************************************/
static void
ComposeRegisterFIS(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction,
    _In_ FISRegisterH2D_t*  Fis,
    _In_ int                BytesQueued)
{
    uint64_t Sector      = Transaction->Sector + Transaction->SectorsTransferred;
    size_t   SectorCount = BytesQueued / Device->SectorSize;
    int      DeviceLUN   = 0; // TODO: Is this LUN or what is it?

    Fis->Type    = LOBYTE(FISRegisterH2D);
    Fis->Flags  |= FIS_HOST_TO_DEVICE;
    Fis->Command = LOBYTE(Transaction->Command);
    Fis->Device  = 0x40 | ((LOBYTE(DeviceLUN) & 0x1) << 4);
    Fis->Count   = (uint16_t)SectorCount;
    
    // Handle LBA to CHS translation if disk uses
    // the CHS scheme
    if (Device->AddressingMode == AHCI_DEVICE_MODE_CHS) {
        //uint16_t Head = 0, Cylinder = 0, Sector = 0;

        // Step 1 -> Transform LBA into CHS

        // Set CHS params
    }
    else if (Device->AddressingMode == AHCI_DEVICE_MODE_LBA28 || 
             Device->AddressingMode == AHCI_DEVICE_MODE_LBA48) {
        // Set LBA 28 parameters
        Fis->SectorNo            = LOBYTE(Sector);
        Fis->CylinderLow         = (uint8_t)((Sector >> 8) & 0xFF);
        Fis->CylinderHigh        = (uint8_t)((Sector >> 16) & 0xFF);
        Fis->SectorNoExtended    = (uint8_t)((Sector >> 24) & 0xFF);

        // If it's an LBA48, set LBA48 params as well
        if (Device->AddressingMode == AHCI_DEVICE_MODE_LBA48) {
            Fis->CylinderLowExtended     = (uint8_t)((Sector >> 32) & 0xFF);
            Fis->CylinderHighExtended    = (uint8_t)((Sector >> 40) & 0xFF);
        }
    }
}

OsStatus_t
AhciDispatchRegisterFIS(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction)
{
    FISRegisterH2D_t Fis = { 0 };
    Flags_t          Flags;
    int              BytesQueued;

    TRACE("AhciDispatchRegisterFIS(Cmd 0x%x, Sector 0x%x)",
        LOBYTE(Transaction->Command), LODWORD(Transaction->Sector));
    
    // Initialize the command
    BytesQueued = PrepareCommandSlot(Device, Transaction);
    ComposeRegisterFIS(Device, Transaction, &Fis, BytesQueued);
    
    // Start out by building dispatcher flags here
    Flags = DISPATCH_MULTIPLIER(0);
    
    if (Device->Type == DeviceATAPI) {
        Flags |= DISPATCH_ATAPI;
    }

    if (Transaction->Direction == AHCI_XACTION_OUT) {
        Flags |= DISPATCH_WRITE;
    }
    return DispatchCommand(Device, Transaction, Flags, &Fis, sizeof(FISRegisterH2D_t), NULL, 0);
}
