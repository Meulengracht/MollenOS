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
    AhciPort_t* port;
    TRACE("AhciPortCreate(controller=0x%" PRIxIN ", portIndex=%i, mapIndex=%i)",
          controller, portIndex, mapIndex);

    port = (AhciPort_t*)malloc(sizeof(AhciPort_t));
    if (!port) {
        return NULL;
    }
    
    memset(port, 0, sizeof(AhciPort_t));
    list_construct(&port->Transactions);
    list_construct(&port->CleanupTransactions);

    port->Id        = portIndex;     // Sequential port number
    port->Index     = mapIndex;    // Index in validity map
    port->SlotCount = AHCI_CAPABILITIES_NCS(controller->Registers->Capabilities);

    // TODO: port nr or bit index? Right now use the Index in the validity map
    port->Registers = (AHCIPortRegisters_t*)((uintptr_t)controller->Registers + AHCI_REGISTER_PORTBASE(mapIndex));
    return port;
}

oserr_t
AhciPortStop(
    _In_ AhciPort_t* port)
{
    reg32_t status;
    int     hung;

    // ST must clear before FRE. CR and FR acknowledge that the controller has
    // stopped fetching commands and writing received FIS memory respectively.
    WRITE_VOLATILE(port->Registers->InterruptEnable, 0);
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    WRITE_VOLATILE(port->Registers->CommandAndStatus, status & ~AHCI_PORT_ST);
    
    WaitForConditionWithFault(hung,
        (READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_CR) == 0, 6, 100);
    if (hung) {
        WARNING("AHCI port %i: command engine did not stop", port->Index);
        return OS_ETIMEOUT;
    }
    
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    WRITE_VOLATILE(port->Registers->CommandAndStatus, status & ~AHCI_PORT_FRE);
    
    WaitForConditionWithFault(hung,
        (READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_FR) == 0, 6, 100);
    if (hung) {
        WARNING("AHCI port %i: FIS engine did not stop", port->Index);
        return OS_ETIMEOUT;
    }
    
    dma_rmb();
    return OS_EOK;
}

oserr_t
AhciPortCleanup(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    oserr_t status;

    status = AhciPortStop(port);
    if (status != OS_EOK) {
        return status;
    }

    // A stopped engine cannot access request mappings or any of these buffers.
    // Each successful free clears its slot. Failed cleanup leaves the port in
    // the controller table, so a later attempt resumes at the remaining work.
    status = AhciTransactionsCancel(controller, port);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAFree(&port->Dma.ReceivedFis);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAFree(&port->Dma.CommandTables);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAFree(&port->Dma.CommandList);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAFree(&port->Dma.InternalBuffer);
    if (status != OS_EOK) {
        return status;
    }
    
    AhciManagerUnregisterDevice(controller, port);
    controller->Ports[port->Index] = NULL;
    free(port);
    return OS_EOK;
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

oserr_t
AhciPortFinishSetup(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    reg32_t status;
    int     hung;
    TRACE("AhciPortFinishSetup(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    if (AhciPortStop(port) != OS_EOK) {
        return OS_ETIMEOUT;
    }

    // Software causes a port reset (COMRESET) by writing 1h to the PxSCTL.DET
    // Also disable slumber and partial state
    WRITE_VOLATILE(port->Registers->SCTL,
        AHCI_PORT_SCTL_DISABLE_PARTIAL_STATE |
        AHCI_PORT_SCTL_DISABLE_SLUMBER_STATE |
        AHCI_PORT_SCTL_RESET);
    thrd_sleep(&(struct timespec) { .tv_nsec = 50 * NSEC_PER_MSEC }, NULL);

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
            return OS_ETIMEOUT;
        }
        return OS_EUNKNOWN;
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
    return OS_EOK;
}

static oserr_t
__AllocatePortBuffer(
    _In_    AhciController_t*  controller,
    _In_    size_t             length,
    _In_    uint64_t           alignment,
    _InOut_ struct DMABuffer** buffer)
{
    struct DMARequirements requirements = {
        .Length = length,
        .Alignment = alignment,
        .Contiguous = true
    };

    // Port tables remain shared with the CPU. DMAAllocate selects suitable
    // memory for this controller, so submission needs only ordering barriers.
    return DMAAllocate(controller->DmaContext, &requirements, buffer);
}

