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
 * Advanced Host Controller Interface Driver
 * TODO:
 *    - Port Multiplier Support
 *    - Power Management
 */
//#define __TRACE

#include <ddk/services/file.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "manager.h"

static Collection_t Disks           = COLLECTION_INIT(KeyId);
static UUId_t       DiskIdGenerator = 0;

/* AHCIStringFlip 
 * Flips a string returned by an ahci command so it's readable */
void
AhciStringFlip(
    _In_ uint8_t*           Buffer,
    _In_ size_t             Length)
{
    size_t StringPairs = Length / 2;
    size_t i;

    // Iterate pairs in string, and swap
    for (i = 0; i < StringPairs; i++) {
        uint8_t TempChar    = Buffer[i * 2];
        Buffer[i * 2]       = Buffer[i * 2 + 1];
        Buffer[i * 2 + 1]   = TempChar;
    }

    // Zero terminate by trimming trailing spaces
    for (i = (Length - 1); i > 0; i--) {
        if (Buffer[i] != ' ' && Buffer[i] != '\0') {
            i += 1;
            if (i < Length) {
                Buffer[i] = '\0';
            }
            break;
        }
    }
}

/* AhciManagerInitialize
 * Initializes the ahci manager that keeps track of
 * all controllers and all attached devices */
OsStatus_t
AhciManagerInitialize(void)
{
    TRACE("AhciManagerInitialize()");
    return OsSuccess;
}

/* AhciManagerDestroy
 * Cleans up the manager and releases resources allocated */
OsStatus_t
AhciManagerDestroy(void)
{
    TRACE("AhciManagerDestroy()");

    foreach(dNode, &Disks) {
        AhciDevice_t* Device = (AhciDevice_t*)dNode->Data;
        UnregisterStorage(Device->Descriptor.Device, __STORAGE_FORCED_REMOVE);
        DestroyBuffer(Device->Buffer);
        free(Device);
    }
    return CollectionClear(&Disks);
}

OsStatus_t
AhciManagerCreateDevice(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    AhciTransaction_t* Transaction;
    DmaBuffer_t*       Buffer;
    AhciDevice_t*      Device;
    reg32_t            Signature = ReadVolatile32(&Port->Registers->Signature);

    // First of all, is this a port multiplier? 
    // because then we should really enumerate it
    if (Signature == SATA_SIGNATURE_PM || Signature == SATA_SIGNATURE_SEMB) {
        WARNING("AHCI::Unsupported device type 0x%x on port %i",
            Signature, Port->Id);
        return OsError;
    }
    TRACE("AhciManagerCreateDevice(Controller %i, Port %i)",
        Controller->Device.Id, Port->Id);

    // Allocate data-structures
    Transaction = (AhciTransaction_t*)malloc(sizeof(AhciTransaction_t));
    Device      = (AhciDevice_t*)malloc(sizeof(AhciDevice_t));
    Buffer      = CreateBuffer(UUID_INVALID, sizeof(ATAIdentify_t));
    if (!Transaction || !Device) {
        return OsOutOfMemory;
    }

    memset(Transaction, 0, sizeof(AhciTransaction_t));
    memset(Device, 0, sizeof(AhciDevice_t));

    Device->Controller     = Controller;
    Device->Port           = Port;
    Device->Buffer         = Buffer;
    Device->Index          = 0;
    Device->AddressingMode = 1;
    Device->SectorSize     = sizeof(ATAIdentify_t);
    Device->Type           = (Signature == SATA_SIGNATURE_ATAPI) ? 1 : 0;

    Transaction->ResponseAddress.Thread = UUID_INVALID;
    Transaction->Address        = GetBufferDma(Buffer);
    Transaction->SectorCount    = 1;
    Transaction->Device         = Device;
    return AhciCommandRegisterFIS(Transaction, AtaPIOIdentifyDevice, 0, 0, 0);
}

