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

#include <assert.h>
#include <os/mollenos.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include "manager.h"
#include "dispatch.h"
#include <stdlib.h>
#include <string.h>
#include <threads.h>

AhciPort_t*
AhciPortCreate(
    _In_ AhciController_t*  controller,
    _In_ int                portIndex,
    _In_ int                mapIndex)
{
    struct dma_buffer_info bufferInfo;
    AhciPort_t*            port;
    TRACE("AhciPortCreate(controller=0x%" PRIxIN ", portIndex=%i, mapIndex=%i)",
          controller, portIndex, mapIndex);

    // Sanitize the port, don't create an already existing
    // and make sure port is valid
    if (controller->Ports[portIndex] != NULL || portIndex >= AHCI_MAX_PORTS) {
        if (controller->Ports[portIndex] != NULL) {
            return controller->Ports[portIndex];
        }
        return NULL;
    }

    port = (AhciPort_t*)malloc(sizeof(AhciPort_t));
    if (!port) {
        return NULL;
    }
    
    memset(port, 0, sizeof(AhciPort_t));
    list_construct(&port->Transactions);

    port->Id        = portIndex;     // Sequential port number
    port->Index     = mapIndex;    // Index in validity map
    port->SlotCount = AHCI_CAPABILITIES_NCS(controller->Registers->Capabilities);

    // Allocate a transfer buffer for internal transactions
    bufferInfo.length   = AhciManagerGetFrameSize();
    bufferInfo.capacity = AhciManagerGetFrameSize();
    bufferInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;
    dma_create(&bufferInfo, &port->InternalBuffer);
    
    // TODO: port nr or bit index? Right now use the Index in the validity map
    port->Registers = (AHCIPortRegisters_t*)((uintptr_t)controller->Registers + AHCI_REGISTER_PORTBASE(mapIndex));
    return port;
}

static void __CancelTransactionCallback(
    _In_ element_t* element,
    _In_ void*      context)
{
    AhciManagerCancelTransaction((AhciTransaction_t*)element->value);
}

void
AhciPortCleanup(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    // Null out the port-entry in the controller
    Controller->Ports[Port->Index] = NULL;

    // Go through each transaction for the ports and clean up
    list_clear(&Port->Transactions, __CancelTransactionCallback, NULL);
    AhciManagerUnregisterDevice(Controller, Port);
    
    // Destroy the internal transfer buffer
    dma_attachment_unmap(&Port->InternalBuffer);
    dma_detach(&Port->InternalBuffer);
    free(Port);
}

