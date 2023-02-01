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
#include <ddk/utils.h>
#include "manager.h"
#include "dispatch.h"
#include <string.h>

static void __DumpCurrentState(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    _CRT_UNUSED(controller);
    _CRT_UNUSED(port);

    TRACE("AHCI.GlobalHostControl 0x%x",
            controller->Registers->GlobalHostControl);
    TRACE("AHCI.InterruptStatus 0x%x",
            controller->Registers->InterruptStatus);
    TRACE("AHCI.CcControl 0x%x",
            controller->Registers->CcControl);

    TRACE("AHCI.Port[%i].CommandAndStatus 0x%x", port->Id,
            port->Registers->CommandAndStatus);
    TRACE("AHCI.Port[%i].InterruptEnable 0x%x", port->Id,
            port->Registers->InterruptEnable);
    TRACE("AHCI.Port[%i].InterruptStatus 0x%x", port->Id,
            port->Registers->InterruptStatus);
    TRACE("AHCI.Port[%i].CommandIssue 0x%x", port->Id,
            port->Registers->CI);
    TRACE("AHCI.Port[%i].TaskFileData 0x%x", port->Id,
            port->Registers->TaskFileData);

    TRACE("AHCI.Port[%i].AtaError 0x%x", port->Id,
            port->Registers->SERR);
    TRACE("AHCI.Port[%i].AtaStatus 0x%x", port->Id,
            port->Registers->STSS);
    TRACE("AHCI.Port[%i].AtaActive 0x%x", port->Id,
          port->Registers->SACT);
}

static int __CommandIsNQC(uint8_t command)
{
    if (command == AtaFPDmaReadQueued || command == AtaFPDmaWriteQueued) {
        return 1;
    }
    return 0;
}

static void __BuildPRDTTable(
    _In_  AhciTransaction_t*  transaction,
    _In_  AHCICommandTable_t* commandTable,
    _In_  size_t              sectorSize,
    _Out_ int*                prdtCountOut)
{
    size_t bytesQueued = 0;
    int    i;
    TRACE("__BuildPRDTTable(transaction=0x%" PRIxIN ", commandTable=0x%" PRIxIN ", sectorSize=%u)",
          transaction, commandTable, sectorSize);

    for (i = 0; i < AHCI_COMMAND_TABLE_PRDT_COUNT && transaction->BytesLeft > 0; i++) {
        AHCIPrdtEntry_t* prdt = &commandTable->PrdtEntry[i];
        uintptr_t        address = transaction->SHMTable.Entries[transaction->SgIndex].Address + transaction->SgOffset;
        size_t           transferLength = MIN(AHCI_PRDT_MAX_LENGTH,
                                              MIN(
                                               transaction->BytesLeft,
                                               transaction->SHMTable.Entries[transaction->SgIndex].Length - transaction->SgOffset));
        
        // On some transfers (sector transfers) we would like to have sector alignment
        // on the transfer we read. This should only ever be neccessary if we cannot fit
        // the entire transaction into <AHCI_COMMAND_TABLE_PRDT_COUNT> prdt entries. Then 
        // we should make sure we transfer on sector boundaries between rounds. So on the last
        // PRDT entry, we must make sure that the total transfer length is on boundaries
        if (i == (AHCI_COMMAND_TABLE_PRDT_COUNT - 1) && transaction->SectorAlignment &&
            transferLength != transaction->BytesLeft) {
            if ((bytesQueued + transferLength) % sectorSize != 0) {
                if (transferLength < sectorSize) {
                    assert(0);
                    // Perform a larger correctional action as no longer have space to reduce
                    // to the next boundary. Instead of correcting the number of bytes to transfer
                    // we drop the previous PRDT entry entirely and let the next transaction round
                    // take care of this.
                    i--;
                    break;
                }
                transferLength -= (bytesQueued + transferLength) % sectorSize;
            }
        }

        prdt->DataBaseAddress      = LODWORD(address);
        prdt->DataBaseAddressUpper = (sizeof(void*) > 4) ? HIDWORD(address) : 0;
        prdt->Descriptor           = transferLength - 1; // N - 1

        TRACE("__BuildPRDTTable i=%i, address=0x%" PRIxIN ", transferLength=0x%" PRIxIN,
              i, address, transferLength);

        // Adjust frame index and offset
        transaction->SgOffset += transferLength;
        if (transaction->SgOffset == transaction->SHMTable.Entries[transaction->SgIndex].Length) {
            transaction->SgOffset = 0;
            transaction->SgIndex++;
        }
        transaction->BytesLeft -= transferLength;
        bytesQueued            += transferLength;
    }

    // Set IOC on the last PRDT entry
    commandTable->PrdtEntry[i].Descriptor |= AHCI_PRDT_IOC;
    *prdtCountOut = i;
}

