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

//#define __TRACE

#include <bus/pci/dma.h>
#include <ddk/utils.h>
#include <bus/pci/hosts/ecam.h>
#include <bus/pci/host-private.h>
#include <firmware/pci.h>
#include <core/io.h>
#include <stdlib.h>
#include <string.h>

static size_t
__EcamOffset(
    _In_ PciHost_t*   host,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function,
    _In_ size_t       reg)
{
    // The mapping starts at BusStart. Each bus uses 1 MiB, with 4 KiB per function.
    return (size_t)(
        ((bus - host->Identification.BusStart) << 20) |
        (slot << 15) |
        (function << 12) |
        reg
    );
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
    oserr_t status;

    status = WriteDeviceIo(
        &host->IoSpace,
        __EcamOffset(host, bus, slot, function, reg),
        value,
        width
    );
    if (status != OS_EOK) {
        ERROR("__EcamWrite %u:%u:%u.%u register 0x%x failed: %u",
            host->Identification.Segment,
            bus,
            slot,
            function,
            (unsigned)reg,
            status);
    }
}

static oserr_t
__DtTranslate(
    _In_  PciHost_t* host,
    _In_  uint32_t   space,
    _In_  uint64_t   address,
    _In_  uint64_t   length,
    _Out_ uint64_t* physicalOut)
{
    oserr_t status;

    status = FdtTranslatePciAddress(
        host->OpContext,
        (FdtPciSpace_t)space,
        address,
        length,
        physicalOut
    );
    if (status != OS_EOK) {
        if (status == OS_ENOENT) {
            WARNING("__DtTranslate: segment %u space %u address 0x%llx length 0x%llx has no window (%u)",
                host->Identification.Segment,
                space,
                (unsigned long long)address,
                (unsigned long long)length,
                status);
        } else {
            ERROR("__DtTranslate: segment %u space %u address 0x%llx length 0x%llx translation failed (%u)",
                host->Identification.Segment,
                space,
                (unsigned long long)address,
                (unsigned long long)length,
                status);
        }
        return status;
    }
    return OS_EOK;
}

static oserr_t
__DtResolveInterrupt(
    _In_  PciHost_t*    host,
    _In_  unsigned int  bus,
    _In_  unsigned int  slot,
    _In_  unsigned int  function,
    _In_  unsigned int  pin,
    _Out_ int*          lineOut,
    _Out_ unsigned int* flagsOut)
{
    oserr_t status;

    status = FdtResolvePciInterrupt(
        host->OpContext,
        bus,
        slot,
        function,
        pin,
        lineOut,
        flagsOut
    );
    if (status != OS_EOK) {
        if (status == OS_ENOENT || status == OS_ENOTSUPPORTED) {
            WARNING("__DtResolveInterrupt: device %u:%u:%u.%u pin %u cannot be routed (%u)",
                host->Identification.Segment,
                bus,
                slot,
                function,
                pin,
                status);
        } else {
            ERROR("__DtResolveInterrupt: device %u:%u:%u.%u pin %u interrupt resolution failed (%u)",
                host->Identification.Segment,
                bus,
                slot,
                function,
                pin,
                status);
        }
        return status;
    }
    return OS_EOK;
}

static const struct PciHostOperations g_pciAcpiEcamOperations = {
    .GetDmaDescription = PciHostGetIdentityDma,
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
    oserr_t    cleanup;

    TRACE("PciEcamHostCreate(segment=%u, buses=%u-%u, base=0x%llx)",
        configuration->Identification.Segment,
        configuration->Identification.BusStart,
        configuration->Identification.BusEnd,
        (unsigned long long)base);

    *hostOut = NULL;
    if (configuration->Identification.HostId != UUID_INVALID) {
        ERROR("PciEcamHostCreate: segment %u has invalid host ID %u",
            configuration->Identification.Segment,
            configuration->Identification.HostId);
        return OS_EINVALPARAMS;
    }
    if (configuration->Identification.BusStart > configuration->Identification.BusEnd) {
        ERROR("PciEcamHostCreate: segment %u has invalid bus range %u-%u",
            configuration->Identification.Segment,
            configuration->Identification.BusStart,
            configuration->Identification.BusEnd);
        return OS_EINVALPARAMS;
    }

    length = ((size_t)configuration->Identification.BusEnd -
        configuration->Identification.BusStart + 1) << 20;
    if (base > UINTPTR_MAX || length - 1 > UINTPTR_MAX - (uintptr_t)base) {
        ERROR("PciEcamHostCreate: segment %u ECAM base 0x%llx length 0x%llx exceeds CPU address space",
            configuration->Identification.Segment,
            (unsigned long long)base,
            (unsigned long long)length);
        return OS_EOVERFLOW;
    }

    host = calloc(1, sizeof(PciHost_t) +
        (configuration->Firmware != NULL ? sizeof(struct FdtPciHost) : 0));
    if (host == NULL) {
        ERROR("PciEcamHostCreate: segment %u buses %u-%u failed to allocate host",
            configuration->Identification.Segment,
            configuration->Identification.BusStart,
            configuration->Identification.BusEnd);
        return OS_EOOM;
    }

    status = CreateDeviceMemoryIo(&host->IoSpace, (uintptr_t)base, length);
    if (status != OS_EOK) {
        ERROR("PciEcamHostCreate: segment %u ECAM base 0x%llx length 0x%llx I/O creation failed (%u)",
            configuration->Identification.Segment,
            (unsigned long long)base,
            (unsigned long long)length,
            status);
        free(host);
        return status;
    }

    status = AcquireDeviceIo(&host->IoSpace);
    if (status != OS_EOK) {
        ERROR("PciEcamHostCreate: segment %u ECAM base 0x%llx length 0x%llx I/O acquisition failed (%u)",
            configuration->Identification.Segment,
            (unsigned long long)base,
            (unsigned long long)length,
            status);
        cleanup = DmIoRelease(&host->IoSpace, &host->IoAcquired);
        if (cleanup != OS_EOK) {
            ERROR("PciEcamHostCreate: segment %u ECAM base 0x%llx I/O destruction failed (%u)",
                configuration->Identification.Segment,
                (unsigned long long)base,
                cleanup);
            // Return the unfinished host so the caller can retry destruction.
            *hostOut = host;
        } else {
            free(host);
        }
        return status;
    }

    host->IoAcquired = 1;
    host->Identification = configuration->Identification;
    host->IsExtended = 1;
    host->IoResourcePolicy = configuration->IoResourcePolicy;
    host->Operations = &g_pciAcpiEcamOperations;

    if (configuration->Firmware != NULL) {
        // Store the firmware description after the host in the same allocation.
        // Its referenced data must stay mapped until host registration succeeds.
        host->OpContext = host + 1;
        memcpy(host->OpContext, configuration->Firmware, sizeof(struct FdtPciHost));
        host->Firmware = host->OpContext;
        host->Operations = &g_pciDtEcamOperations;
    }

    *hostOut = host;
    return OS_EOK;
}
