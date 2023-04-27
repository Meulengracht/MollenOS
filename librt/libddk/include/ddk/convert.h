/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * Gracht types conversion methods
 * Implements conversion methods to convert between formats used in gracht services
 * and OS types.
 */

#ifndef __DDK_CONVERT_H__
#define __DDK_CONVERT_H__

#include <ddk/storage.h>
#include <ddk/device.h>
#include <ddk/usbdevice.h>
#include <ddk/busdevice.h>
#include <ddk/filesystem.h>

#include <os/types/file.h>
#include <os/types/storage.h>
#include <os/types/process.h>

#include <stdlib.h>

#include <sys_device_service.h>
#include <sys_file_service.h>
#include <sys_process_service.h>
#include <ctt_filesystem_service.h>
#include <ctt_usbhost_service.h>

static void from_sys_timestamp(struct sys_timestamp* in, OSTimestamp_t* out)
{
    out->Seconds = in->tv_sec;
    out->Nanoseconds = in->tv_nsec;
}

static void to_sys_timestamp(OSTimestamp_t* in, struct sys_timestamp* out)
{
    out->tv_sec = in->Seconds;
    out->tv_nsec = in->Nanoseconds;
}

static void from_sys_disk_descriptor(struct sys_disk_descriptor* in, OSStorageDescriptor_t* out)
{
    size_t len = strnlen(in->serial, sizeof(out->SerialNumber) - 1);

    out->Id = 0;
    out->Flags = (unsigned int)in->flags;
    out->SectorSize = in->geometry.sector_size;
    out->SectorsTotal.QuadPart = in->geometry.sectors_total;

    memcpy(&out->SerialNumber[0], in->serial, len);
    out->SerialNumber[len] = 0;
}

static void from_sys_disk_descriptor_dkk(struct sys_disk_descriptor* in, StorageDescriptor_t* out)
{
    size_t serialLen = strnlen(in->serial, sizeof(out->Serial) - 1);
    size_t modelLen  = strnlen(in->model, sizeof(out->Model) - 1);

    out->DeviceID = in->device_id;
    out->DriverID = in->driver_id;
    out->Flags = (unsigned int)in->flags;
    out->SectorSize = in->geometry.sector_size;
    out->SectorCount = in->geometry.sectors_total;
    out->LUNCount = in->geometry.lun_count;
    out->SectorsPerCylinder = in->geometry.sectors_per_cylinder;

    memcpy(&out->Serial[0], in->serial, serialLen);
    out->Serial[serialLen] = 0;
    memcpy(&out->Model[0], in->model, modelLen);
    out->Model[modelLen] = 0;
}

static void to_sys_disk_descriptor_dkk(StorageDescriptor_t* in, struct sys_disk_descriptor* out)
{
    out->device_id = in->DeviceID;
    out->driver_id = in->DriverID;
    out->flags = (enum sys_storage_flags)in->Flags;
    out->geometry.sector_size = in->SectorSize;
    out->geometry.sectors_total = in->SectorCount;
    out->geometry.lun_count = in->LUNCount;
    out->geometry.sectors_per_cylinder = in->SectorsPerCylinder;
    out->serial = &in->Serial[0];
    out->model = &in->Model[0];
}

static void from_sys_filesystem_descriptor(struct sys_filesystem_descriptor* in, OSFileSystemDescriptor_t* out)
{
    size_t len = strnlen(in->serial, sizeof(out->SerialNumber) - 1);

    out->Id = in->id;
    out->Flags = in->flags;
    out->BlockSize = in->block_size;
    out->BlocksPerSegment = in->blocks_per_segment;
    out->MaxFilenameLength = in->max_filename_length;
    out->SegmentsFree.QuadPart = in->segments_free;
    out->SegmentsTotal.QuadPart = in->segments_total;

    memcpy(&out->SerialNumber[0], in->serial, len);
    out->SerialNumber[len] = 0;
}

