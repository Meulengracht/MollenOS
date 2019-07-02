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

#include <os/osdefs.h>
#include <ds/collection.h>
#include <commands.h>
#include "ahci.h"

typedef enum {
    TransactionCreated,
    TransactionQueued,
    TransactionInProgress
} TransactionState_t;

typedef enum {
    TransactionRegisterFISH2D
} TransactionType_t;

typedef struct _AhciDevice {
    CollectionItem_t        Header;
    StorageDescriptor_t     Descriptor;

    AhciController_t*       Controller;
    AhciPort_t*             Port;
    int                     Index;

    int                     Type;              // 0 -> ATA, 1 -> ATAPI
    int                     UseDMA;
    uint64_t                SectorsLBA;
    int                     AddressingMode;    // (0) CHS, (1) LBA28, (2) LBA48
    size_t                  SectorSize;
} AhciDevice_t;

#define AHCI_DEVICE_TYPE_ATA    0
#define AHCI_DEVICE_TYPE_ATAPI  1

#define AHCI_DEVICE_MODE_CHS    0
#define AHCI_DEVICE_MODE_LBA28  1
#define AHCI_DEVICE_MODE_LBA48  2

typedef struct {
    CollectionItem_t      Header;
    MRemoteCallAddress_t  ResponseAddress;
    
    TransactionState_t    State;
    TransactionType_t     Type;
    ATACommandType_t      Command;
    int                   Slot;
    int                   Direction;
    AHCIFis_t             Response;
    struct dma_attachment DmaAttachment;

    uint64_t              Sector;
    int                   SectorAlignment;
    size_t                BytesLeft;
    
    int                   SgIndex;
    size_t                SgOffset;
    int                   SgCount;
    struct dma_sg         SgList[1];
} AhciTransaction_t;

#define AHCI_XACTION_IN     0
#define AHCI_XACTION_OUT    1

/**
 * Ahci Manager Interface
 * Initialization and destruction of the ahci manager. Tracks both devices and
 * transactions
 */
__EXTERN OsStatus_t AhciManagerInitialize(void);
__EXTERN OsStatus_t AhciManagerDestroy(void);
__EXTERN size_t     AhciManagerGetFrameSize(void);
__EXTERN OsStatus_t AhciManagerRegisterDevice(AhciDevice_t*);
__EXTERN OsStatus_t AhciManagerUnregisterDevice(AhciDevice_t*);

/**
 * AhciDeviceRegister
 */
__EXTERN OsStatus_t
AhciDeviceRegister(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port);

/**
 * AhciDeviceUnregister
 */
__EXTERN OsStatus_t
AhciDeviceUnregister(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port);

/**
 * AhciDeviceGet
 * * Convert a system device identifier to a AhciDevice_t structure.
 */
__EXTERN AhciDevice_t*
AhciDeviceGet(
    _In_ UUId_t DiskId);

/**
 * AhciDeviceQueueTransaction
 * @param Device      [In] The device that should handle the transaction.
 * @param Transaction [In] The transaction that should get queued up.
 */
__EXTERN OsStatus_t
AhciDeviceQueueTransaction(
    _In_ AhciDevice_t*      Device,
    _In_ AhciTransaction_t* Transaction);

/** 
 * AhciDeviceCancelTransaction
 */
__EXTERN OsStatus_t
AhciManagerCancelTransaction(
    _In_ AhciDevice_t* Device,
    _In_ UUId_t        TransactionId);

/** 
 * AhciTransactionCreate
 */
__EXTERN OsStatus_t
AhciTransactionCreate(
    _In_ AhciDevice_t*         Device,
    _In_ MRemoteCallAddress_t* Address,
    _In_ StorageOperation_t*   Operation);

#endif //!_AHCI_MANAGER_H_
