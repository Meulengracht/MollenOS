/**
 * MollenOS
 *
 * Copyright 2026, Philip Meulengracht
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
 * Integrated Drive Electronics Driver
 */

#include "ide.h"

#include <commands.h>
#include <ddk/convert.h>
#include <ddk/io.h>
#include <ddk/utils.h>
#include <internal/_utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include <os/device.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctt_storage_service_server.h>
#include <gracht/link/vali.h>
#include <sys_storage_service_client.h>

extern int __crt_get_server_iod(void);

static list_t g_devices = LIST_INIT;
static uuid_t g_nextDeviceId = 0;

static inline uint8_t __ReadStatus(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex);
static inline void __WriteControl(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex,
    _In_ uint8_t          value);
static inline void __SelectDevice(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex);

static void
__FlipString(
    _In_ uint8_t* buffer,
    _In_ size_t   length)
{
    size_t pairs = length / 2;
    size_t i;

    for (i = 0; i < pairs; i++) {
        uint8_t temp      = buffer[i * 2];
        buffer[i * 2]     = buffer[i * 2 + 1];
        buffer[i * 2 + 1] = temp;
    }

    for (i = length - 1; i > 0; i--) {
        if (buffer[i] != ' ' && buffer[i] != '\0') {
            if (i + 1 < length) {
                buffer[i + 1] = '\0';
            }
            break;
        }
    }

    if (length > 0) {
        buffer[length - 1] = '\0';
    }
}

static inline void
__SetChannelIo(
    _In_ IdeController_t* controller,
    _In_ int              channel)
{
    if (controller == NULL || controller->Device == NULL) {
        return;
    }

    if (channel < 0 || channel >= IDE_CHANNEL_COUNT) {
        controller->CommandIo = NULL;
        controller->ControlIo = NULL;
        return;
    }

    controller->CommandIo = &controller->Device->IoSpaces[channel * 2];
    controller->ControlIo = &controller->Device->IoSpaces[(channel * 2) + 1];
}

static inline uint8_t
__WaitForNotBusy(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              timeout)
{
    int i;

    __SetChannelIo(controller, channel);
    for (i = 0; i < timeout; i++) {
        uint8_t status = __ReadStatus(controller, channel, 0);
        if ((status & IDE_STATUS_BSY) == 0) {
            return status;
        }
    }

    return __ReadStatus(controller, channel, 0);
}

static inline void
__IssueCommand(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex,
    _In_ uint8_t          command)
{
    __SetChannelIo(controller, channel);
    __SelectDevice(controller, channel, deviceIndex);
    WriteDeviceIo(controller->CommandIo, IDE_REGISTER_COMMAND, command, 1);
}

static void
__RegisterStorage(
    _In_ uuid_t protocolServerId,
    _In_ uuid_t deviceId,
    _In_ unsigned int flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    (void)sys_storage_register(GetGrachtClient(), &msg.base, protocolServerId, deviceId, flags);
}

static void
__UnregisterStorage(
    _In_ uuid_t deviceId,
    _In_ uint8_t forced)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    (void)sys_storage_unregister(GetGrachtClient(), &msg.base, deviceId, forced);
}

static inline uint8_t
__ReadStatus(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex)
{
    (void)deviceIndex;
    __SetChannelIo(controller, channel);
    if (controller == NULL || controller->CommandIo == NULL) {
        return 0;
    }
    return (uint8_t)ReadDeviceIo(controller->CommandIo, IDE_REGISTER_STATUS, 1);
}

static inline void
__WriteControl(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex,
    _In_ uint8_t          value)
{
    (void)deviceIndex;
    __SetChannelIo(controller, channel);
    if (controller == NULL || controller->ControlIo == NULL) {
        return;
    }
    WriteDeviceIo(controller->ControlIo, 0, value, 1);
}

static inline void
__SelectDevice(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex)
{
    uint8_t value = IDE_DEVICE_LBA;

    __SetChannelIo(controller, channel);
    if (controller == NULL || controller->CommandIo == NULL) {
        return;
    }

    if (deviceIndex == 1) {
        value |= IDE_DEVICE_SLAVE;
    }

    WriteDeviceIo(controller->CommandIo, IDE_REGISTER_DEVICE_SELECT, value, 1);
}

