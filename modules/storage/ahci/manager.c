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

#include <ddk/services/file.h>
#include <os/mollenos.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include "manager.h"

static Collection_t Disks           = COLLECTION_INIT(KeyId);
static UUId_t       DiskIdGenerator = 0;
static size_t       FrameSize;

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

    foreach(dNode, &Disks) {
        AhciDevice_t* Device = (AhciDevice_t*)dNode->Data;
        UnregisterStorage(Device->Descriptor.Device, __STORAGE_FORCED_REMOVE);
        DestroyBuffer(Device->Buffer);
        free(Device);
    }
    return CollectionClear(&Disks);
}

size_t
AhciManagerGetFrameSize(void)
{
    return FrameSize;
}

OsStatus_t
AhciManagerRegisterDevice(AhciDevice_t*)
{

}

OsStatus_t
AhciManagerUnregisterDevice(AhciDevice_t*)
{

}

AhciDevice_t*
AhciManagerGetDevice(
    _In_ UUId_t Disk)
{
    DataKey_t Key = { .Value.Id = Disk };
    return CollectionGetDataByKey(&Disks, Key, 0);
}
