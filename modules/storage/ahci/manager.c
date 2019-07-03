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
//#define __TRACE

#include <os/mollenos.h>
#include <ddk/services/file.h>
#include <ddk/utils.h>
#include "manager.h"
#include <stdlib.h>

static Collection_t Devices           = COLLECTION_INIT(KeyId);
static UUId_t       DeviceIdGenerator = 0;
static size_t       FrameSize;

static void
FlipStringBuffer(
    _In_ uint8_t* Buffer,
    _In_ size_t   Length)
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

OsStatus_t
AhciManagerInitialize(void)
{
    SystemDescriptor_t Descriptor;
    OsStatus_t         Status;

    TRACE("AhciManagerInitialize()");
    Status = SystemQuery(&Descriptor);
    if (Status == OsSuccess) {
        FrameSize = Descriptor.PageSizeBytes;
    }
    return Status;
}

OsStatus_t
AhciManagerDestroy(void)
{
    TRACE("AhciManagerDestroy()");
    return CollectionClear(&Devices);
}

size_t
AhciManagerGetFrameSize(void)
{
    return FrameSize;
}

AhciDevice_t*
AhciManagerGetDevice(
    _In_ UUId_t DeviceId)
{
    DataKey_t Key = { .Value.Id = DeviceId };
    return CollectionGetDataByKey(&Devices, Key, 0);
}

static AhciDevice_t*
CreateInitialDevice(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port,
    _In_ DeviceType_t      Type)
{
    AhciDevice_t* Device;

    TRACE("CreateInitialDevice(Controller %i, Port %i)",
        Controller->Device.Id, Port->Id);

    Device = (AhciDevice_t*)malloc(sizeof(AhciDevice_t));
    if (!Device) {
        return NULL;
    }

    memset(Device, 0, sizeof(AhciDevice_t));
    Device->Controller     = Controller;
    Device->Port           = Port;
    Device->Type           = Type;
    
    // Allocate a new device id
    Device->Descriptor.Device   = DeviceIdGenerator++;
    Device->Header.Key.Value.Id = Device->Descriptor.Device;
    
    // Set initial addressing mode + initial values
    Device->AddressingMode = 1;
    Device->SectorSize     = 512;
    return Device;
}


OsStatus_t
AhciManagerRegisterDevice(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port,
    _In_ uint32_t          Signature)
{
    AhciDevice_t* Device;
    DeviceType_t  Type;
    OsStatus_t    Status;
    
    switch (Signature) {
        case SATA_SIGNATURE_ATA: {
            Type = DeviceATA;
        } break;
        case SATA_SIGNATURE_ATAPI: {
            Type = DeviceATAPI;
        } break;
        
        // We don't support these device types yet
        case SATA_SIGNATURE_SEMB:
        case SATA_SIGNATURE_PM:
        default: {
            WARNING("AHCI::Unsupported device type 0x%x on port %i",
                Signature, Port->Id);
            return OsNotSupported;
        };
    }
    
    Device = CreateInitialDevice(Controller, Port, Type);
    if (!Device) {
        return OsOutOfMemory;
    }
    
    Status = AhciTransactionControlCreate(Device, AtaPIOIdentifyDevice);
    if (Status != OsSuccess) {
        free(Device);
        return Status;
    }
    return CollectionAppend(&Devices, &Device->Header);
}

OsStatus_t
AhciManagerUnregisterDevice(
    _In_ AhciController_t* Controller, 
    _In_ AhciPort_t*       Port)
{
    AhciDevice_t* Device = NULL;
    OsStatus_t    Status;
    
    // Lookup device based on controller/port
    foreach(Node, &Devices) {
        AhciDevice_t* _Device = (AhciDevice_t*)Node;
        if (_Device->Port == Port) {
            Device = _Device;
            break;
        }
    }
    assert(Device != NULL);
    
    Status = UnregisterStorage(Device->Header.Key.Value.Id, __STORAGE_FORCED_REMOVE);
    CollectionRemoveByNode(&Devices, &Device->Header);
    return Status;
}

static void
HandleIdentifyCommand(
    _In_ AhciDevice_t* Device)
{
    ATAIdentify_t* DeviceInformation = 
        (ATAIdentify_t*)Device->Port->InternalBuffer.buffer;

    // Flip the data in the strings as it's inverted
    FlipStringBuffer(DeviceInformation->SerialNo, 20);
    FlipStringBuffer(DeviceInformation->ModelNo, 40);
    FlipStringBuffer(DeviceInformation->FWRevision, 8);

    TRACE("HandleIdentifyCommand(%s)", &DeviceInformation->ModelNo[0]);

    // Set capabilities
    if (DeviceInformation->Capabilities0 & (1 << 0)) {
        Device->HasDMAEngine = 1;
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
        Device->SectorCount = DeviceInformation->SectorCountLBA48;
    }
    else {
        Device->SectorCount = DeviceInformation->SectorCountLBA28;
    }

    // At this point the ahcidisk structure is filled
    // and we can continue to fill out the descriptor
    memset(&Device->Descriptor, 0, sizeof(StorageDescriptor_t));
    Device->Descriptor.Driver      = UUID_INVALID;
    Device->Descriptor.Flags       = 0;
    Device->Descriptor.SectorCount = Device->SectorCount;
    Device->Descriptor.SectorSize  = Device->SectorSize;

    // Copy string data
    memcpy(&Device->Descriptor.Model[0], (const void*)&DeviceInformation->ModelNo[0], 40);
    memcpy(&Device->Descriptor.Serial[0], (const void*)&DeviceInformation->SerialNo[0], 20);
    (void)RegisterStorage(Device->Descriptor.Device, Device->Descriptor.Flags);
}

void
AhciManagerHandleControlResponse(
    _In_ AhciPort_t*        Port,
    _In_ AhciTransaction_t* Transaction)
{
    AhciDevice_t* Device = NULL;
    
    // Lookup device based on controller/port
    foreach(Node, &Devices) {
        AhciDevice_t* _Device = (AhciDevice_t*)Node;
        if (_Device->Port == Port) {
            Device = _Device;
            break;
        }
    }
    assert(Device != NULL);
    
    switch (Transaction->Command) {
        case AtaPIOIdentifyDevice: {
            HandleIdentifyCommand(Device);
        } break;
        
        default: {
            WARNING("Unsupported ATA command 0x%x", Transaction->Command);
        } break;
    }
}