static size_t __PrepareCommandSlot(
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction,
    _In_ size_t             sectorSize,
    _In_ int                commandSlot)
{
    AHCICommandList_t*   commandList;
    AHCICommandHeader_t* commandHeader;
    AHCICommandTable_t*  commandTable;
    size_t               bytesQueued = transaction->BytesLeft;
    int                  prdtCount;

    TRACE("__PrepareCommandSlot(port=0x%" PRIxIN ", transaction=0x%" PRIxIN ", sectorSize=0x%" PRIxIN ")",
          port, transaction, sectorSize);

    // Get a reference to the command slot and reset the data in the command table
    commandList   = (AHCICommandList_t*)port->CommandListDMA.Buffer;
    commandTable  = (AHCICommandTable_t*)port->CommandTableDMA.Buffer;
    commandHeader = &commandList->Headers[commandSlot];

    // Build the PRDT table
    __BuildPRDTTable(transaction, commandTable, sectorSize, &prdtCount);

    // Update command table to the new command
    commandHeader->PRDByteCount = 0;
    commandHeader->TableLength  = (uint16_t)(prdtCount & 0xFFFF);
    TRACE("__PrepareCommandSlot returns=%" PRIuIN, bytesQueued - transaction->BytesLeft);
    return bytesQueued - transaction->BytesLeft;
}

static oserr_t __DispatchCommand(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction,
    _In_ unsigned int       flags,
    _In_ void*              ataCommand,
    _In_ size_t             ataCommandLength,
    _In_ void*              atapiCommand,
    _In_ size_t             atapiCommandLength)
{
    AHCICommandList_t*   commandList;
    AHCICommandHeader_t* commandHeader;
    AHCICommandTable_t*  commandTable;
    size_t               commandLength = 0;
    int                  commandSlot = __GetTransferKey(transaction);

    TRACE("__DispatchCommand(transaction=0x%" PRIxIN ", flags=0x%x, ataCommandLength=0x%" PRIuIN ")",
          transaction, flags, ataCommandLength);

#ifdef __TRACE
    __DumpCurrentState(controller, port);
#endif

    // Assert that buffer is WORD aligned, this must be true
    // The number of bytes to transfer must also be WORD aligned, however as
    // the storage interface is implemented one can only transfer in sectors so
    // this is always true.
    if (((uintptr_t)transaction->SgOffset & 0x1) != 0) {
        ERROR("__DispatchCommand FrameOffset was not dword aligned (0x%x)", transaction->SgOffset);
        return OS_EINVALPARAMS;
    }

    if (commandLength > 64 || atapiCommandLength > 16) {
        ERROR("__DispatchCommand Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)",
              commandLength, atapiCommandLength);
        return OS_EINVALPARAMS;
    }

    commandList   = (AHCICommandList_t*)port->CommandListDMA.Buffer;
    commandTable  = (AHCICommandTable_t*)port->CommandTableDMA.Buffer;
    commandHeader = &commandList->Headers[commandSlot];

    if (ataCommand != NULL) {
        memcpy(&commandTable->FISCommand[0], ataCommand, ataCommandLength);
        commandLength = ataCommandLength;
    }
    
    if (atapiCommand != NULL) {
        memcpy(&commandTable->FISAtapi[0], atapiCommand, atapiCommandLength);
        commandLength = atapiCommandLength;
    }

    commandHeader->Flags = (uint16_t)(commandLength >> 2);

    // Update transfer with the dispatch flags
    if (flags & DISPATCH_ATAPI) {
        commandHeader->Flags |= (1 << 5);
    }
    if (flags & DISPATCH_WRITE) {
        commandHeader->Flags |= (1 << 6);
    }
    if (flags & DISPATCH_PREFETCH) {
        commandHeader->Flags |= (1 << 7);
    }
    if (flags & DISPATCH_CLEARBUSY) {
        commandHeader->Flags |= (1 << 10);
    }

    // Set the port multiplier
    commandHeader->Flags |= (DISPATCH_MULTIPLIER(flags) << 12);

    TRACE("__DispatchCommand transaction->Slot=%u, commandHeader->Flags=0x%x",
          commandSlot, commandHeader->Flags);
    AhciPortStartCommandSlot(port, commandSlot, __CommandIsNQC(transaction->Command));

#ifdef __TRACE
    // Dump state
    //thrd_sleep2(5000);
    __DumpCurrentState(controller, port);
#endif
    return OS_EOK;
}

