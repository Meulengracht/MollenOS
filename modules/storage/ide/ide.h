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
 * - Provides a minimal legacy-ATA / IDE controller implementation for devices
 *   exposed through the storage service contracts.
 */

#ifndef _IDE_H_
#define _IDE_H_

#include <ddk/busdevice.h>
#include <ddk/storage.h>
#include <ds/list.h>
#include <os/osdefs.h>

#define IDE_CHANNEL_COUNT      2
#define IDE_DEVICE_PER_CHANNEL 2

#define IDE_REGISTER_DATA          0x00
#define IDE_REGISTER_ERROR         0x01
#define IDE_REGISTER_FEATURES      0x01
#define IDE_REGISTER_SECTOR_COUNT  0x02
#define IDE_REGISTER_LBA_LOW       0x03
#define IDE_REGISTER_LBA_MID       0x04
#define IDE_REGISTER_LBA_HIGH      0x05
#define IDE_REGISTER_DEVICE_SELECT 0x06
#define IDE_REGISTER_COMMAND       0x07
#define IDE_REGISTER_STATUS        0x07
#define IDE_REGISTER_CONTROL       0x206
#define IDE_REGISTER_ALT_STATUS    0x206

#define IDE_COMMAND_IDENTIFY       0xEC
#define IDE_COMMAND_READ_SECTORS   0x20
#define IDE_COMMAND_WRITE_SECTORS  0x30
#define IDE_COMMAND_CACHE_FLUSH    0xE7

#define IDE_STATUS_BSY             0x80
#define IDE_STATUS_DRDY            0x40
#define IDE_STATUS_DF              0x20
#define IDE_STATUS_DSC             0x10
#define IDE_STATUS_DRQ             0x08
#define IDE_STATUS_CORR            0x04
#define IDE_STATUS_IDX             0x02
#define IDE_STATUS_ERR             0x01

#define IDE_DEVICE_LBA             0x40
#define IDE_DEVICE_SLAVE           0x10

/**
 * @brief Represents a single IDE device discovered on a controller channel.
 */
typedef struct IdeDevice {
    element_t           Header;
    StorageDescriptor_t Descriptor;
    struct IdeController* Controller;
    uint8_t             Channel;
    uint8_t             Device;
    uint8_t             Present;
    uint8_t             AddressingMode;
    size_t              SectorSize;
    uint64_t            SectorCount;
} IdeDevice_t;

/**
 * @brief Represents a single legacy IDE controller and its attached channels.
 */
typedef struct IdeController {
    element_t Header;
    BusDevice_t* Device;
    DeviceIo_t* CommandIo;
    DeviceIo_t* ControlIo;
    IdeDevice_t* Devices[IDE_CHANNEL_COUNT][IDE_DEVICE_PER_CHANNEL];
} IdeController_t;

/**
 * @brief Creates a controller instance for a matched bus device.
 *
 * The controller scans the attached IDE channels, identifies any present ATA
 * devices and keeps the information in the controller-local device table.
 *
 * @param busDevice [In] Device exposed by the device-manager.
 * @return Newly created controller instance or NULL on failure.
 */
__EXTERN IdeController_t*
IdeControllerCreate(
    _In_ BusDevice_t* busDevice);

/**
 * @brief Destroys a controller and releases all associated device resources.
 *
 * @param controller [In] Controller to destroy.
 */
__EXTERN void
IdeControllerDestroy(
    _In_ IdeController_t* controller);

/**
 * @brief Retrieves an IDE device by its service-assigned device identifier.
 *
 * @param deviceId [In] Device identifier assigned through the storage service.
 * @return Matching IDE device or NULL if not found.
 */
__EXTERN IdeDevice_t*
IdeDeviceGet(
    _In_ uuid_t deviceId);

/**
 * @brief Scans the available IDE channels and identifies attached ATAPI or ATA devices.
 *
 * @param controller [In] Controller to enumerate.
 * @return OS_EOK on success, or a driver-specific error code otherwise.
 */
__EXTERN oserr_t
IdeControllerEnumerate(
    _In_ IdeController_t* controller);

/**
 * @brief Performs an ATA identify cycle for the provided IDE device.
 *
 * @param device [In] Device to identify.
 * @return OS_EOK on success, or an error code on failure.
 */
__EXTERN oserr_t
IdeDeviceIdentify(
    _In_ IdeDevice_t* device);

/**
 * @brief Reads one or more 512-byte sectors from the given device.
 *
 * The implementation keeps the transport primitive intentionally conservative and
 * reports unsupported requests when a request cannot be completed using the host
 * registration model.
 *
 * @param device [In] Target IDE device.
 * @param sector [In] Starting sector number.
 * @param count [In] Number of sectors to read.
 * @param bufferHandle [In] Shared memory handle backing the read buffer.
 * @param offset [In] Offset into the shared memory buffer.
 * @return OS_EOK on success, or OS_ENOTSUPPORTED when the transfer cannot be handled.
 */
__EXTERN oserr_t
IdeDeviceRead(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ uuid_t       bufferHandle,
    _In_ size_t       offset);

/**
 * @brief Writes one or more sectors to the given device.
 *
 * @param device [In] Target IDE device.
 * @param sector [In] Starting sector number.
 * @param count [In] Number of sectors to write.
 * @param bufferHandle [In] Shared memory handle containing the write data.
 * @param offset [In] Offset into the shared memory buffer.
 * @return OS_EOK on success, or OS_ENOTSUPPORTED when the transfer cannot be handled.
 */
__EXTERN oserr_t
IdeDeviceWrite(
    _In_ IdeDevice_t* device,
    _In_ uint64_t     sector,
    _In_ size_t       count,
    _In_ uuid_t       bufferHandle,
    _In_ size_t       offset);

#endif //!_IDE_H_