void
AhciPortInitiateSetup(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t status;
    TRACE("AhciPortInitiateSetup(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    TRACE("AhciPortInitiateSetup CommandAndStatus=0x%x", status);

    // Make sure the port has stopped
    WRITE_VOLATILE(port->Registers->InterruptEnable, 0);
    if (status & (AHCI_PORT_ST | AHCI_PORT_FRE | AHCI_PORT_CR | AHCI_PORT_FR)) {
        WRITE_VOLATILE(port->Registers->CommandAndStatus, status & ~AHCI_PORT_ST);
    }
}

oscode_t
AhciPortFinishSetup(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t status;
    int     hung;
    TRACE("AhciPortFinishSetup(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    // Step 1 -> wait for the command engine to stop by waiting for AHCI_PORT_CR to clear
    WaitForConditionWithFault(hung, (READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_CR) == 0, 6, 100);
    if (hung) {
        ERROR("AhciPortFinishSetup failed to stop command engine: 0x%x", port->Registers->CommandAndStatus);
        return OsError;
    }

    // Step 2 -> wait for the fis receive engine to stop by waiting for AHCI_PORT_FR to clear
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    WRITE_VOLATILE(port->Registers->CommandAndStatus, status & ~AHCI_PORT_FRE);
    WaitForConditionWithFault(hung, (READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_FR) == 0, 6, 100);
    if (hung) {
        ERROR("AhciPortFinishSetup failed to stop fis receive engine: 0x%x", port->Registers->CommandAndStatus);
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
    WRITE_VOLATILE(port->Registers->SCTL,
        AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE |
        AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE |
        AHCI_PORT_SCTL_RESET);
    thrd_sleepex(50);

    // After clearing PxSCTL.DET to 0h, software should wait for 
    // communication to be re-established as indicated by PxSSTS.DET being set to 3h.
    WRITE_VOLATILE(port->Registers->SCTL,
        AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE | 
        AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE);
    WaitForConditionWithFault(hung,
                              AHCI_PORT_STSS_DET(READ_VOLATILE(port->Registers->STSS)) == AHCI_PORT_SSTS_DET_ENABLED,
                              5, 10);
    status = READ_VOLATILE(port->Registers->STSS);
    if (hung && status != 0) {
        // When PxSCTL.DET is set to 1h, the HBA shall reset PxTFD.STS to 7Fh and 
        // shall reset PxSSTS.DET to 0h. When PxSCTL.DET is set to 0h, upon receiving a 
        // COMINIT from the attached device, PxTFD.STS.BSY shall be set to 1 by the HBA.
        ERROR("AhciPortFinishSetup failed to re-establish communication: 0x%x", port->Registers->STSS);
        if (AHCI_PORT_STSS_DET(status) == AHCI_PORT_SSTS_DET_NOPHYCOM) {
            return OsTimeout;
        }
        return OsError;
    }
    TRACE("AhciPortFinishSetup AtaStatus=0x%x", status);

    if (AHCI_PORT_STSS_DET(status) == AHCI_PORT_SSTS_DET_ENABLED) {
        // Handle staggered spin up support
        status = READ_VOLATILE(controller->Registers->Capabilities);
        TRACE("AhciPortFinishSetup Capabilities=0x%x", status);

        if (status & AHCI_CAPABILITIES_SSS) {
            status = READ_VOLATILE(port->Registers->CommandAndStatus);
            status |= AHCI_PORT_SUD | AHCI_PORT_POD | AHCI_PORT_ICC_ACTIVE;
            WRITE_VOLATILE(port->Registers->CommandAndStatus, status);
        }
    }

    // Then software should write all 1s to the PxSERR register to clear 
    // any bits that were set as part of the port reset.
    WRITE_VOLATILE(port->Registers->SERR, 0xFFFFFFFF);
    WRITE_VOLATILE(port->Registers->InterruptStatus, 0xFFFFFFFF);
    return OsOK;
}

static oscode_t
AllocateOperationalMemory(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    struct dma_buffer_info bufferInfo;
    oscode_t             osStatus;

    TRACE("AllocateOperationalMemory(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    // Allocate some shared resources. The resource we need is 
    // 1K for the Command List per port
    // A Command table for each command header (32) per port
    bufferInfo.length   = sizeof(AHCICommandList_t);
    bufferInfo.capacity = sizeof(AHCICommandList_t);
    bufferInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;
    bufferInfo.type     = DMA_TYPE_DRIVER_32;

    osStatus = dma_create(&bufferInfo, &port->CommandListDMA);
    if (osStatus != OsOK) {
        ERROR("AllocateOperationalMemory failed to allocate memory for the command list.");
        return OsOutOfMemory;
    }
    
    // Allocate memory for the 32 command tables, one for each command header.
    // 32 Command headers = 4K memory, but command headers must be followed by
    // 1..63365 prdt entries. 
    bufferInfo.length   = AHCI_COMMAND_TABLE_SIZE * 32;
    bufferInfo.capacity = AHCI_COMMAND_TABLE_SIZE * 32;
    bufferInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;

    osStatus = dma_create(&bufferInfo, &port->CommandTableDMA);
    if (osStatus != OsOK) {
        ERROR("AllocateOperationalMemory failed to allocate memory for the command table.");
        return OsOutOfMemory;
    }
    
    // We have to take into account FIS based switching here, 
    // if it's supported we need 4K per port, otherwise 256 bytes. 
    // But don't allcoate anything below 4K anyway
    bufferInfo.length   = 0x1000;
    bufferInfo.capacity = 0x1000;
    bufferInfo.flags    = DMA_UNCACHEABLE | DMA_CLEAN;

    osStatus = dma_create(&bufferInfo, &port->RecievedFisDMA);
    if (osStatus != OsOK) {
        ERROR("AllocateOperationalMemory failed to allocate memory for the command table.");
        return OsOutOfMemory;
    }
    return OsOK;
}

oscode_t
AhciPortRebase(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t             Caps = READ_VOLATILE(controller->Registers->Capabilities);
    AHCICommandList_t*  CommandList;
    oscode_t          Status;
    struct dma_sg_table SgTable;
    uintptr_t           PhysicalAddress;
    int                 i;
    int                 j;

    TRACE("AhciPortRebase(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    Status = AllocateOperationalMemory(controller, port);
    if (Status != OsOK) {
        return Status;
    }
    
    (void)dma_get_sg_table(&port->CommandTableDMA, &SgTable, -1);

    // Iterate the 32 command headers
    CommandList     = (AHCICommandList_t*)port->CommandListDMA.buffer;
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
    return OsOK;
}

oscode_t
AhciPortEnable(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t status;
    int     hung;

    TRACE("AhciPortEnable(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    // Set FRE before ST
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    TRACE("AhciPortEnable CommandAndStatus=0x%x", status);

    WRITE_VOLATILE(port->Registers->CommandAndStatus, status | AHCI_PORT_FRE);
    WaitForConditionWithFault(hung, READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_FR, 6, 100);
    if (hung) {
        ERROR("AhciPortEnable fis receive engine failed to start: 0x%x", port->Registers->CommandAndStatus);
        return OsTimeout;
    }

    // Wait for BSYDRQ to clear
    if (READ_VOLATILE(port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) {
        WRITE_VOLATILE(port->Registers->SERR, (1 << 26)); // Exchanged bit.
        WaitForConditionWithFault(hung, (READ_VOLATILE(port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0, 30, 100);
        if (hung) {
            ERROR("AhciPortEnable failed to clear BSY and DRQ: 0x%x", port->Registers->TaskFileData);
            return OsTimeout;
        }
    }

    // Finally start
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    TRACE("AhciPortEnable CommandAndStatus=0x%x", status);

    WRITE_VOLATILE(port->Registers->CommandAndStatus, status | AHCI_PORT_ST);
    WaitForConditionWithFault(hung, READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_CR, 6, 100);
    if (hung) {
        ERROR("AhciPortEnable command engine failed to start: 0x%x", port->Registers->CommandAndStatus);
        return OsTimeout;
    }

    port->Connected = 1;

    status = READ_VOLATILE(port->Registers->Signature);
    return AhciManagerRegisterDevice(controller, port, status);
}

oscode_t
AhciPortStart(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    struct dma_sg_table dmaTable;
    reg32_t             capabilities = READ_VOLATILE(controller->Registers->Capabilities);
    reg32_t             status;
    int                 hung;

    TRACE("AhciPortStart(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    // Setup the physical data addresses
    dma_get_sg_table(&port->RecievedFisDMA, &dmaTable, -1);

    TRACE("AhciPortStart FISBaseAddress=0x%" PRIxIN, dmaTable.entries[0].address);
    WRITE_VOLATILE(port->Registers->FISBaseAddress, LOWORD(dmaTable.entries[0].address));
    if (capabilities & AHCI_CAPABILITIES_S64A) {
        WRITE_VOLATILE(port->Registers->FISBaseAdressUpper,
                       (sizeof(void*) > 4) ? HIDWORD(dmaTable.entries[0].address) : 0);
    }
    else {
        WRITE_VOLATILE(port->Registers->FISBaseAdressUpper, 0);
    }
    free(dmaTable.entries);

    dma_get_sg_table(&port->CommandListDMA, &dmaTable, -1);

    TRACE("AhciPortStart CmdListBaseAddress=0x%" PRIxIN, dmaTable.entries[0].address);
    WRITE_VOLATILE(port->Registers->CmdListBaseAddress, LODWORD(dmaTable.entries[0].address));
    if (capabilities & AHCI_CAPABILITIES_S64A) {
        WRITE_VOLATILE(port->Registers->CmdListBaseAddressUpper,
                       (sizeof(void*) > 4) ? HIDWORD(dmaTable.entries[0].address) : 0);
    }
    else {
        WRITE_VOLATILE(port->Registers->CmdListBaseAddressUpper, 0);
    }
    free(dmaTable.entries);

    // Setup the interesting interrupts we want
    status = AHCI_PORT_IE_PCE |  // Port Change
            AHCI_PORT_IE_DHRE | // Device to Host Register FIS
            AHCI_PORT_IE_DSE |  // DMA Setup FIS
            AHCI_PORT_IE_PSE |  // PIO Setup FIS
            AHCI_PORT_IE_SDBE | // Set Device Bits FIS
            AHCI_PORT_IE_PRCE | // PhyRdy Change Interrupt
            AHCI_PORT_IE_IPME | // Incorrect Port Multiplier
            AHCI_PORT_IE_OFE |  // Overflow
            AHCI_PORT_IE_INFE | // Interface Non-fatal
            AHCI_PORT_IE_IFE |  // Interface Fatal
            AHCI_PORT_IE_HBDE | // Host Bus Data
            AHCI_PORT_IE_HBFE | // Host Bus Fatal
            AHCI_PORT_IE_TFEE | // Task File Error
            AHCI_PORT_IE_CPDE;  // Cold Presence Detect
    TRACE("AhciPortStart IE=0x%x", status);
    WRITE_VOLATILE(port->Registers->InterruptEnable, status);

    // Make sure AHCI_PORT_CR and AHCI_PORT_FR is not set
    WaitForConditionWithFault(hung, (
                                            READ_VOLATILE(port->Registers->CommandAndStatus) &
                                            (AHCI_PORT_CR | AHCI_PORT_FR)) == 0, 10, 25);
    if (hung) {
        // In this case we should reset the entire controller @todo
        ERROR("AhciPortStart command engine is hung: 0x%x", port->Registers->CommandAndStatus);
        return OsTimeout;
    }
    
    // Wait for FRE and BSYDRQ. This assumes a device present
    status = READ_VOLATILE(port->Registers->STSS);
    TRACE("AhciPortStart AtaStatus=0x%x", status);

    if (AHCI_PORT_STSS_DET(status) != AHCI_PORT_SSTS_DET_ENABLED) {
        TRACE("AhciPortStart port %i has nothing present: 0x%x", port->Index, port->Registers->STSS);
        return OsOK;
    }
    return AhciPortEnable(controller, port);
}

void
AhciPortStartCommandSlot(
    _In_ AhciPort_t* port,
    _In_ int         slot,
    _In_ int         nqc)
{
    reg32_t bitIndex = (1U << slot);
    if (nqc) {
        WRITE_VOLATILE(port->Registers->SACT, bitIndex);
    }
    WRITE_VOLATILE(port->Registers->CI, bitIndex);
}

oscode_t
AhciPortAllocateCommandSlot(
    _In_  AhciPort_t* port,
    _Out_ int*        slotOut)
{
    oscode_t osStatus = OsError;
    int        i;
    
    while (osStatus != OsOK) {
        int slots = atomic_load(&port->Slots);
        
        for (i = 0; i < port->SlotCount; i++) {
            int swapped;

            // Check availability status on this command slot
            if (slots & (1 << i)) {
                continue;
            }

            swapped = atomic_compare_exchange_strong(&port->Slots, &slots, slots | (1 << i));
            if (!swapped) {
                i--;
                continue;
            }

            osStatus = OsOK;
            *slotOut = i;
            break;
        }
    }
    return osStatus;
}

void
AhciPortFreeCommandSlot(
    _In_ AhciPort_t* port,
    _In_ int         slot)
{
    // simply just clear it
    atomic_fetch_xor(&port->Slots, (1 << slot));
}

static inline void __UpdateTransaction(
        _In_ AhciController_t* controller,
        _In_ AhciPort_t*       port,
        _In_ int               portSlot)
{
    AHCICommandList_t*   commandList;
    AHCICommandHeader_t* commandHeader;
    AhciTransaction_t*   transaction;
    size_t               bytesTransferred;

    transaction = (AhciTransaction_t*)list_find_value(&port->Transactions, (void*)(uintptr_t)portSlot);
    if (!transaction) {
        AhciPortFreeCommandSlot(port, portSlot);
        ERROR("__UpdateTransaction found no transaction with id %i", portSlot);
        return;
    }

    commandList   = (AHCICommandList_t*)port->CommandListDMA.buffer;
    commandHeader = &commandList->Headers[portSlot];

    bytesTransferred = commandHeader->PRDByteCount;

    // Handle transaction completion, release slot, queue up a new command if any
    // and then handle the event
    memcpy((void*)&transaction->Response, port->RecievedFisDMA.buffer, sizeof(AHCIFis_t));
    AhciTransactionHandleResponse(controller, port, transaction, bytesTransferred);
}

void
AhciPortInterruptHandler(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t interruptStatus = atomic_exchange(&controller->InterruptResource.PortInterruptStatus[port->Index], 0);
    reg32_t doneCommands;
    reg32_t sact;
    int     i;

    TRACE("AhciPortInterruptHandler(Port %i, Interrupt Status 0x%x)", port->Id, interruptStatus);

handler_loop:

    // Check for fatal errors first
    if (interruptStatus & AHCI_PORT_IE_HBFE) { // Host Bus Fatal Error
        ERROR("AhciPortInterruptHandler handle host bus fatal error");
        // Abort all transfers, reset AHCI
    }

    if (interruptStatus & AHCI_PORT_IE_IFE) { // Interface Fatal Error
        reg32_t  serr = READ_VOLATILE(port->Registers->SERR);
        uint32_t err  = AHCI_PORT_SERR_ERROR(serr);
        uint32_t diag = AHCI_PORT_SERR_DIAG(serr);
        ERROR("AhciPortInterruptHandler handle interface fatal error [%x:%x]", err, diag);
    }

    // If no fatal errors occurred, then we handle non-fatal errors
    // In all these cases the trasnfer must be aborted.
    if (interruptStatus & AHCI_PORT_IE_INFE) {
        reg32_t  serr = READ_VOLATILE(port->Registers->SERR);
        uint32_t err  = AHCI_PORT_SERR_ERROR(serr);
        uint32_t diag = AHCI_PORT_SERR_DIAG(serr);
        ERROR("AhciPortInterruptHandler handle interface non-fatal error [%x:%x]", err, diag);
    }

    if (interruptStatus & AHCI_PORT_IE_HBDE) { // Host Bus Data Error
        ERROR("AhciPortInterruptHandler handle host bus data error");
    }

    if (interruptStatus & AHCI_PORT_IE_IPME) { // Incorrect Port Multiplier
        // abort transaction as invalid
        ERROR("AhciPortInterruptHandler handle incorrect multiplier");
    }

    if (interruptStatus & AHCI_PORT_IE_OFE) { // Overflow
        // Finish transaction
        ERROR("AhciPortInterruptHandler handle overflow");
    }

    if (interruptStatus & AHCI_PORT_IE_TFEE) { // Task File Error
        reg32_t tfd = READ_VOLATILE(port->Registers->TaskFileData);
        PrintTaskDataErrorString(HIBYTE(tfd));
    }

    // Check for PhyRdy Change
    if (interruptStatus & AHCI_PORT_IE_PRCE) {
        ERROR("AhciPortInterruptHandler handle phyrdy change");
    }

    // Check for hot-plugs
    if (interruptStatus & AHCI_PORT_IE_PCE) {
        // Determine whether or not there is a device connected
        // Detect present ports using
        // PxTFD.STS.BSY = 0, PxTFD.STS.DRQ = 0, and PxSSTS.DET = 3
        reg32_t tfd    = READ_VOLATILE(port->Registers->TaskFileData);
        reg32_t status = READ_VOLATILE(port->Registers->STSS);
        if (tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ) || (AHCI_PORT_STSS_DET(status) != AHCI_PORT_SSTS_DET_ENABLED)) {
            AhciManagerUnregisterDevice(controller, port);
            port->Connected = 0;
        }
        else {
            port->Connected = 1;

            status = READ_VOLATILE(port->Registers->Signature);
            AhciManagerRegisterDevice(controller, port, status);
        }
    }

    // Get completed commands, by using our own slot-status
    sact = READ_VOLATILE(port->Registers->SACT);
    doneCommands = atomic_load(&port->Slots) ^ sact;
    TRACE("DoneCommands(0x%x) <= SlotStatus(0x%x) ^ AtaActive(0x%x)",
          doneCommands, atomic_load(&port->Slots), sact);

    // Check for command completion by iterating through the command slots
    if (doneCommands != 0) {
        for (i = 0; i < port->SlotCount; i++) {
            if (doneCommands & (1 << i)) {
                __UpdateTransaction(controller, port, i);
            }
        }
    }

    interruptStatus = atomic_exchange(&controller->InterruptResource.PortInterruptStatus[port->Index], 0);
    if (interruptStatus) {
        goto handler_loop;
    }
}
