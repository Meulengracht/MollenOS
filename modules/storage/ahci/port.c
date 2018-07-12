/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */
//#define __TRACE

#include <os/mollenos.h>
#include <os/utils.h>
#include "manager.h"
#include <threads.h>
#include <stdlib.h>
#include <assert.h>

/* AhciPortCreate
 * Initializes the port structure, but not memory structures yet */
AhciPort_t*
AhciPortCreate(
    _In_ AhciController_t*  Controller, 
    _In_ int                Port, 
    _In_ int                Index)
{
    // Varaibles
    AhciPort_t *AhciPort = NULL;

    // Sanitize the port, don't create an already existing
    // and make sure port is valid
    if (Controller->Ports[Port] != NULL || Port >= AHCI_MAX_PORTS) {
        if (Controller->Ports[Port] != NULL) {
            return Controller->Ports[Port];
        }
        return NULL;
    }

    // Allocate a new port instance
    AhciPort = (AhciPort_t*)malloc(sizeof(AhciPort_t));
    memset(AhciPort, 0, sizeof(AhciPort_t));

    // Set port id and index
    AhciPort->Id    = Port;
    AhciPort->Index = Index;

    // Instantiate the register pointer
    AhciPort->Registers = (AHCIPortRegisters_t*)
        ((uint8_t*)Controller->Registers + AHCI_REGISTER_PORTBASE(Port));

    // Create the transaction list and we're done!
    AhciPort->Transactions = CollectionCreate(KeyInteger);
    return AhciPort;
}

/* AhciPortInitialize 
 * Initializes the memory regions and enables them in the port */
void
AhciPortInitialize(
    _In_  AhciController_t* Controller, 
    _Out_ AhciPort_t*       Port)
{
    // Variables
    uintptr_t CommandTablePointerPhysical   = 0;
    uintptr_t PhysicalAddress               = 0;
    uint8_t *CommandTablePointer            = NULL;
    int i;

    // Initialize memory structures 
    // Both RecievedFIS and PRDT
    Port->CommandList   = (AHCICommandList_t*)
        ((uint8_t*)Controller->CommandListBase + (1024 * Port->Id));
    Port->CommandTable  = (void*)((uint8_t*)Controller->CommandTableBase 
        + ((AHCI_COMMAND_TABLE_SIZE  * 32) * Port->Id));

    // Setup FIS Area
    if (Controller->Registers->Capabilities & AHCI_CAPABILITIES_FBSS) {
        Port->RecievedFis   = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (0x1000 * Port->Id));
        PhysicalAddress     = Controller->FisBasePhysical + (0x1000 * Port->Id);
    }
    else {
        Port->RecievedFis   = (AHCIFis_t*)((uint8_t*)Controller->FisBase + (256 * Port->Id));
        PhysicalAddress     = Controller->FisBasePhysical + (256 * Port->Id);
    }
    
    // Setup Recieved-FIS table
    Port->RecievedFisTable = (AHCIFis_t**)malloc(Controller->CommandSlotCount * 256);
    memset((void*)Port->RecievedFisTable, 0, Controller->CommandSlotCount * 256);

    // Instantiate the command table pointer
    CommandTablePointerPhysical = Controller->CommandTableBasePhysical 
        + ((AHCI_COMMAND_TABLE_SIZE * 32) * Port->Id);
    CommandTablePointer         = (uint8_t*)Port->CommandTable;

    // Iterate the 32 command headers
    for (i = 0; i < 32; i++) {
        // Setup entry
        Port->CommandList->Headers[i].Flags         = 0;
        Port->CommandList->Headers[i].TableLength   = AHCI_PORT_PRDT_COUNT;
        Port->CommandList->Headers[i].PRDByteCount  = 0;

        // Load the command table address (physical)
        Port->CommandList->Headers[i].CmdTableBaseAddress = 
            LODWORD(CommandTablePointerPhysical);

        // Set command table address upper register 
        // Only set upper if we are in 64 bit
        Port->CommandList->Headers[i].CmdTableBaseAddressUpper = 
            (sizeof(void*) > 4) ? HIDWORD(CommandTablePointerPhysical) : 0;

        // Go to next entry
        CommandTablePointerPhysical += AHCI_COMMAND_TABLE_SIZE;
        CommandTablePointer         += AHCI_COMMAND_TABLE_SIZE;
    }

    // Update registers with the new physical addresses
    // PhysicalAddress already contains the FIS
    Port->Registers->FISBaseAddress     = LOWORD(PhysicalAddress);
    Port->Registers->FISBaseAdressUpper = (sizeof(void*) > 4) ? HIDWORD(PhysicalAddress) : 0;

    PhysicalAddress = Controller->CommandListBasePhysical + (1024 * Port->Id);
    Port->Registers->CmdListBaseAddress         = LODWORD(PhysicalAddress);
    Port->Registers->CmdListBaseAddressUpper    = (sizeof(void*) > 4) ? HIDWORD(PhysicalAddress) : 0;
    MemoryBarrier();

    // After setting PxFB and PxFBU to the physical address of the FIS receive area,
    // system software shall set PxCMD.FRE to 1.
    Port->Registers->CommandAndStatus   |= AHCI_PORT_FRE;
    MemoryBarrier();

    // For each implemented port, clear the PxSERR register, 
    // by writing 1s to each implemented bit location.
    Port->Registers->AtaError           = AHCI_PORT_SERR_CLEARALL;
    Port->Registers->InterruptStatus    = 0xFFFFFFFF;
    
    // Determine which events should cause an interrupt, 
    // and set each implemented ports PxIE register with the appropriate enables.
    Port->Registers->InterruptEnable    = (uint32_t)AHCI_PORT_IE_CPDE | AHCI_PORT_IE_TFEE
        | AHCI_PORT_IE_PCE | AHCI_PORT_IE_DSE | AHCI_PORT_IE_PSE | AHCI_PORT_IE_DHRE;
}