void
PrintTaskDataErrorString(uint8_t taskDataError)
{
    if (taskDataError & ATA_ERR_DEV_EOM) {
        ERROR("AHCI::Transmission Error, Invalid LBA(sector) range given, end of media.");
    }
    else if (taskDataError & ATA_ERR_DEV_IDNF) {
        ERROR("AHCI::Transmission Error, Invalid sector range given.");
    }
    else {
        ERROR("AHCI::Transmission Error, error 0x%x", taskDataError);
    }
}

/************************************************************************
 * RegisterFIS
 ************************************************************************/
static void __FillRegisterFIS(
    _In_ AhciTransaction_t* transaction,
    _In_ FISRegisterH2D_t*  registerH2D,
    _In_ int                bytesQueued,
    _In_ size_t             sectorSize,
    _In_ int                addressingMode)
{
    uint64_t sector      = transaction->Sector + transaction->SectorsTransferred;
    size_t   sectorCount = bytesQueued / sectorSize;
    int      deviceLun   = 0; // TODO: Is this LUN or what is it?

    TRACE("__FillRegisterFIS(bytesQueued=%i, sectorSize=0x%" PRIxIN ", addressingMode=%i)",
          bytesQueued, sectorSize, addressingMode);
    TRACE("__FillRegisterFIS sector=%" PRIuIN ", sectorCount=%" PRIuIN ", command=%x",
          sector, sectorCount, transaction->Command);

    registerH2D->Type    = LOBYTE(FISRegisterH2D);
    registerH2D->Flags  |= FIS_REGISTER_H2D_FLAG_COMMAND;
    registerH2D->Command = LOBYTE(transaction->Command);
    registerH2D->Device  = FIS_REGISTER_H2D_DEVICE_LBAMODE | ((LOBYTE(deviceLun) & 0x1) << 4);
    registerH2D->Count   = (uint16_t)sectorCount;
    
    // Handle LBA to CHS translation if disk uses
    // the CHS scheme
    if (addressingMode == AHCI_DEVICE_MODE_CHS) {
        //uint16_t Head = 0, Cylinder = 0, Sector = 0;

        // Step 1 -> Transform LBA into CHS

        // Set CHS params
    }
    else if (addressingMode == AHCI_DEVICE_MODE_LBA28 ||
             addressingMode == AHCI_DEVICE_MODE_LBA48) {
        // Set LBA 28 parameters
        registerH2D->SectorNo         = LOBYTE(sector);
        registerH2D->CylinderLow      = (uint8_t)((sector >> 8) & 0xFF);
        registerH2D->CylinderHigh     = (uint8_t)((sector >> 16) & 0xFF);
        registerH2D->SectorNoExtended = (uint8_t)((sector >> 24) & 0xFF);

        // If it's an LBA48, set LBA48 params as well
        if (addressingMode == AHCI_DEVICE_MODE_LBA48) {
            registerH2D->CylinderLowExtended  = (uint8_t)((sector >> 32) & 0xFF);
            registerH2D->CylinderHighExtended = (uint8_t)((sector >> 40) & 0xFF);
        }
    }
}

oserr_t
AhciDispatchRegisterFIS(
    _In_ AhciController_t*  controller,
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction)
{
    FISRegisterH2D_t fis = { 0 };
    unsigned int     flags;
    int              bytesQueued;
    int              commandSlot = __GetTransferKey(transaction);

    TRACE("AhciDispatchRegisterFIS(controller=0x%" PRIxIN ", port=0x%" PRIxIN, ", transaction=0x%" PRIxIN ")",
          controller, port, transaction);
    
    // Initialize the command
    bytesQueued = __PrepareCommandSlot(port, transaction, transaction->Target.SectorSize, commandSlot);
    __FillRegisterFIS(transaction, &fis, bytesQueued,
                      transaction->Target.SectorSize,
                      transaction->Target.AddressingMode);
    
    // Start out by building dispatcher flags here
    flags = DISPATCH_MULTIPLIER(transaction->Target.PortMultiplier);
    
    if (transaction->Target.Type == DeviceATAPI) {
        flags |= DISPATCH_ATAPI;
    }

    if (transaction->Direction == AHCI_XACTION_OUT) {
        flags |= DISPATCH_WRITE;
    }

    // Prefetch can only be enabled when PMP == 0 and !NQC. Also cannot be enabled when
    // FIS-based switching is used
    if (!transaction->Target.PortMultiplier && !__CommandIsNQC(transaction->Command)) {
        flags |= DISPATCH_PREFETCH;
    }

    return __DispatchCommand(controller, port, transaction, flags,
                             &fis, sizeof(FISRegisterH2D_t),
                             NULL, 0);
}
