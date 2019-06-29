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
#include "manager.h"
#include <threads.h>
#include <stdlib.h>
#include <assert.h>

AhciPort_t*
AhciPortCreate(
    _In_ AhciController_t*  Controller, 
    _In_ int                Port, 
    _In_ int                Index)
{
    AhciPort_t* AhciPort;

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
    AhciPort->Id           = Port;     // Sequential port number
    AhciPort->Index        = Index;    // Index in validity map
    AhciPort->Registers    = (AHCIPortRegisters_t*)((uintptr_t)Controller->Registers + AHCI_REGISTER_PORTBASE(Index)); // @todo port nr or bit index?
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

    // Cleanup all transactions
    _foreach(Node, Port->Transactions) {
        cnd_destroy((cnd_t*)Node->Data);
    }

    // Free the memory resources allocated
    if (Port->RecievedFisTable != NULL) {
        free((void*)Port->RecievedFisTable);
    }
    free(Port);
}

OsStatus_t
AhciPortIdentifyDevice(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    Port->Connected = 1;
    TRACE(" > device present 0x%x on port %i", Port->Registers->Signature, Port->Id);
    return AhciManagerCreateDevice(Controller, Port);
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

void
AhciPortRebase(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    uintptr_t CommandTablePointerPhysical = 0;
    reg32_t   Caps = ReadVolatile32(&Controller->Registers->Capabilities);
    int       i;

    // Initialize memory structures - both RecievedFIS and PRDT
    Port->CommandList  = (AHCICommandList_t*)((uint8_t*)Controller->CommandListBase + (sizeof(AHCICommandList_t) * Port->Id));
    Port->CommandTable = (void*)((uint8_t*)Controller->CommandTableBase + ((AHCI_COMMAND_TABLE_SIZE  * 32) * Port->Id));
    
    CommandTablePointerPhysical = Controller->CommandTableBasePhysical + ((AHCI_COMMAND_TABLE_SIZE * 32) * Port->Id);

    // Setup FIS Area
    if (Caps & AHCI_CAPABILITIES_FBSS) {
        Port->RecievedFis = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (0x1000 * Port->Id));
    }
    else {
        Port->RecievedFis = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (256 * Port->Id));
    }
    
    // Setup Recieved-FIS table
    Port->RecievedFisTable = (AHCIFis_t*)malloc(Controller->CommandSlotCount * AHCI_RECIEVED_FIS_SIZE);
    assert(Port->RecievedFisTable != NULL);
    memset((void*)Port->RecievedFisTable, 0, Controller->CommandSlotCount * AHCI_RECIEVED_FIS_SIZE);
    
    // Iterate the 32 command headers
    for (i = 0; i < 32; i++) {
        Port->CommandList->Headers[i].Flags        = 0;
        Port->CommandList->Headers[i].TableLength  = 0;
        Port->CommandList->Headers[i].PRDByteCount = 0;

        // Load the command table address (physical)
        Port->CommandList->Headers[i].CmdTableBaseAddress = 
            LODWORD(CommandTablePointerPhysical);

        // Set command table address upper register if supported and we are in 64 bit
        if (Caps & AHCI_CAPABILITIES_S64A) {
            Port->CommandList->Headers[i].CmdTableBaseAddressUpper = 
                (sizeof(void*) > 4) ? HIDWORD(CommandTablePointerPhysical) : 0;
        }
        else {
            Port->CommandList->Headers[i].CmdTableBaseAddressUpper = 0;
        }
        CommandTablePointerPhysical += AHCI_COMMAND_TABLE_SIZE;
    }
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
    return AhciPortIdentifyDevice(Controller, Port);
}

OsStatus_t
AhciPortStart(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    uintptr_t PhysicalAddress;
    reg32_t   Caps = ReadVolatile32(&Controller->Registers->Capabilities);
    int       Hung = 0;

    // Setup the physical data addresses
    if (Caps & AHCI_CAPABILITIES_FBSS) {
        PhysicalAddress = Controller->FisBasePhysical + (0x1000 * Port->Id);
    }
    else {
        PhysicalAddress = Controller->FisBasePhysical + (256 * Port->Id);
    }

    WriteVolatile32(&Port->Registers->FISBaseAddress, LOWORD(PhysicalAddress));
    if (Caps & AHCI_CAPABILITIES_S64A) {
        WriteVolatile32(&Port->Registers->FISBaseAdressUpper,
            (sizeof(void*) > 4) ? HIDWORD(PhysicalAddress) : 0);
    }
    else {
        WriteVolatile32(&Port->Registers->FISBaseAdressUpper, 0);
    }

    PhysicalAddress = Controller->CommandListBasePhysical + (sizeof(AHCICommandList_t) * Port->Id);
    WriteVolatile32(&Port->Registers->CmdListBaseAddress, LODWORD(PhysicalAddress));
    if (Caps & AHCI_CAPABILITIES_S64A) {
        WriteVolatile32(&Port->Registers->CmdListBaseAddressUpper,
            (sizeof(void*) > 4) ? HIDWORD(PhysicalAddress) : 0);
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

void
AhciPortInterruptHandler(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    AhciTransaction_t* Transaction;
    reg32_t            InterruptStatus;
    reg32_t            DoneCommands;
    CollectionItem_t*  tNode;
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
            AhciManagerRemoveDevice(Controller, Port);
            Port->Connected = 0;
        }
        else {
            AhciPortIdentifyDevice(Controller, Port);
        }
    }

    // Get completed commands, by using our own slot-status
    DoneCommands = Port->SlotStatus ^ ReadVolatile32(&Port->Registers->AtaActive);
    TRACE("DoneCommands(0x%x) <= SlotStatus(0x%x) ^ AtaActive(0x%x)", 
        DoneCommands, Port->SlotStatus, Port->Registers->AtaActive);

    // Check for command completion
    // by iterating through the command slots
    if (DoneCommands != 0) {
        for (i = 0; i < AHCI_MAX_PORTS; i++) {
            if (DoneCommands & (1 << i)) {
                Key.Value.Integer   = i;
                tNode               = CollectionGetNodeByKey(Port->Transactions, Key, 0);
                
                assert(tNode != NULL);
                Transaction = (AhciTransaction_t*)tNode;

                // Remove and destroy node
                CollectionRemoveByNode(Port->Transactions, tNode);
                CollectionDestroyNode(Port->Transactions, tNode);

                // Copy data over - we make a copy of the recieved fis
                // to make the slot reusable as quickly as possible
                memcpy((void*)&Port->RecievedFisTable[i], (void*)Port->RecievedFis, sizeof(AHCIFis_t));
                AhciCommandFinish(Transaction);
            }
        }
    }

    // Re-handle?
    if (Controller->InterruptResource.PortInterruptStatus[Port->Index] != 0) {
        goto HandleInterrupt;
    }
}
