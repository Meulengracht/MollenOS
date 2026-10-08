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

#include <bus/pci/hosts/ecam.h>
#include <bus/pci/host-private.h>
#include <firmware/pci.h>
#include <stdlib.h>
#include <string.h>

static size_t
__EcamOffset(
    _In_ PciHost_t*   Io,
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t       Register)
{
    return (size_t)(
        ((Bus - Io->Identification.BusStart) << 20) |
            (Device << 15) |
            (Function << 12) |
            Register);
}

static size_t
__EcamRead(
    _In_ PciHost_t*   host,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       width)
{
    return ReadDeviceIo(
        &host->IoSpace,
        __EcamOffset(host, bus, slot, function, reg),
        width
    );
}

static void
__EcamWrite(
    _In_ PciHost_t*   host,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg,
    _In_ size_t       value,
    _In_ size_t       width)
{
    WriteDeviceIo(
        &host->IoSpace,
        __EcamOffset(host, bus, slot, function, reg),
        value,
        width
    );
}

static oserr_t
__DtTranslate(
    _In_ PciHost_t* host,
    _In_ uint32_t   space,
    _In_ uint64_t   address,
    _In_ uint64_t   length,
    _Out_ uint64_t* physicalOut)
{
    return FdtTranslatePciAddress(
        host->OpContext,
        space,
        address,
        length,
        physicalOut
    );
}

static oserr_t
__DtResolveInterrupt(
    _In_ PciHost_t*     host,
    _In_ unsigned int   bus,
    _In_ unsigned int   slot,
    _In_ unsigned int   function,
    _In_ unsigned int   pin,
    _Out_ int*          lineOut,
    _Out_ unsigned int* flagsOut)
{
    return FdtResolvePciInterrupt(
        host->OpContext,
        bus,
        slot,
        function,
        pin,
        lineOut,
        flagsOut
    );
}

static const struct PciHostOperations g_pciAcpiEcamOperations = {
    .Read = __EcamRead,
    .Write = __EcamWrite
};

static const struct PciHostOperations g_pciDtEcamOperations = {
    .Read = __EcamRead,
    .Write = __EcamWrite,
    .Translate = __DtTranslate,
    .ResolveInterrupt = __DtResolveInterrupt
};


oserr_t
PciEcamHostCreate(
    _In_  const struct PciEcamHostConfiguration* configuration,
    _Out_ PciHost_t**                            hostOut)
{
    PciHost_t* host;
    size_t     length;
    uint64_t   base = configuration->Base;
    oserr_t    status;

    *hostOut = NULL;
    if (configuration->Identification.HostId != UUID_INVALID ||
        configuration->Identification.BusStart > configuration->Identification.BusEnd) {
        return OS_EINVALPARAMS;
    }

    length = ((size_t)configuration->Identification.BusEnd -
        configuration->Identification.BusStart + 1) << 20;
    if (base > UINTPTR_MAX || length - 1 > UINTPTR_MAX - (uintptr_t)base) {
        return OS_EOVERFLOW;
    }

    host = calloc(1, sizeof(*host) +
        (configuration->Firmware != NULL ? sizeof(struct FdtPciHost) : 0));
    if (host == NULL) {
        return OS_EOOM;
    }

    status = CreateDeviceMemoryIo(&host->IoSpace, (uintptr_t)base, length);
    if (status != OS_EOK) {
        free(host);
        return status;
    }

    status = AcquireDeviceIo(&host->IoSpace);
    if (status != OS_EOK) {
        DestroyDeviceIo(&host->IoSpace);
        free(host);
        return status;
    }

    host->Identification = configuration->Identification;
    host->IsExtended = 1;
    host->IoResourcePolicy = configuration->IoResourcePolicy;
    host->Operations = &g_pciAcpiEcamOperations;

    if (configuration->Firmware != NULL) {
        host->OpContext = host + 1;
        memcpy(host->OpContext, configuration->Firmware, sizeof(struct FdtPciHost));
        host->Firmware = host->OpContext;
        host->Operations = &g_pciDtEcamOperations;
    }

    *hostOut = host;
    return OS_EOK;
}