static inline int
__IsDevicePresent(
    _In_ IdeController_t* controller,
    _In_ int              channel,
    _In_ int              deviceIndex)
{
    uint8_t status;

    __WriteControl(controller, channel, deviceIndex, 0);
    __SelectDevice(controller, channel, deviceIndex);
    for (int i = 0; i < 1000; i++) {
        status = __ReadStatus(controller, channel, deviceIndex);
        if (status != 0xFF && status != 0x00) {
            return 1;
        }
    }

    return 0;
}

static oserr_t
__ReadPioSectors(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ void*        buffer)
{
    IdeController_t* controller = device->Controller;
    size_t           i;

    if (!controller || !buffer) {
        return OS_EINVALPARAMS;
    }

    if (sector > 0x0FFFFFFF || count == 0 || count > 256) {
        return OS_ENOTSUPPORTED;
    }

    __SetChannelIo(controller, device->Channel);
    __SelectDevice(controller, device->Channel, device->Device);
    __WriteControl(controller, device->Channel, device->Device, 0);

    for (i = 0; i < count; i++) {
        uint8_t  data[512];
        uint16_t baseSector = (uint16_t)(sector + i);
        size_t   wordIndex;
        uint8_t  status;

        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_SECTOR_COUNT, 1, 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_LOW, (uint8_t)baseSector, 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_MID, (uint8_t)(baseSector >> 8), 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_HIGH, (uint8_t)(baseSector >> 16), 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_DEVICE_SELECT,
                      IDE_DEVICE_LBA | ((device->Device == 1) ? IDE_DEVICE_SLAVE : 0) |
                          ((baseSector >> 24) & 0x0F),
                      1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_COMMAND, IDE_COMMAND_READ_SECTORS, 1);

        status = __WaitForNotBusy(controller, device->Channel, 2000);
        if ((status & IDE_STATUS_ERR) != 0) {
            return OS_EDEVFAULT;
        }
        if ((status & IDE_STATUS_DRQ) == 0) {
            return OS_ETIMEOUT;
        }

        for (wordIndex = 0; wordIndex < 256; wordIndex++) {
            uint16_t word = (uint16_t)ReadDeviceIo(controller->CommandIo, IDE_REGISTER_DATA, 2);
            data[wordIndex * 2] = (uint8_t)(word & 0xFF);
            data[wordIndex * 2 + 1] = (uint8_t)((word >> 8) & 0xFF);
        }

        memcpy((uint8_t*)buffer + (i * 512), data, 512);
    }

    return OS_EOK;
}

static oserr_t
__WritePioSectors(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ const void*  buffer)
{
    IdeController_t* controller = device->Controller;
    size_t           i;

    if (!controller || !buffer) {
        return OS_EINVALPARAMS;
    }

    if (sector > 0x0FFFFFFF || count == 0 || count > 256) {
        return OS_ENOTSUPPORTED;
    }

    __SetChannelIo(controller, device->Channel);
    __SelectDevice(controller, device->Channel, device->Device);
    __WriteControl(controller, device->Channel, device->Device, 0);

    for (i = 0; i < count; i++) {
        const uint8_t* sectorData = (const uint8_t*)buffer + (i * 512);
        uint16_t       baseSector = (uint16_t)(sector + i);
        uint8_t        status;
        size_t         wordIndex;

        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_SECTOR_COUNT, 1, 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_LOW, (uint8_t)baseSector, 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_MID, (uint8_t)(baseSector >> 8), 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_LBA_HIGH, (uint8_t)(baseSector >> 16), 1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_DEVICE_SELECT,
                      IDE_DEVICE_LBA | ((device->Device == 1) ? IDE_DEVICE_SLAVE : 0) |
                          ((baseSector >> 24) & 0x0F),
                      1);
        WriteDeviceIo(controller->CommandIo, IDE_REGISTER_COMMAND, IDE_COMMAND_WRITE_SECTORS, 1);

        status = __WaitForNotBusy(controller, device->Channel, 2000);
        if ((status & IDE_STATUS_ERR) != 0) {
            return OS_EDEVFAULT;
        }
        if ((status & IDE_STATUS_DRQ) == 0) {
            return OS_ETIMEOUT;
        }

        for (wordIndex = 0; wordIndex < 256; wordIndex++) {
            uint16_t word = (uint16_t)sectorData[wordIndex * 2] |
                            ((uint16_t)sectorData[wordIndex * 2 + 1] << 8);
            WriteDeviceIo(controller->CommandIo, IDE_REGISTER_DATA, word, 2);
        }

        __WriteControl(controller, device->Channel, device->Device, 0);
        __WaitForNotBusy(controller, device->Channel, 2000);
    }

    return OS_EOK;
}

