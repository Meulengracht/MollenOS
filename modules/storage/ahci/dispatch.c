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

static oserr_t
__BuildPRDTTable(
    _In_ AhciTransaction_t*  transaction,
    _In_ AHCICommandTable_t* table,
    _In_ size_t              sectorSize,
    _Out_ int*               countOut)
{
    const struct DMASegment* segments = transaction->Dma.Segments;
    uint32_t                 index = transaction->SgIndex;
    size_t                   offset = transaction->SgOffset;
    size_t                   queued = 0;
    size_t                   length;
    size_t                   trim;
    size_t                   remove;
    uint64_t                 address;
    int                      count = 0;

    // Fill one command table from the mapping's device addresses.
    // Keep the mapping for any remaining ATA commands.
    while (count < AHCI_COMMAND_TABLE_PRDT_COUNT && queued < transaction->BytesLeft) {
        if (index >= transaction->Dma.SegmentCount || offset >= segments[index].Length) {
            return OS_EINVALPARAMS;
        }

        address = segments[index].Address + offset;
        length = MIN(
            AHCI_PRDT_MAX_LENGTH,
            MIN(
                transaction->BytesLeft - queued,
                segments[index].Length - offset
            )
        );
        if ((address & 1) != 0 || (length & 1) != 0) {
            return OS_EINVALPARAMS;
        }
        
        table->PrdtEntry[count].DataBaseAddress = (uint32_t)address;
        table->PrdtEntry[count].DataBaseAddressUpper = (uint32_t)(address >> 32);
        table->PrdtEntry[count].Reserved = 0;
        table->PrdtEntry[count].Descriptor = (uint32_t)(length - 1);
        
        count++;
        queued += length;
        offset += length;
        
        if (offset == segments[index].Length) {
            offset = 0;
            index++;
        }
    }

    if (count == 0) {
        return OS_EINVALPARAMS;
    }

    // A split ATA command must end on a sector boundary. Small final segments
    // can require trimming more than one PRD. Only this split case walks back.
    if (queued < transaction->BytesLeft && transaction->SectorAlignment) {
        trim = queued % sectorSize;
        queued -= trim;
        if (queued == 0) {
            return OS_ENOTSUPPORTED;
        }

        remove = trim;
        while (remove != 0) {
            length = (size_t)table->PrdtEntry[count - 1].Descriptor + 1;
            if (remove >= length) {
                remove -= length;
                count--;
            } else {
                table->PrdtEntry[count - 1].Descriptor -= (uint32_t)remove;
                remove = 0;
            }
        }
        
        while (trim != 0) {
            if (offset == 0) {
                index--;
                offset = (size_t)segments[index].Length;
            }
            remove = MIN(trim, offset);
            offset -= remove;
            trim -= remove;
        }
    }

    table->PrdtEntry[count - 1].Descriptor |= AHCI_PRDT_IOC;
    
    transaction->SgIndex = index;
    transaction->SgOffset = offset;
    transaction->BytesLeft -= queued;
    transaction->CommandBytes = queued;
    
    *countOut = count;
    return OS_EOK;
}

static oserr_t
__PrepareCommandSlot(
    _In_ AhciPort_t*        port,
    _In_ AhciTransaction_t* transaction,
    _In_ size_t             sectorSize,
    _In_ int                slot)
{
    AHCICommandList_t*   list = DMABufferData(port->Dma.CommandList);
    AHCICommandTable_t*  table;
    AHCICommandHeader_t* header = &list->Headers[slot];
    oserr_t              status;
    int                  count;

    // Each header points to its own table. Sharing slot zero's table would
    // overwrite commands that the controller is still fetching for other slots.
    table = (AHCICommandTable_t*)((uint8_t*)DMABufferData(port->Dma.CommandTables) +
        slot * AHCI_COMMAND_TABLE_SIZE);
    
    status = __BuildPRDTTable(transaction, table, sectorSize, &count);
    if (status != OS_EOK) {
        return status;
    }

    header->PRDByteCount = 0;
    header->TableLength = (uint16_t)count;
    return OS_EOK;
}

static oserr_t
__DispatchCommand(
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
    oserr_t              status;

    TRACE("__DispatchCommand(transaction=0x%" PRIxIN ", flags=0x%x, ataCommandLength=0x%" PRIuIN ")",
          transaction, flags, ataCommandLength);

#ifdef __TRACE
    __DumpCurrentState(controller, port);
#endif

    if (ataCommandLength > 64 || atapiCommandLength > 16) {
        ERROR("__DispatchCommand Commands are exceeding the allowed length, FIS (%u), ATAPI (%u)",
              commandLength, atapiCommandLength);
        return OS_EINVALPARAMS;
    }

    commandList   = (AHCICommandList_t*)DMABufferData(port->Dma.CommandList);
    commandTable  = (AHCICommandTable_t*)((uint8_t*)DMABufferData(port->Dma.CommandTables) +
        commandSlot * AHCI_COMMAND_TABLE_SIZE);
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
    
    // Begin DMA once for the entire request, including split commands.
    // Between commands, the CPU only updates the persistent command tables.
    if (transaction->Dma.Mapping != NULL && !transaction->Dma.DeviceOwned) {
        status = DMABeginTransfer(
            transaction->Dma.Mapping,
            0,
            transaction->Dma.Length,
            SHMWaitNonBlocking,
            NULL
        );
        if (status != OS_EOK) {
            return status;
        }
        transaction->Dma.DeviceOwned = true;
    }
    
    transaction->State = TransactionInProgress;
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
    _In_ size_t             bytesQueued,
    _In_ size_t             sectorSize,
    _In_ int                addressingMode)
{
    uint64_t sector      = transaction->Sector + transaction->SectorsTransferred;
    size_t   sectorCount = bytesQueued / sectorSize;
    int      deviceLun   = 0; // TODO: Is this LUN or what is it?

    TRACE("__FillRegisterFIS(bytesQueued=%zu, sectorSize=0x%" PRIxIN ", addressingMode=%i)",
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
    oserr_t          status;
    int              commandSlot = __GetTransferKey(transaction);

    TRACE("AhciDispatchRegisterFIS(controller=0x%" PRIxIN ", port=0x%" PRIxIN ", transaction=0x%" PRIxIN ")",
          controller, port, transaction);
    
    // Initialize the command
    status = __PrepareCommandSlot(port, transaction, transaction->Target.SectorSize, commandSlot);
    if (status != OS_EOK) {
        return status;
    }
    
    __FillRegisterFIS(
        transaction,
        &fis,
        transaction->CommandBytes,
        transaction->Target.SectorSize,
        transaction->Target.AddressingMode
    );
    
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

    return __DispatchCommand(
        controller,
        port,
        transaction,
        flags,
        &fis,
        sizeof(FISRegisterH2D_t),
        NULL,
        0
    );
}
