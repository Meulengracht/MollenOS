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

#ifndef _AHCI_MANAGER_H_
#define _AHCI_MANAGER_H_

#include "ahci.h"
#include <ds/list.h>
#include <commands.h>
#include <gracht/link/vali.h>
#include <os/osdefs.h>
#include <threads.h>

typedef enum {
    TransactionCreated,
    TransactionQueued,
    TransactionInProgress
} TransactionState_t;

typedef enum {
    TransactionRegisterFISH2D
} TransactionType_t;

typedef enum {
    DeviceATA,
    DeviceATAPI,
    DevicePM,  // Port Multiplier
    DeviceSEMB // Enclosure Management Bridge
} DeviceType_t;

typedef struct AhciDevice {
    element_t           header;
    StorageDescriptor_t Descriptor;

    AhciController_t* Controller;
    AhciPort_t*       Port;
    int               Index;

    DeviceType_t      Type;
    int               HasDMAEngine;
    size_t            SectorSize;
    uint64_t          SectorCount;
    
    int               AddressingMode;    // (0) CHS, (1) LBA28, (2) LBA48
    struct {
        uint16_t CylinderCount;
        uint8_t  TracksPerCylinder;
        uint8_t  SectorsPerTrack;
    } CHS;
} AhciDevice_t;

#define AHCI_DEVICE_MODE_CHS    0
#define AHCI_DEVICE_MODE_LBA28  1
#define AHCI_DEVICE_MODE_LBA48  2

typedef struct AhciTransation {
    element_t             header;
    int                   Internal;
    TransactionState_t    State;
    TransactionType_t     Type;
    AtaCommand_t          Command;
    int                   Slot;
    int                   Direction;
    AHCIFis_t             Response;
    struct dma_attachment DmaAttachment;
    struct dma_sg_table   DmaTable;

    struct {
        DeviceType_t Type;
        size_t       SectorSize;
        int          AddressingMode;
    } Target;

    uint64_t              Sector;
    size_t                SectorsTransferred;
    int                   SectorAlignment;
    size_t                BytesLeft;
    
    int                   SgIndex;
    size_t                SgOffset;
    
    struct vali_link_deferred_response DeferredMessage;
} AhciTransaction_t;

#define AHCI_XACTION_IN     0
#define AHCI_XACTION_OUT    1

/**
 * Ahci Manager Interface
 * Initialization and destruction of the ahci manager. Tracks both devices and
 * transactions
 */
__EXTERN OsStatus_t AhciManagerInitialize(void);
__EXTERN void AhciManagerDestroy(void);
__EXTERN size_t     AhciManagerGetFrameSize(void);
__EXTERN OsStatus_t AhciManagerRegisterDevice(AhciController_t*, AhciPort_t*, uint32_t);
__EXTERN void       AhciManagerUnregisterDevice(AhciController_t*, AhciPort_t*);
__EXTERN void       AhciManagerHandleControlResponse(AhciPort_t*, AhciTransaction_t*);

/**
 * AhciManagerGetDevice
 * * Convert a system device identifier to a AhciDevice_t structure.
 */
__EXTERN AhciDevice_t*
AhciManagerGetDevice(
    _In_ UUId_t deviceId);

/**
 * AhciTransactionControlCreate
 * @param Device  [In] The device that should handle the transaction.
 * @param Command [In] The transaction that should get queued up.
 */
__EXTERN OsStatus_t
AhciTransactionControlCreate(
    _In_ AhciDevice_t* Device,
    _In_ AtaCommand_t  Command,
    _In_ size_t        Length,
    _In_ int           Direction);

/** 
 * AhciDeviceCancelTransaction
 */
__EXTERN OsStatus_t
AhciManagerCancelTransaction(
    _In_ AhciTransaction_t* Transaction);

/**
 * AhciTransactionHandleResponse
 */
OsStatus_t
AhciTransactionHandleResponse(
    _In_ AhciController_t*  Controller,
    _In_ AhciPort_t*        Port,
    _In_ AhciTransaction_t* Transaction);

#endif //!_AHCI_MANAGER_H_
