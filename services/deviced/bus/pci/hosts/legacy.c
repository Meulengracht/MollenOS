/**
 * MollenOS
 *
 * Copyright (C) Philip Meulengracht
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

#include <bus/pci/hosts/legacy.h>
#include <bus/pci/host-private.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI

// To be able to access bus data we need io-space
// access, so lets define the io-ports neccessary
// for accessing PCI (legacy), not PCIe
#define PCI_IO_BASE                     0xCF8
#define PCI_IO_LENGTH                   8
#define PCI_REGISTER_SELECT             0x00
#define PCI_REGISTER_DATA               0x04

static void
__LegacySelect(
    _In_ PciHost_t*      Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register)
{
    size_t address = 0x80000000 | (Bus << 16) | (Device << 11) | (Function << 8) | (Register & 0xFC);
    WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, address, 4);
}

// Sub-dword accesses select a byte lane within the 32-bit data port.
#define __LEGACY_DATA_PORT(Register, Width) (PCI_REGISTER_DATA + ((Register) & 0x3 & ~((Width) - 1)))

static size_t
__LegacyRead(
    _In_ PciHost_t*    host,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       width)
{
    __LegacySelect(host, bus, slot, function, reg);
    return ReadDeviceIo(&host->IoSpace, __LEGACY_DATA_PORT(reg, width), width);
}

static void
__LegacyWrite(
    _In_ PciHost_t*    host,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       value,
    _In_ size_t       width)
{
    __LegacySelect(host, bus, slot, function, reg);
    WriteDeviceIo(&host->IoSpace, __LEGACY_DATA_PORT(reg, width), value, width);
}

static const struct PciHostOperations g_pciLegacyOperations = {
    .Read = __LegacyRead,
    .Write = __LegacyWrite
};
#endif

#ifdef __OSCONFIG_HAS_LEGACY_PCI
oserr_t
PciLegacyHostCreate(
    _Out_ PciHost_t** hostOut)
{
    PciHost_t* bus;
    oserr_t oserr;

    *hostOut = NULL;
    bus = (PciHost_t*)malloc(sizeof(PciHost_t));
    if (!bus) {
        return OS_EOOM;
    }
    memset(bus, 0, sizeof(PciHost_t));
    bus->Identification.BusEnd = 255;
    bus->Operations = &g_pciLegacyOperations;
    bus->IoResourcePolicy = PciIoResourcePorts;

    oserr = CreateDevicePortIo(&bus->IoSpace, PCI_IO_BASE, PCI_IO_LENGTH);
    if (oserr != OS_EOK) {
        ERROR(" > failed to initialize pci io space");
        free(bus);
        return oserr;
    }

    oserr = AcquireDeviceIo(&bus->IoSpace);
    if (oserr != OS_EOK) {
        ERROR(" > failed to acquire pci io space");
        DestroyDeviceIo(&bus->IoSpace);
        free(bus);
        return oserr;
    }
    *hostOut = bus;
    return OS_EOK;
}
#endif
