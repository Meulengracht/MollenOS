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

static Collection_t Devices           = COLLECTION_INIT(KeyId);
static UUId_t       DeviceIdGenerator = 0;
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
    return CollectionClear(&Devices);
}

size_t
AhciManagerGetFrameSize(void)
{
    return FrameSize;
}

OsStatus_t
AhciManagerRegisterDevice(
    _In_ AhciDevice_t* Device)
{
    Device->Descriptor.Device = DeviceIdGenerator++;
    Device->Header.Key.Value.Id = Device->Descriptor.Device;

    CollectionAppend(&Devices, &Device->Header);
    return RegisterStorage(Device->Descriptor.Device, Device->Descriptor.Flags);
}

OsStatus_t
AhciManagerUnregisterDevice(
    _In_ AhciDevice_t* Device)
{
    OsStatus_t Status = UnregisterStorage(Device->Header.Key.Value.Id, __STORAGE_FORCED_REMOVE);
    CollectionRemoveByNode(&Devices, &Device->Header);
    return Status;
}

AhciDevice_t*
AhciManagerGetDevice(
    _In_ UUId_t Disk)
{
    DataKey_t Key = { .Value.Id = Disk };
    return CollectionGetDataByKey(&Devices, Key, 0);
}
