/* MollenOS
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
#include <ds/collection.h>
#include "manager.h"
#include "dispatch.h"
#include <threads.h>
#include <stdlib.h>
#include <assert.h>

AhciPort_t*
AhciPortCreate(
    _In_ AhciController_t*  Controller, 
    _In_ int                Port, 
    _In_ int                Index)
{
    struct dma_buffer_info DmaInfo;
    AhciPort_t*            AhciPort;

    // Sanitize the port, don't create an already existing
    // and make sure port is valid
    if (Controller->Ports[Port] != NULL || Port >= AHCI_MAX_PORTS) {
        if (Controller->Ports[Port] != NULL) {
            return Controller->Ports[Port];
        }
        return NULL;
    }

    AhciPort = (AhciPort_t*)malloc(sizeof(AhciPort_t));
    if (!AhciPort) {
        return NULL;
    }
    
    memset(AhciPort, 0, sizeof(AhciPort_t));
    AhciPort->Id        = Port;     // Sequential port number
    AhciPort->Index     = Index;    // Index in validity map
    AhciPort->SlotCount = AHCI_CAPABILITIES_NCS(Controller->Registers->Capabilities);

    // Allocate a transfer buffer for internal transactions
    DmaInfo.length   = AhciManagerGetFrameSize();
    DmaInfo.capacity = AhciManagerGetFrameSize();
    DmaInfo.flags    = 0;
    dma_create(&DmaInfo, &AhciPort->InternalBuffer);
    
    // TODO: port nr or bit index? Right now use the Index in the validity map
    AhciPort->Registers    = (AHCIPortRegisters_t*)((uintptr_t)Controller->Registers + AHCI_REGISTER_PORTBASE(Index));
    AhciPort->Transactions = CollectionCreate(KeyInteger);
    return AhciPort;
}

void
AhciPortCleanup(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    CollectionItem_t* Node;

    // Null out the port-entry in the controller
    Controller->Ports[Port->Index] = NULL;

    // Go through each transaction for the ports and clean up
    Node = CollectionPopFront(Port->Transactions);
    while (Node) {
        AhciManagerCancelTransaction((AhciTransaction_t*)Node);
        Node = CollectionPopFront(Port->Transactions);
    }
    CollectionDestroy(Port->Transactions);
    AhciManagerUnregisterDevice(Controller, Port);
    
    // Destroy the internal transfer buffer
    dma_attachment_unmap(&Port->InternalBuffer);
    dma_detach(&Port->InternalBuffer);
    free(Port);
}

void
AhciPortInitiateSetup(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    reg32_t Status = ReadVolatile32(&Port->Registers->CommandAndStatus);
    
    // Make sure the port has stopped
    WriteVolatile32(&Port->Registers->InterruptEnable, 0);
    if (Status & (AHCI_PORT_ST | AHCI_PORT_FRE | AHCI_PORT_CR | AHCI_PORT_FR)) {
        WriteVolatile32(&Port->Registers->CommandAndStatus, Status & ~AHCI_PORT_ST);
    }
}

OsStatus_t
AhciPortFinishSetup(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    reg32_t Status;
    int     Hung = 0;

    // Step 1 -> wait for the command engine to stop by waiting for AHCI_PORT_CR to clear
    WaitForConditionWithFault(Hung, (ReadVolatile32(&Port->Registers->CommandAndStatus) & AHCI_PORT_CR) == 0, 6, 100);
    if (Hung) {
        ERROR(" > failed to stop command engine: 0x%x", Port->Registers->CommandAndStatus);
        return OsError;
    }

    // Step 2 -> wait for the fis receive engine to stop by waiting for AHCI_PORT_FR to clear
    Status = ReadVolatile32(&Port->Registers->CommandAndStatus);
    WriteVolatile32(&Port->Registers->CommandAndStatus, Status & ~AHCI_PORT_FRE);
    WaitForConditionWithFault(Hung, (ReadVolatile32(&Port->Registers->CommandAndStatus) & AHCI_PORT_FR) == 0, 6, 100);
    if (Hung) {
        ERROR(" > failed to stop fis receive engine: 0x%x", Port->Registers->CommandAndStatus);
        return OsError;
    }

    // Step 3.1 -> If 1 or 2 fails, then we proceed to a reset of the port.
    // Step 3.2 -> If 1 and 2 succeeds, then skip reset and proceed to rebase of port.

    // Step 4 -> Rebase
    // Step 5 -> Initialize registers
    // Step 6 -> DET initialization
    // Step 7 -> FRE initialization
    // Step 8 -> Wait for BSYDRQ to clear
    // Step 9 -> ST initialization

    // Software causes a port reset (COMRESET) by writing 1h to the PxSCTL.DET
    // Also disable slumber and partial state
    WriteVolatile32(&Port->Registers->AtaControl, 
        AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE | 
        AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE | 
        AHCI_PORT_SCTL_RESET);
    thrd_sleepex(50);

    // After clearing PxSCTL.DET to 0h, software should wait for 
    // communication to be re-established as indicated by PxSSTS.DET being set to 3h.
    WriteVolatile32(&Port->Registers->AtaControl, 
        AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE | 
        AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE);
    WaitForConditionWithFault(Hung, 
        AHCI_PORT_STSS_DET(ReadVolatile32(&Port->Registers->AtaStatus)) == AHCI_PORT_SSTS_DET_ENABLED, 5, 10);
    Status = ReadVolatile32(&Port->Registers->AtaStatus);
    if (Hung && Status != 0) {
        // When PxSCTL.DET is set to 1h, the HBA shall reset PxTFD.STS to 7Fh and 
        // shall reset PxSSTS.DET to 0h. When PxSCTL.DET is set to 0h, upon receiving a 
        // COMINIT from the attached device, PxTFD.STS.BSY shall be set to 1 by the HBA.
        ERROR(" > failed to re-establish communication: 0x%x", Port->Registers->AtaStatus);
        if (AHCI_PORT_STSS_DET(Status) == AHCI_PORT_SSTS_DET_NOPHYCOM) {
            return OsTimeout;
        }
        return OsError;
    }

    if (AHCI_PORT_STSS_DET(Status) == AHCI_PORT_SSTS_DET_ENABLED) {
        // Handle staggered spin up support
        Status = ReadVolatile32(&Controller->Registers->Capabilities);
        if (Status & AHCI_CAPABILITIES_SSS) {
            Status = ReadVolatile32(&Port->Registers->CommandAndStatus);
            Status |= AHCI_PORT_SUD | AHCI_PORT_POD | AHCI_PORT_ICC_ACTIVE;
            WriteVolatile32(&Port->Registers->CommandAndStatus, Status);
        }
    }

    // Then software should write all 1s to the PxSERR register to clear 
    // any bits that were set as part of the port reset.
    WriteVolatile32(&Port->Registers->AtaError, 0xFFFFFFFF);
    WriteVolatile32(&Port->Registers->InterruptStatus, 0xFFFFFFFF);
    return OsSuccess;
}

static OsStatus_t
AllocateOperationalMemory(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    struct dma_buffer_info DmaInfo;
    OsStatus_t             Status;
    
    TRACE("AllocateOperationalMemory()");

    // Allocate some shared resources. The resource we need is 
    // 1K for the Command List per port
    // A Command table for each command header (32) per port
    DmaInfo.length   = sizeof(AHCICommandList_t);
    DmaInfo.capacity = sizeof(AHCICommandList_t);
    DmaInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    
    Status = dma_create(&DmaInfo, &Port->CommandListDMA);
    if (Status != OsSuccess) {
        ERROR("AHCI::Failed to allocate memory for the command list.");
        return OsOutOfMemory;
    }
    
    // Allocate memory for the 32 command tables, one for each command header.
    // 32 Command headers = 4K memory, but command headers must be followed by
    // 1..63365 prdt entries. 
    DmaInfo.length   = AHCI_COMMAND_TABLE_SIZE * 32;
    DmaInfo.capacity = AHCI_COMMAND_TABLE_SIZE * 32;
    DmaInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    
    Status = dma_create(&DmaInfo, &Port->CommandTableDMA);
    if (Status != OsSuccess) {
        ERROR("AHCI::Failed to allocate memory for the command table.");
        return OsOutOfMemory;
    }
    
    // We have to take into account FIS based switching here, 
    // if it's supported we need 4K per port, otherwise 256 bytes. 
    // But don't allcoate anything below 4K anyway
    DmaInfo.length   = 0x1000;
    DmaInfo.capacity = 0x1000;
    DmaInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    
    Status = dma_create(&DmaInfo, &Port->RecievedFisDMA);
    if (Status != OsSuccess) {
        ERROR("AHCI::Failed to allocate memory for the command table.");
        return OsOutOfMemory;
    }
    return OsSuccess;
}

OsStatus_t
AhciPortRebase(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    reg32_t             Caps = ReadVolatile32(&Controller->Registers->Capabilities);
    AHCICommandList_t*  CommandList;
    OsStatus_t          Status;
    struct dma_sg_table SgTable;
    uintptr_t           PhysicalAddress;
    int                 i;
    int                 j;

    Status = AllocateOperationalMemory(Controller, Port);
    if (Status != OsSuccess) {
        return Status;
    }
    
    (void)dma_get_sg_table(&Port->CommandTableDMA, &SgTable, -1);

    // Iterate the 32 command headers
    CommandList     = (AHCICommandList_t*)Port->CommandListDMA.buffer;
    PhysicalAddress = SgTable.entries[0].address;
    for (i = 0, j = 0; i < 32; i++) {
        // Load the command table address (physical)
        CommandList->Headers[i].CmdTableBaseAddress = LODWORD(PhysicalAddress);

        // Set command table address upper register if supported and we are in 64 bit
        if (Caps & AHCI_CAPABILITIES_S64A) {
            CommandList->Headers[i].CmdTableBaseAddressUpper = 
                (sizeof(void*) > 4) ? HIDWORD(PhysicalAddress) : 0;
        }

        PhysicalAddress           += AHCI_COMMAND_TABLE_SIZE;
        SgTable.entries[j].length -= AHCI_COMMAND_TABLE_SIZE;
        if (!SgTable.entries[j].length) {
            j++;
            PhysicalAddress = SgTable.entries[j].address;
        }
    }

    free(SgTable.entries);
    return OsSuccess;
}

OsStatus_t
AhciPortEnable(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    reg32_t Status;
    int     Hung = 0;

    // Set FRE before ST
    Status = ReadVolatile32(&Port->Registers->CommandAndStatus);
    WriteVolatile32(&Port->Registers->CommandAndStatus, Status | AHCI_PORT_FRE);
    WaitForConditionWithFault(Hung, ReadVolatile32(&Port->Registers->CommandAndStatus) & AHCI_PORT_FR, 6, 100);
    if (Hung) {
        ERROR(" > fis receive engine failed to start: 0x%x", Port->Registers->CommandAndStatus);
        return OsTimeout;
    }

    // Wait for BSYDRQ to clear
    if (ReadVolatile32(&Port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) {
        WriteVolatile32(&Port->Registers->AtaError, (1 << 26)); // Exchanged bit.
        WaitForConditionWithFault(Hung, (ReadVolatile32(&Port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0, 30, 100);
        if (Hung) {
            ERROR(" > failed to clear BSY and DRQ: 0x%x", Port->Registers->TaskFileData);
            return OsTimeout;
        }
    }

    // Finally start
    Status = ReadVolatile32(&Port->Registers->CommandAndStatus);
    WriteVolatile32(&Port->Registers->CommandAndStatus, Status | AHCI_PORT_ST);
    WaitForConditionWithFault(Hung, ReadVolatile32(&Port->Registers->CommandAndStatus) & AHCI_PORT_CR, 6, 100);
    if (Hung) {
        ERROR(" > command engine failed to start: 0x%x", Port->Registers->CommandAndStatus);
        return OsTimeout;
    }
    
    Port->Connected = 1;
    return AhciManagerRegisterDevice(Controller, Port, Port->Registers->Signature);
}

OsStatus_t
AhciPortStart(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    struct dma_sg_table DmaTable;
    reg32_t       Caps       = ReadVolatile32(&Controller->Registers->Capabilities);
    int           Hung       = 0;

    // Setup the physical data addresses
    (void)dma_get_metrics(&Port->RecievedFisDMA, &DmaSgCount, &DmaSg);
    WriteVolatile32(&Port->Registers->FISBaseAddress, LOWORD(DmaSg.address));
    if (Caps & AHCI_CAPABILITIES_S64A) {
        WriteVolatile32(&Port->Registers->FISBaseAdressUpper,
            (sizeof(void*) > 4) ? HIDWORD(DmaSg.address) : 0);
    }
    else {
        WriteVolatile32(&Port->Registers->FISBaseAdressUpper, 0);
    }

    (void)dma_get_metrics(&Port->CommandListDMA, &DmaSgCount, &DmaSg);
    WriteVolatile32(&Port->Registers->CmdListBaseAddress, LODWORD(DmaSg.address));
    if (Caps & AHCI_CAPABILITIES_S64A) {
        WriteVolatile32(&Port->Registers->CmdListBaseAddressUpper,
            (sizeof(void*) > 4) ? HIDWORD(DmaSg.address) : 0);
    }
    else {
        WriteVolatile32(&Port->Registers->CmdListBaseAddressUpper, 0);
    }

    // Setup the interesting interrupts we want
    WriteVolatile32(&Port->Registers->InterruptEnable, (reg32_t)(AHCI_PORT_IE_CPDE | AHCI_PORT_IE_TFEE
        | AHCI_PORT_IE_PCE | AHCI_PORT_IE_DSE | AHCI_PORT_IE_PSE | AHCI_PORT_IE_DHRE));

    // Make sure AHCI_PORT_CR and AHCI_PORT_FR is not set
    WaitForConditionWithFault(Hung, (
        ReadVolatile32(&Port->Registers->CommandAndStatus) & 
            (AHCI_PORT_CR | AHCI_PORT_FR)) == 0, 10, 25);
    if (Hung) {
        // In this case we should reset the entire controller @todo
        ERROR(" > command engine is hung: 0x%x", Port->Registers->CommandAndStatus);
        return OsTimeout;
    }
    
    // Wait for FRE and BSYDRQ. This assumes a device present
    if (AHCI_PORT_STSS_DET(ReadVolatile32(&Port->Registers->AtaStatus)) != AHCI_PORT_SSTS_DET_ENABLED) {
        WARNING(" > port has nothing present: 0x%x", Port->Registers->AtaStatus);
        return OsSuccess;
    }
    return AhciPortEnable(Controller, Port);
}

void
AhciPortStartCommandSlot(
    _In_ AhciPort_t* Port, 
    _In_ int         Slot)
{
    WriteVolatile32(&Port->Registers->CommandIssue, (1 << Slot));
}

OsStatus_t
AhciPortAllocateCommandSlot(
    _In_  AhciPort_t* Port,
    _Out_ int*        SlotOut)
{
    OsStatus_t Status = OsError;
    int        Slots;
    int        i;
    
    while (Status != OsSuccess) {
        Slots = atomic_load(&Port->Slots);
        
        for (i = 0; i < Port->SlotCount; i++) {
            // Check availability status on this command slot
            if (Slots & (1 << i)) {
                continue;
            }

            if (atomic_compare_exchange_strong(&Port->Slots, &Slots, Slots | (1 << i))) {
                Status   = OsSuccess;
                *SlotOut = i;
            }
            break;
        }
    }
    return Status;
}

void
AhciPortFreeCommandSlot(
    _In_ AhciPort_t* Port,
    _In_ int         Slot)
{
    
}

void
AhciPortInterruptHandler(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    AhciTransaction_t* Transaction;
    reg32_t            InterruptStatus;
    reg32_t            DoneCommands;
    DataKey_t          Key;
    int                i;
    
    // Check interrupt services 
    // Cold port detect, recieved fis etc
    TRACE("AhciPortInterruptHandler(Port %i, Interrupt Status 0x%x)",
        Port->Id, Controller->InterruptResource.PortInterruptStatus[Port->Index]);

HandleInterrupt:
    InterruptStatus = Controller->InterruptResource.PortInterruptStatus[Port->Index];
    Controller->InterruptResource.PortInterruptStatus[Port->Index] = 0;
    
    // Check for errors status's
    if (InterruptStatus & (AHCI_PORT_IE_TFEE | AHCI_PORT_IE_HBFE 
        | AHCI_PORT_IE_HBDE | AHCI_PORT_IE_IFE | AHCI_PORT_IE_INFE)) {
        if (InterruptStatus & AHCI_PORT_IE_TFEE) {
            PrintTaskDataErrorString(HIBYTE(Port->Registers->TaskFileData));
        }
        else {
            ERROR("AHCI::Port ERROR %i, CMD: 0x%x, CI 0x%x, IE: 0x%x, IS 0x%x, TFD: 0x%x", Port->Id,
                Port->Registers->CommandAndStatus, Port->Registers->CommandIssue,
                Port->Registers->InterruptEnable, InterruptStatus, Port->Registers->TaskFileData);
        }
    }

    // Check for hot-plugs
    if (InterruptStatus & AHCI_PORT_IE_PCE) {
        // Determine whether or not there is a device connected
        // Detect present ports using
        // PxTFD.STS.BSY = 0, PxTFD.STS.DRQ = 0, and PxSSTS.DET = 3
        reg32_t TFD    = ReadVolatile32(&Port->Registers->TaskFileData);
        reg32_t Status = ReadVolatile32(&Port->Registers->AtaStatus);
        if (TFD & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)
            || (AHCI_PORT_STSS_DET(Status) != AHCI_PORT_SSTS_DET_ENABLED)) {
            AhciManagerUnregisterDevice(Controller, Port);
            Port->Connected = 0;
        }
        else {
            Port->Connected = 1;
            AhciManagerRegisterDevice(Controller, Port, Port->Registers->Signature);
        }
    }

    // Get completed commands, by using our own slot-status
    DoneCommands = atomic_load(&Port->Slots) ^ ReadVolatile32(&Port->Registers->AtaActive);
    TRACE("DoneCommands(0x%x) <= SlotStatus(0x%x) ^ AtaActive(0x%x)", 
        DoneCommands, atomic_load(&Port->Slots), Port->Registers->AtaActive);

    // Check for command completion
    // by iterating through the command slots
    if (DoneCommands != 0) {
        for (i = 0; i < AHCI_MAX_PORTS; i++) {
            if (DoneCommands & (1 << i)) {
                Key.Value.Integer = i;
                Transaction       = (AhciTransaction_t*)CollectionGetNodeByKey(Port->Transactions, Key, 0);                
                assert(Transaction != NULL);

                // Handle transaction completion, release slot, queue up a new command if any
                // and then handle the event
                CollectionRemoveByNode(Port->Transactions, &Transaction->Header);
                memcpy((void*)&Transaction->Response, Port->RecievedFisDMA.buffer, sizeof(AHCIFis_t));
                AhciPortFreeCommandSlot(Port, Transaction->Slot);
                Transaction->Slot = -1;

                AhciTransactionHandleResponse(Controller, Port, Transaction);
            }
        }
    }

    // Re-handle?
    if (Controller->InterruptResource.PortInterruptStatus[Port->Index] != 0) {
        goto HandleInterrupt;
    }
}
