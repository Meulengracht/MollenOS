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
 * ARM has no separate port-I/O address space. MMIO is accessed through the
 * Device mappings supplied by the device-I/O subsystem. This file only contains
 * stubs.
 */

#include <arch/io.h>

oserr_t
ReadDirectIo(
    _In_  DeviceIoType_t type,
    _In_  uintptr_t      address,
    _In_  size_t         width,
    _Out_ size_t*        value)
{
    (void)type;
    (void)address;
    (void)width;
    (void)value;
    return OS_ENOTSUPPORTED;
}

oserr_t
WriteDirectIo(
    _In_ DeviceIoType_t type,
    _In_ uintptr_t      address,
    _In_ size_t         width,
    _In_ size_t         value)
{
    (void)type;
    (void)address;
    (void)width;
    (void)value;
    return OS_ENOTSUPPORTED;
}

oserr_t
ReadDirectPci(
    _In_  unsigned int bus,
    _In_  unsigned int slot,
    _In_  unsigned int function,
    _In_  unsigned int offset,
    _In_  size_t       width,
    _Out_ size_t*      value)
{
    // BCM2711/BCM2712 PCIe configuration needs its host-controller driver;
    // x86's global configuration ports do not exist on these platforms.
    (void)bus;
    (void)slot;
    (void)function;
    (void)offset;
    (void)width;
    (void)value;
    return OS_ENOTSUPPORTED;
}

oserr_t
WriteDirectPci(
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ unsigned int offset,
    _In_ size_t       width,
    _In_ size_t       value)
{
    (void)bus;
    (void)slot;
    (void)function;
    (void)offset;
    (void)width;
    (void)value;
    return OS_ENOTSUPPORTED;
}

oserr_t
SetDirectIoAccess(
    _In_ uuid_t         coreId,
    _In_ MemorySpace_t* space,
    _In_ uint16_t       port,
    _In_ int            enable)
{
    (void)coreId;
    (void)space;
    (void)port;
    (void)enable;
    return OS_ENOTSUPPORTED;
}