static void from_sys_file_descriptor(struct sys_file_descriptor* in, OSFileDescriptor_t* out)
{
    out->Id = in->id;
    out->StorageId = in->storageId;
    out->Flags = (unsigned int)in->flags;
    out->Permissions = (unsigned int)in->permissions;
    out->Size.QuadPart = in->size;

    from_sys_timestamp(&in->created, &out->CreatedAt);
    from_sys_timestamp(&in->accessed, &out->AccessedAt);
    from_sys_timestamp(&in->modified, &out->ModifiedAt);
}

static void to_sys_file_descriptor(OSFileDescriptor_t* in, struct sys_file_descriptor* out)
{
    out->id = in->Id;
    out->storageId = in->StorageId;
    out->flags = (enum sys_file_flags)in->Flags;
    out->permissions = (enum sys_file_permissions)in->Permissions;
    out->size = in->Size.QuadPart;

    to_sys_timestamp(&in->CreatedAt, &out->created);
    to_sys_timestamp(&in->AccessedAt, &out->accessed);
    to_sys_timestamp(&in->ModifiedAt, &out->modified);
}

static char* to_protocol_string(const char* in)
{
    if (!in || strlen(in) == 0) {
        return NULL;
    }
    return strdup(in);
}

static void to_sys_device_identification(DeviceIdentification_t* in, struct sys_device_identification* out)
{
    out->description  = to_protocol_string(in->Description);
    out->manufacturer = to_protocol_string(in->Manufacturer);
    out->product      = to_protocol_string(in->Product);
    out->revision     = to_protocol_string(in->Revision);
    out->serial       = to_protocol_string(in->Serial);
}

static void to_sys_device_base(Device_t* in, struct sys_device_base* out)
{
    out->id = in->Id;
    out->parent_id = in->ParentId;
    out->identification.vendor_id = in->VendorId;
    out->identification.product_id = in->ProductId;
    out->identification.class = in->Class;
    out->identification.subclass = in->Subclass;
    to_sys_device_identification(&in->Identification, &out->identification);
}

static void to_sys_bus_io(DeviceIo_t* in, struct sys_bus_io* out)
{
    out->id = in->Id;
    switch (in->Type) {
        case DeviceIoMemoryBased: {
            sys_bus_io_access_set_bus_io_memory(out, &(struct sys_bus_io_memory) {
                .physical_base = in->Access.Memory.PhysicalBase,
                .virtual_base  = in->Access.Memory.VirtualBase,
                .length        = in->Access.Memory.Length
            });
        } break;
        case DeviceIoPortBased: {
            sys_bus_io_access_set_bus_io_port(out, &(struct sys_bus_io_port) {
                .base = in->Access.Port.Base,
                .length = in->Access.Port.Length
            });
        } break;
        case DeviceIoPinBased: {
            sys_bus_io_access_set_bus_io_pin(out, &(struct sys_bus_io_pin) {
                    .port = in->Access.Pin.Port,
                    .pin = in->Access.Pin.Pin
            });
        } break;
        default: break;
    }
}

static void to_sys_device_bus(BusDevice_t* in, struct sys_device_bus* out)
{
    out->id = in->Base.Id;
    out->parent_id = in->Base.ParentId;
    out->identification.vendor_id = in->Base.VendorId;
    out->identification.product_id = in->Base.ProductId;
    out->identification.class = in->Base.Class;
    out->identification.subclass = in->Base.Subclass;
    to_sys_device_identification(&in->Base.Identification, &out->identification);

    out->irq_line = in->InterruptLine;
    out->irq_pin = in->InterruptPin;
    out->acpi_conform_flags = in->InterruptAcpiConform;
    out->segment = in->Segment;
    out->bus = in->Bus;
    out->slot = in->Slot;
    out->function = in->Function;

    sys_device_bus_ios_add(out, __DEVICEMANAGER_MAX_IOSPACES);
    for (int __i = 0; __i < __DEVICEMANAGER_MAX_IOSPACES; __i++) {
        struct sys_bus_io* io = sys_device_bus_ios_get(out, __i);
        to_sys_bus_io(&in->IoSpaces[__i], io);
    }
}

