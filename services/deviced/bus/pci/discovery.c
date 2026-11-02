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

#include <ddk/acpi.h>
#include <ddk/firmware.h>
#include <ddk/utils.h>
#include <stdlib.h>

#include <bus/pci/discovery.h>
#include <bus/pci/host-private.h>
#include <bus/pci/device.h>
#include <bus/pci/enumerate.h>
#include <bus/pci/interrupts.h>
#include <bus/pci/publish.h>
#include <bus/pci/hosts/ecam.h>
#include <bus/pci/hosts/legacy.h>
#include <bus/pci/hosts/broadcom/bcm.h>

#include <firmware/pci.h>
#include <firmware/tree.h>

#ifdef __OSCONFIG_HAS_LEGACY_PCI
#include <bus/legacy/fixed.h>
#endif

static int        g_acpiAvailable;
static int        g_msiDisabled;
static int        g_hostsDiscovered;
static int        g_policyInitialized;
static oserr_t    g_discoveryStatus;
static oserr_t    g_discoveryFailure;
static PciHost_t* g_hostCleanup;

static void
__RecordDiscoveryError(
    _In_ oserr_t status)
{
    DmStartupResult_t result;

    // Retry only errors classified as temporary. Invalid firmware stays invalid
    // across startup attempts.
    __DmStartupFromStatus(status, &result);
    if (result.Outcome != DmStartupRetry) {
        return;
    }
    
    if (g_discoveryStatus == OS_EOK) {
        g_discoveryStatus = status;
    }
}

static int
__HostExists(
    _In_ uint32_t segment,
    _In_ uint8_t  busStart,
    _In_ uint8_t  busEnd)
{
    element_t* element;
    PciHost_t* host;

    _foreach(element, &g_pciRoots) {
        host = ((struct PciDevice*)element->value)->Host;
        if (host->Identification.Segment == segment &&
            host->Identification.BusStart == busStart &&
            host->Identification.BusEnd == busEnd) {
            return 1;
        }
    }
    return 0;
}

static void
__DiscardHost(
    _In_ PciHost_t*      host,
    _In_ struct FdtTree* tree)
{
    oserr_t status;

    if (host == NULL) {
        return;
    }
    // If construction failed before attachment, keep the firmware tree alive
    // until controller cleanup succeeds.
    if (tree != NULL && host->FirmwareTree == NULL) {
        FdtTreeAddReference(tree);
        host->FirmwareTree = tree;
    }
    status = PciHostDestroy(host);
    if (status != OS_EOK) {
        ERROR("__DiscardHost: failed to destroy host %u, segment %u, buses %u-%u (%u)",
            host->Identification.HostId, host->Identification.Segment,
            host->Identification.BusStart, host->Identification.BusEnd, status);
        host->CleanupNext = g_hostCleanup;
        g_hostCleanup = host;
        g_discoveryStatus = status;
    }
}

static oserr_t
__FinishHostCleanup(void)
{
    PciHost_t** link = &g_hostCleanup;
    PciHost_t*  host;
    PciHost_t*  next;
    oserr_t     status;
    oserr_t     result = OS_EOK;

    while (*link != NULL) {
        host = *link;
        next = host->CleanupNext;
        status = PciHostDestroy(host);
        if (status != OS_EOK) {
            ERROR("__FinishHostCleanup: failed to destroy host %u, segment %u, buses %u-%u (%u)",
                host->Identification.HostId, host->Identification.Segment,
                host->Identification.BusStart, host->Identification.BusEnd, status);
            result = status;
            link = &host->CleanupNext;
            continue;
        }
        *link = next;
    }
    return result;
}

int
PciIsMsiAvailable(void)
{
    if (g_msiDisabled) {
        return 0;
    }
    return 1;
}