oserr_t
AhciPortRebase(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    AHCICommandList_t*       commandList;
    const struct DMASegment* segments;
    uint64_t                 address;
    uint32_t                 count;
    oserr_t                  status;

    // The caller must stop both engines before replacing their table addresses.
    if (READ_VOLATILE(port->Registers->CommandAndStatus) &
        (AHCI_PORT_ST | AHCI_PORT_CR | AHCI_PORT_FRE | AHCI_PORT_FR)) {
        return OS_EBUSY;
    }
    
    status = __AllocatePortBuffer(controller, AhciManagerGetFrameSize(), 2, &port->Dma.InternalBuffer);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocatePortBuffer(controller, sizeof(AHCICommandList_t), 1024, &port->Dma.CommandList);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __AllocatePortBuffer(controller, AHCI_COMMAND_TABLE_SIZE * port->SlotCount,
        128, &port->Dma.CommandTables);
    if (status != OS_EOK) {
        return status;
    }
    
    status = __AllocatePortBuffer(controller, 4096, 256, &port->Dma.ReceivedFis);
    if (status != OS_EOK) {
        return status;
    }

    // The table allocation is contiguous in device address space. Each slot
    // has its own 4 KiB table; CPU pointer width does not limit device addresses.
    segments = DMABufferSegments(port->Dma.CommandTables, &count);
    commandList = DMABufferData(port->Dma.CommandList);
    for (int i = 0; i < port->SlotCount; i++) {
        address = segments[0].Address + (uint64_t)i * AHCI_COMMAND_TABLE_SIZE;
        commandList->Headers[i].CmdTableBaseAddress = (uint32_t)address;
        commandList->Headers[i].CmdTableBaseAddressUpper = (uint32_t)(address >> 32);
    }
    return OS_EOK;
}