static void to_sys_device_usb(UsbDevice_t* in, struct sys_device_usb* out)
{
    out->id                        = in->Base.Id;
    out->parent_id                 = in->Base.ParentId;
    out->identification.vendor_id  = in->Base.VendorId;
    out->identification.product_id = in->Base.ProductId;
    out->identification.class      = in->Base.Class;
    out->identification.subclass   = in->Base.Subclass;
    to_sys_device_identification(&in->Base.Identification, &out->identification);

    out->controller_device_id = in->DeviceContext.controller_device_id;
    out->controller_driver_id = in->DeviceContext.controller_driver_id;
    out->hub_device_id        = in->DeviceContext.hub_device_id;
    out->hub_driver_id        = in->DeviceContext.hub_driver_id;
    out->hub_address          = in->DeviceContext.hub_address;
    out->port_address         = in->DeviceContext.port_address;
    out->device_address       = in->DeviceContext.device_address;
    out->configuration_length = in->DeviceContext.configuration_length;
    out->device_mps           = in->DeviceContext.device_mps;
    out->speed                = in->DeviceContext.speed;
}

static void to_sys_device(Device_t* in, struct sys_device* out)
{
    sys_device_init(out);

    if (in->Length == sizeof(Device_t)) {
        out->content_type = SYS_DEVICE_CONTENT_BASE;
        to_sys_device_base(in, &out->content.base);
    } else if (in->Length == sizeof(BusDevice_t)) {
        out->content_type = SYS_DEVICE_CONTENT_BUS;
        to_sys_device_bus((BusDevice_t*)in, &out->content.bus);
    } else if (in->Length == sizeof(UsbDevice_t)) {
        out->content_type = SYS_DEVICE_CONTENT_USB;
        to_sys_device_usb((UsbDevice_t*)in,&out->content.usb);
    }
}

static char* from_protocol_string(const char* in)
{
    if (!in || strlen(in) == 0) {
        return NULL;
    }
    return strdup(in);
}

static void from_sys_device_identification(const struct sys_device_identification* in, DeviceIdentification_t* out)
{
    out->Description = from_protocol_string(in->description);
    out->Manufacturer = from_protocol_string(in->manufacturer);
    out->Product = from_protocol_string(in->product);
    out->Revision = from_protocol_string(in->revision);
    out->Serial = from_protocol_string(in->serial);
}

static void from_sys_device_base(const struct sys_device_base* in, Device_t* out)
{
    out->Id = in->id;
    out->ParentId = in->parent_id;
    out->Length = sizeof(Device_t);
    out->VendorId = in->identification.vendor_id;
    out->ProductId = in->identification.product_id;
    out->Class = in->identification.class;
    out->Subclass = in->identification.subclass;
    from_sys_device_identification(&in->identification, &out->Identification);
}

static void from_sys_bus_io(const struct sys_bus_io* in, DeviceIo_t* out)
{
    out->Id = in->id;
    switch (in->access_type) {
        case SYS_BUS_IO_ACCESS_MEMORY: {
            out->Type                       = DeviceIoMemoryBased;
            out->Access.Memory.PhysicalBase = in->access.memory.physical_base;
            out->Access.Memory.VirtualBase  = in->access.memory.virtual_base;
            out->Access.Memory.Length       = in->access.memory.length;
        } break;
        case SYS_BUS_IO_ACCESS_PORT: {
            out->Type               = DeviceIoPortBased;
            out->Access.Port.Base   = in->access.port.base;
            out->Access.Port.Length = in->access.port.length;
        } break;
        case SYS_BUS_IO_ACCESS_PIN: {
            out->Type            = DeviceIoPinBased;
            out->Access.Pin.Port = in->access.pin.port;
            out->Access.Pin.Pin  = in->access.pin.pin;
        } break;
        default: break;
    }
}