/* AhciPortCleanup
 * Destroys a port, cleans up device, cleans up memory and resources */
void
AhciPortCleanup(
    _In_  AhciController_t* Controller, 
    _Out_ AhciPort_t*       Port)
{
    // Variables
    CollectionItem_t *pNode;

    // Null out the port-entry in the controller
    Controller->Ports[Port->Index] = NULL;

    // Cleanup all transactions
    _foreach(pNode, Port->Transactions) {
        cnd_destroy((cnd_t*)pNode->Data);
    }

    // Free the memory resources allocated
    if (Port->RecievedFisTable != NULL) {
        free((void*)Port->RecievedFisTable);
    }
    CollectionDestroy(Port->Transactions);
    free(Port);
}

/* AhciPortReset
 * Resets the port, and resets communication with the device on the port
 * if the communication was destroyed */
OsStatus_t
AhciPortReset(
    _In_ AhciController_t*  Controller, 
    _In_ AhciPort_t*        Port)
{
    // Variables
    reg32_t Control;

    // Unused parameters
    _CRT_UNUSED(Controller);

    // Software causes a port reset (COMRESET) by writing 1h to the PxSCTL.DET
    Control = Port->Registers->AtaControl;

    // Remove current status, set reset
    Control &= ~(AHCI_PORT_SCTL_DET_MASK);
    Control |= AHCI_PORT_SCTL_DET_RESET;
    
    // Do the reset
    Port->Registers->AtaControl |= Control;
    MemoryBarrier();

    // wait at least 1 millisecond before clearing PxSCTL.DET to 0h
    thrd_sleepex(2);

    // After clearing PxSCTL.DET to 0h, software should wait for 
    // communication to be re-established as indicated by PxSSTS.DET 
    // being set to 3h.
    Port->Registers->AtaControl &= ~(AHCI_PORT_SCTL_DET_MASK);
    MemoryBarrier();
    WaitForCondition((Port->Registers->AtaStatus & AHCI_PORT_SSTS_DET_ENABLED), 10, 25,
        "Port status never reached communication established, proceeding anyway.", 0);

    // Then software should write all 1s to the PxSERR register to clear 
    // any bits that were set as part of the port reset.
    Port->Registers->AtaError = AHCI_PORT_SERR_CLEARALL;

    // When PxSCTL.DET is set to 1h, the HBA shall reset PxTFD.STS to 7Fh and 
    // shall reset PxSSTS.DET to 0h. When PxSCTL.DET is set to 0h, upon receiving a 
    // COMINIT from the attached device, PxTFD.STS.BSY shall be set to 1 by the HBA.
    return OsSuccess;
}

/* AhciPortSetupDevice
 * Identifies connection on a port, and initializes connection/device */
OsStatus_t
AhciPortSetupDevice(
    _In_ AhciController_t*  Controller, 
    _In_ AhciPort_t*        Port)
{
    // Start command engine
    Port->Registers->CommandAndStatus |= AHCI_PORT_ST | AHCI_PORT_FRE;

    // Detect present ports using
    // PxTFD.STS.BSY = 0, PxTFD.STS.DRQ = 0, and PxSSTS.DET = 3
    if (Port->Registers->TaskFileData & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)
        || (AHCI_PORT_STSS_DET(Port->Registers->AtaStatus) != AHCI_PORT_SSTS_DET_ENABLED)) {
        return OsError;
    }

    // Update port status
    TRACE("AHCI::Device present 0x%x on port %i", Port->Registers->Signature, Port->Id);
    Port->Connected = 1;

    // Identify device
    return AhciManagerCreateDevice(Controller, Port);
}

/* AhciPortAcquireCommandSlot
 * Allocates an available command slot on a port returns index on success, otherwise -1 */
