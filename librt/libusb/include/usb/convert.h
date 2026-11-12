/**
 * Copyright, Philip Meulengracht
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
 */

#ifndef __USB_CONVERT_H__
#define __USB_CONVERT_H__

#include <usb/usb.h>
#include <ddk/convert/grants.h>
#include <ctt_usbhost_service.h>

static inline unsigned int
__USBTransferScope(
    _In_ const USBTransfer_t* transfer)
{
    // The DMA for a control transfer covers both the setup packet and the
    // data/status stages, so it needs read and write access.
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL ||
        transfer->Direction == USBTRANSFER_DIRECTION_IN) {
        return GrantTokenScopeRead | GrantTokenScopeWrite;
    }
    return GrantTokenScopeRead;
}

static inline bool
__USBTransferFieldsValid(
    _In_ const USBTransfer_t* transfer)
{
    if ((unsigned int)transfer->Type > USBTRANSFER_TYPE_ISOC ||
        (unsigned int)transfer->Direction > USBTRANSFER_DIRECTION_OUT ||
        (unsigned int)transfer->Speed > USBSPEED_SUPER_PLUS) {
        return false;
    }
    if (transfer->MaxPacketSize == 0 || transfer->Length == 0) {
        return false;
    }
    return (transfer->Flags & ~(USB_TRANSFER_NO_NOTIFICATION | USB_TRANSFER_SHORT_NOT_OK)) == 0;
}

/**
 * @brief Converts the endpoint information and an existing token to the USB contract type.
 * The buffer selector must cover the whole setup and data range of the transfer.
 */
static inline oserr_t
__ToUSBTransfer(
    _In_  const USBTransfer_t*   in,
    _In_  const SHMTransfer_t*   buffer,
    _Out_ struct ctt_usb_transfer* out)
{
    oserr_t status;

    if (!__USBTransferFieldsValid(in)) {
        return OS_EINVALPARAMS;
    }
    
    status = __ToSysSHMTransfer(buffer, &out->buffer);
    if (status != OS_EOK) {
        return status;
    }

    out->hub_address = in->Address.HubAddress;
    out->port_address = in->Address.PortAddress;
    out->device_address = in->Address.DeviceAddress;
    out->endpoint_address = in->Address.EndpointAddress;
    out->type = (enum ctt_usb_transfer_type)in->Type;
    out->direction = (enum ctt_usb_transfer_direction)in->Direction;
    out->speed = (enum ctt_usb_speed)in->Speed;
    out->max_packet_size = in->MaxPacketSize;
    out->flags = in->Flags;
    out->periodic_bandwidth = in->PeriodicBandwith;
    out->periodic_interval = in->PeriodicInterval;

    return OS_EOK;
}

/**
 * @brief Decodes the USB transfer information and checks the selector and access
 * scope of the token. BufferHandle stays UUID_INVALID until the host attaches to
 * the returned token.
 */
static inline oserr_t
__FromUSBTransfer(
    _In_  const struct ctt_usb_transfer* in,
    _Out_ USBTransfer_t*                 out,
    _Out_ SHMTransfer_t*                 buffer)
{
    unsigned int scope;
    oserr_t      status;

    status = __FromSysSHMTransfer(&in->buffer, buffer);
    if (status != OS_EOK) {
        return status;
    }

    *out = (USBTransfer_t) {
        .Address = {
            in->hub_address,
            in->port_address,
            in->device_address,
            in->endpoint_address
        },
        .Type = (enum USBTransferType)in->type,
        .Direction = (enum USBTransferDirection)in->direction,
        .Speed = (enum USBSpeed)in->speed,
        .MaxPacketSize = in->max_packet_size,
        .Flags = in->flags,
        .BufferHandle = UUID_INVALID,
        .BufferOffset = buffer->RelativeOffset,
        .Length = __SHMTransferLength(buffer),
        .PeriodicBandwith = in->periodic_bandwidth,
        .PeriodicInterval = in->periodic_interval
    };

    if (!__USBTransferFieldsValid(out)) {
        return OS_EINVALPARAMS;
    }

    scope = __USBTransferScope(out);
    if ((buffer->Token.Options.Scope & scope) != scope) {
        return OS_EPERMISSIONS;
    }

    return OS_EOK;
}

#endif