static void from_sys_device_bus(struct sys_device_bus* in, BusDevice_t* out)
{
    memset(out, 0, sizeof(BusDevice_t));
    out->Base.Id = in->id;
    out->Base.ParentId = in->parent_id;
    out->Base.Length = sizeof(BusDevice_t);
    out->Base.VendorId = in->identification.vendor_id;
    out->Base.ProductId = in->identification.product_id;
    out->Base.Class = in->identification.class;
    out->Base.Subclass = in->identification.subclass;
    from_sys_device_identification(&in->identification, &out->Base.Identification);

    for (int i = 0; i < in->ios_count; i++) {
        struct sys_bus_io* io = sys_device_bus_ios_get(in, i);
        from_sys_bus_io(io, &out->IoSpaces[i]);
    }

    out->InterruptLine = in->irq_line;
    out->InterruptPin = in->irq_pin;
    out->InterruptAcpiConform = in->acpi_conform_flags;
    out->Segment = in->segment;
    out->Bus = in->bus;
    out->Slot = in->slot;
    out->Function = in->function;
}

static void from_sys_device_usb(const struct sys_device_usb* in, UsbDevice_t* out)
{
    out->Base.Id = in->id;
    out->Base.ParentId = in->parent_id;
    out->Base.Length = sizeof(UsbDevice_t);
    out->Base.VendorId = in->identification.vendor_id;
    out->Base.ProductId = in->identification.product_id;
    out->Base.Class = in->identification.class;
    out->Base.Subclass = in->identification.subclass;
    from_sys_device_identification(&in->identification, &out->Base.Identification);

    out->DeviceContext.controller_device_id = in->controller_device_id;
    out->DeviceContext.controller_driver_id = in->controller_driver_id;
    out->DeviceContext.hub_device_id = in->hub_device_id;
    out->DeviceContext.hub_driver_id = in->hub_driver_id;
    out->DeviceContext.hub_address = in->hub_address;
    out->DeviceContext.port_address = in->port_address;
    out->DeviceContext.device_address = in->device_address;
    out->DeviceContext.configuration_length = in->configuration_length;
    out->DeviceContext.device_mps = in->device_mps;
    out->DeviceContext.speed = in->speed;
}

static Device_t* from_sys_device(const struct sys_device* in)
{
    Device_t* out = NULL;

    switch (in->content_type) {
        case SYS_DEVICE_CONTENT_BASE: {
            out = malloc(sizeof(Device_t));
            if (out == NULL) {
                return NULL;
            }
            from_sys_device_base(&in->content.base, out);
        } break;
        case SYS_DEVICE_CONTENT_BUS: {
            out = malloc(sizeof(BusDevice_t));
            if (out == NULL) {
                return NULL;
            }
            from_sys_device_bus((struct sys_device_bus*)&in->content.bus, (BusDevice_t*)out);
        } break;
        case SYS_DEVICE_CONTENT_USB: {
            out = malloc(sizeof(UsbDevice_t));
            if (out == NULL) {
                return NULL;
            }
            from_sys_device_usb(&in->content.usb, (UsbDevice_t*)out);
        } break;
        default: break;
    }
    return out;
}

static void from_fs_setup_params(const struct ctt_fs_setup_params* in, struct VFSStorageParameters* out)
{
    switch (in->storage_type) {
        case CTT_FS_SETUP_PARAMS_STORAGE_DEVICE: {
            out->StorageType = VFSSTORAGE_TYPE_DEVICE;
            out->Storage.Device.DeviceID = in->storage.device.device_id;
            out->Storage.Device.DriverID = in->storage.device.driver_id;
        } break;
        case CTT_FS_SETUP_PARAMS_STORAGE_FILE: {
            out->StorageType = VFSSTORAGE_TYPE_FILE;
            out->Storage.File.HandleID = in->storage.file.id;
        } break;
        default: break;
    }
    out->Flags = in->flags;
    out->SectorStart.QuadPart = in->sector;
}

