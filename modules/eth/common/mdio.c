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

#include "mdio.h"
#include <ddk/utils.h>

oserr_t
MdioInitialize(
    _Out_ MdioBus_t*                   bus,
    _In_  const struct MdioOperations* operations,
    _In_  void*                        context,
    _In_  uint32_t                     address)
{
    if (bus == NULL || operations == NULL || address > MDIO_MAX_ADDRESS) {
        return OS_EINVALPARAMS;
    }

    if (operations->Read == NULL || operations->Write == NULL || operations->DelayUs == NULL) {
        return OS_EINVALPARAMS;
    }

    // Publish the endpoint only after the entire callback table is valid.
    // The firmware address selects this PHY. Do not probe other bus addresses.
    bus->Operations = operations;
    bus->Context = context;
    bus->Address = (uint8_t)address;
    return OS_EOK;
}

oserr_t
MdioRead(
    _In_  MdioBus_t* bus,
    _In_  uint8_t    reg,
    _Out_ uint16_t*  value)
{
    uint16_t result;
    oserr_t  status;

    if (reg > MDIO_MAX_REGISTER) {
        return OS_EINVALPARAMS;
    }

    status = bus->Operations->Read(
        bus->Context,
        bus->Address,
        reg,
        &result
    );
    if (status != OS_EOK) {
        TRACE("MdioRead: address %u register 0x%02x failed (%u)", bus->Address, reg, status);
        return status;
    }

    // A failed transaction must not replace the caller's last valid value.
    *value = result;
    return OS_EOK;
}

oserr_t
MdioWrite(
    _In_ MdioBus_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   value)
{
    oserr_t status;

    if (reg > MDIO_MAX_REGISTER) {
        return OS_EINVALPARAMS;
    }

    // Do not retry here. A failed transaction may already have changed the PHY.
    status = bus->Operations->Write(
        bus->Context,
        bus->Address,
        reg,
        value
    );
    if (status != OS_EOK) {
        TRACE("MdioWrite: address %u register 0x%02x failed (%u)", bus->Address, reg, status);
    }
    return status;
}

oserr_t
MdioModify(
    _In_ MdioBus_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   mask,
    _In_ uint16_t   value)
{
    uint16_t original;
    uint16_t updated;
    oserr_t  status;

    status = MdioRead(bus, reg, &original);
    if (status != OS_EOK) {
        return status;
    }

    // The caller serializes the complete read/modify/write sequence. Mask the
    // supplied value too, so unrelated register fields cannot be changed.
    updated = (original & ~mask) | (value & mask);
    if (updated == original) {
        return OS_EOK;
    }
    return MdioWrite(bus, reg, updated);
}