OsStatus_t
AhciPortAcquireCommandSlot(
    _In_  AhciController_t* Controller, 
    _In_  AhciPort_t*       Port,
    _Out_ int*              Index)
{
    // Variables
    reg32_t AtaActive = Port->Registers->AtaActive;
    OsStatus_t Status = OsError;
    int i;

    // Sanitize out
    if (Index == NULL) {
        return OsError;
    }

    // Iterate possible command slots
    for (i = 0; i < (int)Controller->CommandSlotCount; i++) {
        // Check availability status 
        // on this command slot
        if ((Port->SlotStatus & (1 << i)) != 0 || (AtaActive & (1 << i)) != 0) {
            continue;
        }

        // Allocate slot and update the out variables
        Status              = OsSuccess;
        Port->SlotStatus    |= (1 << i);
        *Index              = i;
        break;
    }
    return Status;
}

/* AhciPortReleaseCommandSlot
 * Deallocates a previously allocated command slot */
void
AhciPortReleaseCommandSlot(
    _In_ AhciPort_t*        Port, 
    _In_ int                Slot)
{
    Port->SlotStatus &= ~(1 << Slot);
}

/* AhciPortStartCommandSlot
 * Starts a command slot on the given port */
void
AhciPortStartCommandSlot(
    _In_ AhciPort_t*        Port, 
    _In_ int                Slot)
{
    // Set slot to active
    Port->Registers->CommandIssue |= (1 << Slot);
}

/* AhciPortInterruptHandler
 * Port specific interrupt handler 
 * handles interrupt for a specific port */
void
AhciPortInterruptHandler(
    _In_ AhciController_t*  Controller, 
    _In_ AhciPort_t*        Port)
{
    // Variables
    AhciTransaction_t *Transaction;
	reg32_t InterruptStatus;
    reg32_t DoneCommands;
    CollectionItem_t *tNode;
    DataKey_t Key;
    int i;
    
    // Check interrupt services 
    // Cold port detect, recieved fis etc
    TRACE("AhciPortInterruptHandler(Port %i, Interrupt Status 0x%x)",
        Port->Id, Port->InterruptStatus);

HandleInterrupt:
    InterruptStatus         = Port->InterruptStatus;
    Port->InterruptStatus   = 0;
    
    // Check for errors status's
    if (Port->InterruptStatus & (AHCI_PORT_IE_TFEE | AHCI_PORT_IE_HBFE 
        | AHCI_PORT_IE_HBDE | AHCI_PORT_IE_IFE | AHCI_PORT_IE_INFE)) {
        ERROR("AHCI::Port ERROR %i, CMD: 0x%x, CI 0x%x, IE: 0x%x, IS 0x%x, TFD: 0x%x", Port->Id,
            Port->Registers->CommandAndStatus, Port->Registers->CommandIssue,
            Port->Registers->InterruptEnable, Port->InterruptStatus,
            Port->Registers->TaskFileData);
    }

    // Check for hot-plugs
    if (Port->InterruptStatus & AHCI_PORT_IE_PCE) {
        // Determine whether or not there is a device connected
        // Detect present ports using
        // PxTFD.STS.BSY = 0, PxTFD.STS.DRQ = 0, and PxSSTS.DET = 3
        if (Port->Registers->TaskFileData & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)
            || (AHCI_PORT_STSS_DET(Port->Registers->AtaStatus) != AHCI_PORT_SSTS_DET_ENABLED)) {
            AhciManagerRemoveDevice(Controller, Port);
            Port->Connected = 0;
        }
        else {
            AhciPortSetupDevice(Controller, Port);
        }
    }

    // Get completed commands, by using our own slot-status
    DoneCommands = Port->SlotStatus ^ Port->Registers->AtaActive;
    TRACE("DoneCommands(0x%x) <= SlotStatus(0x%x) ^ AtaActive(0x%x)", 
        DoneCommands, Port->SlotStatus, Port->Registers->AtaActive);

    // Check for command completion
    // by iterating through the command slots
    if (DoneCommands != 0) {
        for (i = 0; i < AHCI_MAX_PORTS; i++) {
            if (DoneCommands & (1 << i)) {
                size_t Offset   = i * AHCI_RECIEVED_FIS_SIZE;
                Key.Value       = i;
                tNode           = CollectionGetNodeByKey(Port->Transactions, Key, 0);
                assert(tNode != NULL);
                Transaction     = (AhciTransaction_t*)tNode->Data;

                // Remove and destroy node
                CollectionRemoveByNode(Port->Transactions, tNode);
                CollectionDestroyNode(Port->Transactions, tNode);

                // Copy data over - we make a copy of the recieved fis
                // to make the slot reusable as quickly as possible
                memcpy((void*)((uint8_t*)Port->RecievedFisTable + Offset), 
                    (void*)Port->RecievedFis, sizeof(AHCIFis_t));

                // Take care of transaction
                AhciCommandFinish(Transaction);
            }
        }
    }

    // Re-handle?
    if (Port->InterruptStatus != 0) {
        goto HandleInterrupt;
    }
}