static void to_fs_setup_params(const struct VFSStorageParameters* in, struct ctt_fs_setup_params* out)
{
    ctt_fs_setup_params_init(out);
    switch (in->StorageType) {
        case VFSSTORAGE_TYPE_DEVICE: {
            ctt_fs_setup_params_storage_set_fs_storage_device(out, &(struct ctt_fs_storage_device) {
                .device_id = in->Storage.Device.DeviceID,
                .driver_id = in->Storage.Device.DriverID
            });
        } break;
        case VFSSTORAGE_TYPE_FILE: {
            ctt_fs_setup_params_storage_set_fs_storage_file(out, &(struct ctt_fs_storage_file) {
                    .id = in->Storage.File.HandleID
            });
        } break;
        default: break;
    }
    out->flags = in->Flags;
    out->sector = in->SectorStart.QuadPart;
}

static void from_fsstat(const struct ctt_fsstat* in, struct VFSStatFS* out)
{
    out->Label = mstr_new_u8(in->label);
    out->BlockSize = in->block_size;
    out->BlocksPerSegment = in->blocks_per_segment;
    out->MaxFilenameLength = in->max_filename_length;
    out->SegmentsFree = in->segments_free;
    out->SegmentsTotal = in->segments_total;
}

static void to_fsstat(const struct VFSStatFS* in, struct ctt_fsstat* out)
{
    out->label = mstr_u8(in->Label);
    out->block_size = in->BlockSize;
    out->blocks_per_segment = in->BlocksPerSegment;
    out->max_filename_length = in->MaxFilenameLength;
    out->segments_free = in->SegmentsFree;
    out->segments_total = in->SegmentsTotal;
}

static enum USBTransferCode to_usbcode(enum ctt_usb_transfer_status status) {
    switch (status) {
        case CTT_USB_TRANSFER_STATUS_SUCCESS: return USBTRANSFERCODE_SUCCESS;
        case CTT_USB_TRANSFER_STATUS_CANCELLED: return USBTRANSFERCODE_CANCELLED;
        case CTT_USB_TRANSFER_STATUS_NOBANDWIDTH: return USBTRANSFERCODE_NOBANDWIDTH;
        case CTT_USB_TRANSFER_STATUS_STALL: return USBTRANSFERCODE_STALL;
        case CTT_USB_TRANSFER_STATUS_NORESPONSE: return USBTRANSFERCODE_NORESPONSE;
        case CTT_USB_TRANSFER_STATUS_DATATOGGLEMISMATCH: return USBTRANSFERCODE_DATATOGGLEMISMATCH;
        case CTT_USB_TRANSFER_STATUS_BUFFERERROR: return USBTRANSFERCODE_BUFFERERROR;
        case CTT_USB_TRANSFER_STATUS_NAK: return USBTRANSFERCODE_NAK;
        case CTT_USB_TRANSFER_STATUS_BABBLE: return USBTRANSFERCODE_BABBLE;
        default: return USBTRANSFERCODE_INVALID;
    }
}


static enum ctt_usb_transfer_status from_usbcode(enum USBTransferCode code) {
    switch (code) {
        case USBTRANSFERCODE_SUCCESS: return CTT_USB_TRANSFER_STATUS_SUCCESS;
        case USBTRANSFERCODE_CANCELLED: return CTT_USB_TRANSFER_STATUS_CANCELLED;
        case USBTRANSFERCODE_NOBANDWIDTH: return CTT_USB_TRANSFER_STATUS_NOBANDWIDTH;
        case USBTRANSFERCODE_STALL: return CTT_USB_TRANSFER_STATUS_STALL;
        case USBTRANSFERCODE_NORESPONSE: return CTT_USB_TRANSFER_STATUS_NORESPONSE;
        case USBTRANSFERCODE_DATATOGGLEMISMATCH: return CTT_USB_TRANSFER_STATUS_DATATOGGLEMISMATCH;
        case USBTRANSFERCODE_BUFFERERROR: return CTT_USB_TRANSFER_STATUS_BUFFERERROR;
        case USBTRANSFERCODE_BABBLE: return CTT_USB_TRANSFER_STATUS_BABBLE;
        default: return CTT_USB_TRANSFER_STATUS_NAK;
    }
}

#endif //!__DDK_CONVERT_H__
