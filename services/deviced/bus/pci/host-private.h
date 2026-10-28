/**
 * MollenOS
 *
 * Copyright 2015, Philip Meulengracht
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

#ifndef __DEVICED_BUS_PCI_HOST_PRIVATE_H__
#define __DEVICED_BUS_PCI_HOST_PRIVATE_H__

#include <bus/pci/host.h>
#include <bus/pci/publish.h>
#include <ddk/io.h>
#include <ds/list.h>

struct PciDevice;
struct FdtTree;
struct FdtPciHost;
struct PciDmaDescription;

/**
 * @brief Controller operations for PCI configuration, address translation and IRQs.
 * The host implementation supplies these callbacks and its optional DMA policy.
 */
struct PciHostOperations {
    /**
     * @brief Copy configured DMA ranges without giving callers private host state.
     * Optional: a host with no platform DMA policy leaves this callback NULL.
     * PciHostGetDmaDescription fills in the host identification and leaves the
     * caller's output unchanged on failure. A NULL callback means DMA is unsupported.
     *
     * @param host Host whose configured DMA ranges are requested.
     * @param description Receives address ranges and cache policy on success.
     * @return OS_EOK on success, or an error if DMA configuration is unavailable.
     */
    oserr_t (*GetDmaDescription)(
        _In_  struct PciHost*           host,
        _Out_ struct PciDmaDescription* description);

    /**
     * @brief Read from the PCI configuration space.
     *
     * @param host The PCI host to read from.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg Byte offset within the configuration space.
     * @param width Number of bytes to read.
     * @return The value read from the configuration space.
     */
    size_t (*Read)(
        _In_ struct PciHost* host,
        _In_ unsigned int   bus,
        _In_ unsigned int   slot,
        _In_ unsigned int   function,
        _In_ size_t         reg,
        _In_ size_t         width);

    /**
     * @brief Write to the PCI configuration space.
     *
     * @param host The PCI host to write to.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg Byte offset within the configuration space.
     * @param value The value to write.
     * @param width Number of bytes to write.
     */
    void (*Write)(
        _In_ struct PciHost* host,
        _In_ unsigned int   bus,
        _In_ unsigned int   slot,
        _In_ unsigned int   function,
        _In_ size_t         reg,
        _In_ size_t         value,
        _In_ size_t         width);

    /**
     * @brief Releases state used only by this host implementation. Common teardown
     * releases the main I/O mapping, firmware tree reference and host allocation.
     *
     * @param host The PCI host to destroy.
     * @return OS_EOK after controller cleanup, or an error keeping the context and
     *         operations valid for retry. Do not release the main host mapping.
     */
    oserr_t (*Destroy)(
        _In_ struct PciHost* host);

    /**
     * @brief Translate a PCI address to a physical address.
     *
     * @param host The PCI host to use for translation.
     * @param space PCI address-space kind from enum FdtPciSpace.
     * @param address The PCI address to translate.
     * @param length Number of bytes in the address range.
     * @param physicalOut The output physical address.
     * @return An error code indicating success or failure.
     */
    oserr_t (*Translate)(
        _In_  struct PciHost* host,
        _In_  uint32_t        space,
        _In_  uint64_t        address,
        _In_  uint64_t        length,
        _Out_ uint64_t*       physicalOut);

    /**
     * @brief Resolve the interrupt for a PCI device.
     *
     * @param host The PCI host to use for resolution.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param pin PCI interrupt pin, from 1 (INTA) through 4 (INTD).
     * @param lineOut The output interrupt line.
     * @param flagsOut The output interrupt flags.
     * @return An error code indicating success or failure.
     */
    oserr_t (*ResolveInterrupt)(
        _In_  struct PciHost* host,
        _In_  unsigned int   bus,
        _In_  unsigned int   slot,
        _In_  unsigned int   function,
        _In_  unsigned int   pin,
        _Out_ int*           lineOut,
        _Out_ unsigned int*  flagsOut);
};

/**
 * @brief Represents one PCI controller, its segment and bus range, and any
 * firmware data used to describe its devices. Configuration access is provided
 * separately from the way device I/O resources are exposed to drivers.
 */
typedef struct PciHost {
    DeviceIo_t                   IoSpace;
    // Keep completed teardown stages separate from unfinished mapping cleanup.
    int                          IoAcquired;
    int                          Removing;
    // Links unfinished constructor cleanup owned by discovery.
    struct PciHost*              CleanupNext;
    // A failed startup scan retries missing functions before publication.
    oserr_t                      ScanStatus;
    // Terminal startup errors keep this host blocked without repeating its setup.
    oserr_t                      StartupFailure;
    int                          LegacyScan;
    enum PciIoResourcePolicy     IoResourcePolicy;
    int                          IsExtended;
    struct PciHostIdentification Identification;

    const struct PciHostOperations* Operations;
    void*                           OpContext;

    const struct FdtPciHost*   Firmware;
    uuid_t                     InterruptControllerId;
    // One bit per bus number, covering all 256 PCI buses.
    uint8_t                    ScannedBuses[32];
    struct PciDevice*          RootDevice;
    // Includes PCI descriptions and every attached bus, such as RP1.
    struct PciPublication      Publication;
    struct FdtTree*            FirmwareTree;
} PciHost_t;

extern list_t g_pciDevices;
extern list_t g_pciRoots;

/**
 * @brief Keeps firmware data mapped for a host before registering it. If
 * registration fails, the caller keeps the host and this function drops its
 * temporary reference to the firmware data. Successful registration transfers
 * the host to PCI teardown.
 *
 * @param host The host being attached.
 * @param tree Shared firmware tree whose reference count is increased on success,
 *             or NULL for ACPI or legacy hosts.
 * @return OS_EOK on registration, OS_EEXISTS if already attached, or a registration error.
 */
__EXTERN oserr_t
PciHostAttach(
    _In_ PciHost_t*      host,
    _In_ struct FdtTree* tree);

/**
 * @brief Finds a function while the caller holds the PCI critical section.
 *
 * @param segment PCI segment containing the device.
 * @param bus PCI bus number.
 * @param slot Device slot on the bus.
 * @param function Function number within the slot.
 * @return Matching device, or NULL if no discovered device has this address.
 */
__EXTERN struct PciDevice*
PciFindDevice(
    _In_ unsigned int segment,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function);

/**
 * @brief Lock the PCI critical section before accessing shared host or device data.
 */
__EXTERN void
PciCriticalSectionEnter(void);

/**
 * @brief Releases the lock taken by PciCriticalSectionEnter.
 */
__EXTERN void
PciCriticalSectionLeave(void);

#endif // __DEVICED_BUS_PCI_HOST_PRIVATE_H__