int
PciIsAcpiAvailable(void)
{
    return g_acpiAvailable;
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void
__EnumerateLegacy(void)
{
    PciHost_t* host;
    oserr_t    status;

    TRACE("__EnumerateLegacy()");

    status = PciLegacyHostCreate(&host);
    if (status != OS_EOK) {
        ERROR("__EnumerateLegacy: failed to create host (%u)", status);
        __RecordDiscoveryError(status);
        __DiscardHost(host, NULL);
        return;
    }
    
    status = PciHostAttach(host, NULL);
    if (status != OS_EOK) {
        ERROR("__EnumerateLegacy: failed to attach host (%u)", status);
        __RecordDiscoveryError(status);
        __DiscardHost(host, NULL);
        return;
    }
    
    PciScanLegacyRoots(host);
}
#endif

// base must address busStart. Each bus occupies 1 MiB of ECAM space.
static void
__EnumerateEcamWindow(
    _In_ uint32_t                  segment,
    _In_ uint8_t                   busStart,
    _In_ uint8_t                   busEnd,
    _In_ uint64_t                  base,
    _In_ const struct FdtPciHost*  firmwareHost,
    _In_ struct FdtTree*           tree)
{
    PciHost_t* host;
    oserr_t    status;
    
    struct PciEcamHostConfiguration configuration = {
        .Identification = {
            .Segment = segment,
            .BusStart = busStart,
            .BusEnd = busEnd
        },
        .Base = base,
        .IoResourcePolicy = PciIoResourceMemory,
        .Firmware = firmwareHost
    };

    TRACE("__EnumerateEcamWindow(segment=%u, buses=%u-%u, base=0x%llx)",
        segment, busStart, busEnd, (unsigned long long)base);

    if (__HostExists(segment, busStart, busEnd)) {
        TRACE("__EnumerateEcamWindow: skipping existing segment %u, buses %u-%u",
            segment, busStart, busEnd);
        return;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (firmwareHost == NULL) {
        configuration.IoResourcePolicy = PciIoResourcePorts;
    }
#endif
    
    status = PciEcamHostCreate(&configuration, &host);
    if (status != OS_EOK) {
        ERROR("__EnumerateEcamWindow: failed to create segment %u, buses %u-%u, base 0x%llx (%u)",
            segment, busStart, busEnd, (unsigned long long)base, status);
        __RecordDiscoveryError(status);
        __DiscardHost(host, tree);
        return;
    }
    
    status = PciHostAttach(host, tree);
    if (status != OS_EOK) {
        ERROR("__EnumerateEcamWindow: failed to attach segment %u, buses %u-%u (%u)",
            segment, busStart, busEnd, status);
        __RecordDiscoveryError(status);
        __DiscardHost(host, tree);
        return;
    }
    PciCheckBus(host->RootDevice, busStart);
}

static void
__EnumerateMcfg(void)
{
    ACPI_TABLE_HEADER*    header;
    ACPI_MCFG_ALLOCATION* entry;
    size_t                count;
    oserr_t               status;

    TRACE("__EnumerateMcfg()");

    status = AcpiQueryTable(ACPI_SIG_MCFG, &header);
    if (status != OS_EOK) {
        TRACE("__EnumerateMcfg: MCFG unavailable (%u); legacy fallback may be used", status);
        __RecordDiscoveryError(status);
        return;
    }

    if (header->Length < sizeof(ACPI_TABLE_MCFG)) {
        ERROR("__EnumerateMcfg: truncated MCFG, length %u", header->Length);
        free(header);
        return;
    }

    entry = (ACPI_MCFG_ALLOCATION*)((uint8_t*)header + sizeof(ACPI_TABLE_MCFG));
    count = (header->Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
    for (size_t i = 0; i < count; i++, entry++) {
        if (entry->EndBusNumber < entry->StartBusNumber) {
            WARNING("__EnumerateMcfg: ignoring segment %u with inverted bus range %u-%u",
                entry->PciSegment, entry->StartBusNumber, entry->EndBusNumber);
            continue;
        }
        if (entry->Address > UINT64_MAX - ((uint64_t)entry->StartBusNumber << 20)) {
            WARNING("__EnumerateMcfg: ECAM base 0x%llx overflows for segment %u, buses %u-%u",
                (unsigned long long)entry->Address, entry->PciSegment,
                entry->StartBusNumber, entry->EndBusNumber);
            continue;
        }

        // MCFG gives the address of bus 0. Advance to the first described bus.
        __EnumerateEcamWindow(
            entry->PciSegment,
            entry->StartBusNumber,
            entry->EndBusNumber,
            entry->Address + ((uint64_t)entry->StartBusNumber << 20),
            NULL,
            NULL
        );
    }
    free(header);
}

static void
__OnFdtPciHost(
    _In_ const struct FdtPciHost* host,
    _In_ void*                    context)
{
    PciHost_t* bus;
    oserr_t    status;

    TRACE("__OnFdtPciHost(segment=%u, buses=%u-%u, type=%u)",
        host->Segment, host->BusStart, host->BusEnd, host->Type);

    if (__HostExists(host->Segment, host->BusStart, host->BusEnd)) {
        TRACE("__OnFdtPciHost: skipping existing segment %u, buses %u-%u",
            host->Segment, host->BusStart, host->BusEnd);
        return;
    }

    if (host->Type == FdtPciHostEcam) {
        __EnumerateEcamWindow(
            host->Segment,
            host->BusStart,
            host->BusEnd,
            host->EcamBase,
            host,
            context
        );
        return;
    }
    
    status = BcmPciHostCreate(host, &bus);
    if (status != OS_EOK) {
        ERROR("__OnFdtPciHost: failed to create segment %u, buses %u-%u, type %u (%u)",
            host->Segment, host->BusStart, host->BusEnd, host->Type, status);
        __RecordDiscoveryError(status);
        __DiscardHost(bus, context);
        return;
    }

    status = PciHostAttach(bus, context);
    if (status != OS_EOK) {
        ERROR("__OnFdtPciHost: failed to attach segment %u, buses %u-%u, type %u (%u)",
            host->Segment, host->BusStart, host->BusEnd, host->Type, status);
        __RecordDiscoveryError(status);
        __DiscardHost(bus, context);
        return;
    }
    
    PciCheckBus(bus->RootDevice, host->BusStart);
    TRACE("__OnFdtPciHost: initialized and enumerated segment %u, buses %u-%u",
        host->Segment, host->BusStart, host->BusEnd);
}

static void
__EnumerateDeviceTree(void)
{
    struct FdtTree*   tree = FirmwareGetTree();
    DmStartupResult_t result;
    oserr_t           status;

    if (tree == NULL) {
        // Retry only while firmware startup can still produce a usable tree.
        FirmwareGetStartupResult(&result);
        TRACE("__EnumerateDeviceTree: firmware tree unavailable, outcome %u (%u)",
            result.Outcome, result.Status);
        if (result.Outcome == DmStartupRetry) {
            __RecordDiscoveryError(OS_EBUSY);
        }
        return;
    }

    status = FdtEnumeratePciHosts(tree, __OnFdtPciHost, tree);
    if (status != OS_EOK) {
        ERROR("__EnumerateDeviceTree: PCI discovery failed (%u)", status);
        __RecordDiscoveryError(status);
    }
}

static oserr_t
__InitializeDiscoveryPolicy(void)
{
    AcpiDescriptor_t  acpi = { 0 };
    DmStartupResult_t result;
    oserr_t           status;

    if (g_policyInitialized) {
        return OS_EOK;
    }
    status = AcpiQueryStatus(&acpi);
    __DmStartupFromStatus(status, &result);
    if (result.Outcome == DmStartupRetry) {
        WARNING("__InitializeDiscoveryPolicy: ACPI status query requires a retry (%u)", status);
        return status;
    }
    
    if (status != OS_EOK) {
        TRACE("__InitializeDiscoveryPolicy: ACPI unavailable (%u)", status);
    } else {
        TRACE("__InitializeDiscoveryPolicy: ACPI version 0x%x (BootFlags 0x%x)",
              acpi.Version, acpi.BootFlags);
        g_acpiAvailable = 1;
        g_msiDisabled = (acpi.BootFlags & ACPI_IA_NO_MSI) != 0;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    // Assume an 8042 controller is present when ACPI is unavailable or its
    // boot flags are zero.
    // Otherwise, require the firmware's 8042 flag.
    if (!g_acpiAvailable || (acpi.BootFlags & ACPI_IA_8042) || acpi.BootFlags == 0) {
        __InstallPS2Controller();
    }
#endif
    // Keep the policy fixed once hosts can use it, including discovery retries.
    g_policyInitialized = 1;
    return OS_EOK;
}

static oserr_t
__DiscoverHosts(void)
{
    OSFirmwareInfo_t firmware = { 0 };
    oserr_t          status;

    // Retry only unfinished startup. Registered hosts are skipped before any
    // controller writes, and completed discovery does not rescan for hot-plug.
    if (g_hostsDiscovered) {
        return OS_EOK;
    }

    g_discoveryStatus = OS_EOK;
    status = __InitializeDiscoveryPolicy();
    if (status != OS_EOK) {
        return status;
    }

    status = FirmwareQuery(&firmware);
    if (status != OS_EOK) {
        ERROR("__DiscoverHosts: firmware query failed (%u)", status);
        return status;
    }
    switch (firmware.Primary) {
        case OSFIRMWARE_ACPI:
            __EnumerateMcfg();
            break;
        case OSFIRMWARE_DEVICETREE:
            __EnumerateDeviceTree();
            break;
        default:
            TRACE("__DiscoverHosts: no PCI discovery method for firmware type %u", firmware.Primary);
            break;
    }

#ifdef __OSCONFIG_HAS_LEGACY_PCI
    if (g_pciRoots.head == NULL && g_discoveryStatus == OS_EOK) {
        __EnumerateLegacy();
    }
#endif

    if (g_pciRoots.head == NULL) {
        WARNING("__DiscoverHosts: no usable PCI configuration hosts");
    }
    g_hostsDiscovered = g_discoveryStatus == OS_EOK;
    return g_discoveryStatus;
}

void
BusRecoverAndRepublish(
    _Out_ DmStartupResult_t* result)
{
    element_t*        element;
    PciHost_t*        host;
    oserr_t           status;
    DmStartupResult_t next;

    // Finish releasing failed hosts before another discovery attempt, even if
    // their setup error prevents further startup attempts.
    status = __FinishHostCleanup();
    __DmStartupRetryResult(status, result);
    if (status != OS_EOK) {
        goto recover_hosts;
    }

    if (g_discoveryFailure != OS_EOK) {
        __DmStartupFromStatus(g_discoveryFailure, result);
        goto recover_hosts;
    }

    status = __DiscoverHosts();
    __DmStartupFromStatus(status, result);
    if (g_hostCleanup != NULL) {
        result->Outcome = DmStartupRetry;
    } else if (result->Outcome == DmStartupFailed) {
        g_discoveryFailure = result->Status;
    }

recover_hosts:
    // Continue startup for other hosts after one fails. A host with a permanent
    // startup error must not be restarted on later attempts.
    _foreach(element, &g_pciRoots) {
        host = ((struct PciDevice*)element->value)->Host;
        if (host->StartupFailure != OS_EOK) {
            next = (DmStartupResult_t) {
                .Status = host->StartupFailure,
                .Outcome = DmStartupFailed
            };
        } else {
            status = PciRecoverScan(host);
            __DmStartupFromStatus(status, &next);
            if (status != OS_EOK) {
                ERROR("BusRecoverAndRepublish: scan recovery failed for host %u, "
                      "segment %u, buses %u-%u (%u)",
                    host->Identification.HostId, host->Identification.Segment,
                    host->Identification.BusStart, host->Identification.BusEnd, status);
            }
            if (next.Outcome == DmStartupComplete) {
                PciPublishDevice(element->value, &next);
                if (next.Outcome != DmStartupComplete) {
                    ERROR("BusRecoverAndRepublish: publication failed for host %u, "
                          "segment %u, buses %u-%u, outcome %u (%u)",
                        host->Identification.HostId, host->Identification.Segment,
                        host->Identification.BusStart, host->Identification.BusEnd,
                        next.Outcome, next.Status);
                }
            }
            
            if (next.Outcome == DmStartupFailed) {
                host->StartupFailure = next.Status;
            }
        }
        __DmStartupMerge(result, &next);
    }
}