IdeController_t*
IdeControllerCreate(
    _In_ BusDevice_t* busDevice)
{
    IdeController_t* controller;
    int              i;

    if (!busDevice) {
        return NULL;
    }

    controller = (IdeController_t*)malloc(sizeof(IdeController_t));
    if (!controller) {
        return NULL;
    }

    memset(controller, 0, sizeof(IdeController_t));
    ELEMENT_INIT(&controller->Header, (void*)(uintptr_t)busDevice->Base.Id, controller);
    controller->Device = busDevice;

    for (i = 0; i < __DEVICEMANAGER_MAX_IOSPACES; i++) {
        if (busDevice->IoSpaces[i].Type != DeviceIoInvalid) {
            (void)AcquireDeviceIo(&busDevice->IoSpaces[i]);
        }
    }

    if (busDevice->IoSpaces[0].Type != DeviceIoInvalid) {
        controller->CommandIo = &busDevice->IoSpaces[0];
        controller->ControlIo = &busDevice->IoSpaces[1];
    }

    if (IdeControllerEnumerate(controller) != OS_EOK) {
        free(controller);
        return NULL;
    }

    return controller;
}

void
IdeControllerDestroy(
    _In_ IdeController_t* controller)
{
    int channel;
    int deviceIndex;

    if (!controller) {
        return;
    }

    for (channel = 0; channel < IDE_CHANNEL_COUNT; channel++) {
        for (deviceIndex = 0; deviceIndex < IDE_DEVICE_PER_CHANNEL; deviceIndex++) {
            IdeDevice_t* device = controller->Devices[channel][deviceIndex];
            if (!device) {
                continue;
            }
            __UnregisterStorage(device->Descriptor.DeviceID, 1);
            list_remove(&g_devices, &device->Header);
            free(device);
        }
    }

    free(controller);
}

IdeDevice_t*
IdeDeviceGet(
    _In_ uuid_t deviceId)
{
    return list_find_value(&g_devices, (void*)(uintptr_t)deviceId);
}

oserr_t
IdeControllerEnumerate(
    _In_ IdeController_t* controller)
{
    int channel;
    int deviceIndex;

    if (!controller) {
        return OS_EINVALPARAMS;
    }

    for (channel = 0; channel < IDE_CHANNEL_COUNT; channel++) {
        for (deviceIndex = 0; deviceIndex < IDE_DEVICE_PER_CHANNEL; deviceIndex++) {
            IdeDevice_t* device;

            __SetChannelIo(controller, channel);

            __WriteControl(controller, channel, deviceIndex, 0);
            __SelectDevice(controller, channel, deviceIndex);
            if (!__IsDevicePresent(controller, channel, deviceIndex)) {
                continue;
            }

            device = (IdeDevice_t*)calloc(1, sizeof(IdeDevice_t));
            if (!device) {
                return OS_EOOM;
            }

            device->Controller = controller;
            device->Channel = (uint8_t)channel;
            device->Device = (uint8_t)deviceIndex;
            device->Present = 1;
            device->SectorSize = 512;
            device->Descriptor.DeviceID = g_nextDeviceId++;
            device->Descriptor.DriverID = GetNativeHandle(__crt_get_server_iod());
            device->Descriptor.SectorSize = device->SectorSize;
            device->Descriptor.SectorCount = 0;
            device->Descriptor.Flags = 0;

            if (IdeDeviceIdentify(device) != OS_EOK) {
                free(device);
                continue;
            }

            device->Descriptor.Flags = 0;
            device->Descriptor.DriverID = GetNativeHandle(__crt_get_server_iod());

            ELEMENT_INIT(&device->Header, (void*)(uintptr_t)device->Descriptor.DeviceID, device);
            controller->Devices[channel][deviceIndex] = device;
            list_append(&g_devices, &device->Header);
            __RegisterStorage(GetNativeHandle(__crt_get_server_iod()), device->Descriptor.DeviceID, device->Descriptor.Flags);
        }
    }

    return OS_EOK;
}