oserr_t
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
        return OS_ETIMEOUT;
    }

    // Wait for BSYDRQ to clear
    if (READ_VOLATILE(port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) {
        WRITE_VOLATILE(port->Registers->SERR, (1 << 26)); // Exchanged bit.
        WaitForConditionWithFault(hung, (READ_VOLATILE(port->Registers->TaskFileData) & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0, 30, 100);
        if (hung) {
            ERROR("AhciPortEnable failed to clear BSY and DRQ: 0x%x", port->Registers->TaskFileData);
            return OS_ETIMEOUT;
        }
    }

    // Finally start
    status = READ_VOLATILE(port->Registers->CommandAndStatus);
    TRACE("AhciPortEnable CommandAndStatus=0x%x", status);

    WRITE_VOLATILE(port->Registers->CommandAndStatus, status | AHCI_PORT_ST);
    WaitForConditionWithFault(hung, READ_VOLATILE(port->Registers->CommandAndStatus) & AHCI_PORT_CR, 6, 100);
    if (hung) {
        ERROR("AhciPortEnable command engine failed to start: 0x%x", port->Registers->CommandAndStatus);
        return OS_ETIMEOUT;
    }

    port->Connected = 1;

    status = READ_VOLATILE(port->Registers->Signature);
    return AhciManagerRegisterDevice(controller, port, status);
}

oserr_t
AhciPortStart(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port)
{
    const struct DMASegment* segments;
    uint32_t                 count;
    reg32_t                  status;
    int                      hung;

    TRACE("AhciPortStart(controller=0x%" PRIxIN ", port=0x%" PRIxIN ")",
          controller, port);

    // These addresses come from the controller's DMA domain, not CPU pages.
    segments = DMABufferSegments(port->Dma.ReceivedFis, &count);
    WRITE_VOLATILE(port->Registers->FISBaseAddress, (uint32_t)segments[0].Address);
    WRITE_VOLATILE(port->Registers->FISBaseAdressUpper, (uint32_t)(segments[0].Address >> 32));
    
    segments = DMABufferSegments(port->Dma.CommandList, &count);
    WRITE_VOLATILE(port->Registers->CmdListBaseAddress, (uint32_t)segments[0].Address);
    WRITE_VOLATILE(port->Registers->CmdListBaseAddressUpper, (uint32_t)(segments[0].Address >> 32));
    dma_wmb();

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
        return OS_ETIMEOUT;
    }
    
    // Wait for FRE and BSYDRQ. This assumes a device present
    status = READ_VOLATILE(port->Registers->STSS);
    TRACE("AhciPortStart AtaStatus=0x%x", status);

    if (AHCI_PORT_STSS_DET(status) != AHCI_PORT_SSTS_DET_ENABLED) {
        TRACE("AhciPortStart port %i has nothing present: 0x%x", port->Index, port->Registers->STSS);
        return OS_EOK;
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
    dma_wmb();
    if (nqc) {
        WRITE_VOLATILE(port->Registers->SACT, bitIndex);
    }
    WRITE_VOLATILE(port->Registers->CI, bitIndex);
}

oserr_t
AhciPortAllocateCommandSlot(
    _In_  AhciPort_t* port,
    _Out_ int*        slotOut)
{
    // A full port must not spin in the event loop: completions need that loop
    // to free slots. This driver reports busy so its caller can retry the I/O.
    for (int i = 0; i < port->SlotCount; i++) {
        uint32_t bit = 1U << i;
        uint32_t slots = atomic_load(&port->Slots);
        while (!(slots & bit)) {
            if (atomic_compare_exchange_weak(&port->Slots, &slots, slots | bit)) {
                *slotOut = i;
                return OS_EOK;
            }
        }
    }
    return OS_EBUSY;
}

void
AhciPortFreeCommandSlot(
    _In_ AhciPort_t* port,
    _In_ int         slot)
{
    atomic_fetch_and(&port->Slots, ~(1U << slot));
}

static inline void
__UpdateTransaction(
    _In_ AhciController_t* controller,
    _In_ AhciPort_t*       port,
    _In_ int               portSlot)
{
    AHCICommandList_t*   commandList;
    AHCICommandHeader_t* commandHeader;
    AhciTransaction_t*   transaction;
    size_t               bytesTransferred;

    // Earlier completion callbacks can yield and allow a slot to be reused.
    // Recheck this slot instead of trusting the interrupt's original bit mask.
    if ((READ_VOLATILE(port->Registers->CI) | READ_VOLATILE(port->Registers->SACT)) & (1U << portSlot)) {
        return;
    }
    transaction = (AhciTransaction_t*)list_find_value(&port->Transactions, (void*)(uintptr_t)portSlot);
    if (!transaction) {
        AhciPortFreeCommandSlot(port, portSlot);
        ERROR("__UpdateTransaction found no transaction with id %i", portSlot);
        return;
    }

    commandList   = (AHCICommandList_t*)DMABufferData(port->Dma.CommandList);
    commandHeader = &commandList->Headers[portSlot];

    // CI/SACT completion must be observed before reading controller writes.
    dma_rmb();
    bytesTransferred = commandHeader->PRDByteCount;

    // Handle transaction completion, release slot, queue up a new command if any
    // and then handle the event
    memcpy((void*)&transaction->Response, DMABufferData(port->Dma.ReceivedFis), sizeof(AHCIFis_t));
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
    // Non-NCQ commands use CI; NCQ also keeps SACT set until completion.
    doneCommands = atomic_load(&port->Slots) & ~(sact | READ_VOLATILE(port->Registers->CI));
    TRACE("DoneCommands(0x%x) <= SlotStatus(0x%x) ^ AtaActive(0x%x)",
          doneCommands, atomic_load(&port->Slots), sact);

    // Check for command completion by iterating through the command slots
    if (doneCommands != 0) {
        for (i = 0; i < port->SlotCount; i++) {
            if (doneCommands & (1U << i)) {
                __UpdateTransaction(controller, port, i);
            }
        }
    }

    interruptStatus = atomic_exchange(&controller->InterruptResource.PortInterruptStatus[port->Index], 0);
    if (interruptStatus) {
        goto handler_loop;
    }
}