OsStatus_t
AhciManagerCreateDeviceCallback(
    _In_ AhciDevice_t* Device)
{
    ATAIdentify_t* DeviceInformation;
    DataKey_t      Key;

    DeviceInformation = (ATAIdentify_t*)GetBufferDataPointer(Device->Buffer);

    // Flip the data in the strings as it's inverted
    AhciStringFlip(DeviceInformation->SerialNo, 20);
    AhciStringFlip(DeviceInformation->ModelNo, 40);
    AhciStringFlip(DeviceInformation->FWRevision, 8);

    TRACE("AhciManagerCreateDeviceCallback(%s)", &DeviceInformation->ModelNo[0]);

    // Set capabilities
    if (DeviceInformation->Capabilities0 & (1 << 0)) {
        Device->UseDMA = 1;
    }

    // Check addressing mode supported
    // Check that LBA is supported
    if (DeviceInformation->Capabilities0 & (1 << 1)) {
        Device->AddressingMode = 1; // LBA28
        if (DeviceInformation->CommandSetSupport1 & (1 << 10)) {
            Device->AddressingMode = 2; // LBA48
        }
    }
    else {
        Device->AddressingMode = 0; // CHS
    }

    // Calculate sector size if neccessary
    if (DeviceInformation->SectorSize & (1 << 12)) {
        Device->SectorSize = DeviceInformation->WordsPerLogicalSector * 2;
    }
    else {
        Device->SectorSize = 512;
    }

    // Calculate sector count per physical sector
    if (DeviceInformation->SectorSize & (1 << 13)) {
        Device->SectorSize *= (DeviceInformation->SectorSize & 0xF);
    }

    // Now, get the number of sectors for this particular disk
    if (DeviceInformation->SectorCountLBA48 != 0) {
        Device->SectorsLBA = DeviceInformation->SectorCountLBA48;
    }
    else {
        Device->SectorsLBA = DeviceInformation->SectorCountLBA28;
    }

    // At this point the ahcidisk structure is filled
    // and we can continue to fill out the descriptor
    memset(&Device->Descriptor, 0, sizeof(StorageDescriptor_t));
    Device->Descriptor.Driver      = UUID_INVALID;
    Device->Descriptor.Device      = DiskIdGenerator++;
    Device->Descriptor.Flags       = 0;
    Device->Descriptor.SectorCount = Device->SectorsLBA;
    Device->Descriptor.SectorSize  = Device->SectorSize;

    // Copy string data
    memcpy(&Device->Descriptor.Model[0], (const void*)&DeviceInformation->ModelNo[0], 40);
    memcpy(&Device->Descriptor.Serial[0], (const void*)&DeviceInformation->SerialNo[0], 20);

    Key.Value.Id = Device->Descriptor.Device;
    CollectionAppend(&Disks, CollectionCreateNode(Key, Device));
    return RegisterStorage(Device->Descriptor.Device, Device->Descriptor.Flags);
}

OsStatus_t
AhciManagerRemoveDevice(
    _In_ AhciController_t* Controller,
    _In_ AhciPort_t*       Port)
{
    CollectionItem_t* dNode;
    AhciDevice_t*     Device;
    DataKey_t         Key = { .Value.Id = UUID_INVALID };

    TRACE("AhciManagerRemoveDevice(Controller %i, Port %i)",
        Controller->Device.Id, Port->Id);

    _foreach(dNode, &Disks) {
        Device = (AhciDevice_t*)dNode->Data;
        if (Device->Port == Port && Device->Controller == Controller) {
            Key.Value = dNode->Key.Value;
            break;
        }
    }
    if (Key.Value.Id == UUID_INVALID) {
        return OsError;
    }

    CollectionRemoveByKey(&Disks, Key);
    DestroyBuffer(Device->Buffer);
    free(Device);
    return UnregisterStorage(Key.Value.Id, __STORAGE_FORCED_REMOVE);
}

AhciDevice_t*
AhciManagerGetDevice(
    _In_ UUId_t Disk)
{
    DataKey_t Key = { .Value.Id = Disk };
    return CollectionGetDataByKey(&Disks, Key, 0);
}