oserr_t
IdeDeviceIdentify(
    _In_ IdeDevice_t* device)
{
    if (!device) {
        return OS_EINVALPARAMS;
    }

    if (device->Controller == NULL || device->Controller->CommandIo == NULL) {
        return OS_EUNKNOWN;
    }

    __SetChannelIo(device->Controller, device->Channel);
    __SelectDevice(device->Controller, device->Channel, device->Device);
    __WriteControl(device->Controller, device->Channel, device->Device, 0);

    WriteDeviceIo(device->Controller->CommandIo, IDE_REGISTER_COMMAND, IDE_COMMAND_IDENTIFY, 1);
    if (__WaitForNotBusy(device->Controller, device->Channel, 2000) == 0) {
        return OS_EDEVFAULT;
    }

    {
        ATAIdentify_t identify = { 0 };
        uint16_t*     words = (uint16_t*)&identify;
        size_t        i;

        for (i = 0; i < 256; i++) {
            words[i] = (uint16_t)ReadDeviceIo(device->Controller->CommandIo, IDE_REGISTER_DATA, 2);
        }

        __FlipString((uint8_t*)&identify.SerialNo[0], sizeof(identify.SerialNo));
        __FlipString((uint8_t*)&identify.ModelNo[0], sizeof(identify.ModelNo));
        __FlipString((uint8_t*)&identify.FWRevision[0], sizeof(identify.FWRevision));

        if (identify.Capabilities0 & (1 << 1)) {
            device->AddressingMode = 1;
            if (identify.CommandSetSupport1 & (1 << 10)) {
                device->AddressingMode = 2;
            }
        }
        else {
            device->AddressingMode = 0;
        }

        if (identify.SectorSize & (1 << 12)) {
            device->SectorSize = identify.WordsPerLogicalSector * 2;
        }
        else {
            device->SectorSize = 512;
        }

        if (identify.SectorSize & (1 << 13)) {
            device->SectorSize *= (identify.SectorSize & 0xF);
        }

        device->SectorCount = (identify.SectorCountLBA48 != 0) ? identify.SectorCountLBA48 : identify.SectorCountLBA28;

        memset(&device->Descriptor, 0, sizeof(device->Descriptor));
        device->Descriptor.DeviceID = device->Descriptor.DeviceID;
        device->Descriptor.DriverID = GetNativeHandle(__crt_get_server_iod());
        device->Descriptor.SectorSize = device->SectorSize;
        device->Descriptor.SectorCount = device->SectorCount;
        device->Descriptor.Flags = 0;

        snprintf(device->Descriptor.Model, sizeof(device->Descriptor.Model), "%.*s",
                 (int)sizeof(identify.ModelNo), identify.ModelNo);
        snprintf(device->Descriptor.Serial, sizeof(device->Descriptor.Serial), "%.*s",
                 (int)sizeof(identify.SerialNo), identify.SerialNo);
        if (strlen(device->Descriptor.Model) == 0) {
            snprintf(device->Descriptor.Model, sizeof(device->Descriptor.Model), "IDE Disk");
        }
    }

    device->Descriptor.SectorCount = 0;
    device->Descriptor.SectorSize = 512;
    device->AddressingMode = 1;

    return OS_EOK;
}

oserr_t
IdeDeviceRead(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ uuid_t       bufferHandle,
    _In_ size_t       offset)
{
    OSHandle_t buffer;
    void*      mapping;
    oserr_t     status;

    if (!device || bufferHandle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }

    status = SHMAttach(bufferHandle, &buffer);
    if (status != OS_EOK) {
        return status;
    }

    mapping = (void*)((uint8_t*)SHMBuffer(&buffer) + offset);
    status = __ReadPioSectors(device, sector, count, mapping);
    OSHandleDestroy(&buffer);
    return status;
}

oserr_t
IdeDeviceWrite(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ uuid_t       bufferHandle,
    _In_ size_t       offset)
{
    OSHandle_t buffer;
    void*      mapping;
    oserr_t     status;

    if (!device || bufferHandle == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }

    status = SHMAttach(bufferHandle, &buffer);
    if (status != OS_EOK) {
        return status;
    }

    mapping = (void*)((uint8_t*)SHMBuffer(&buffer) + offset);
    status = __WritePioSectors(device, sector, count, mapping);
    OSHandleDestroy(&buffer);
    return status;
}
