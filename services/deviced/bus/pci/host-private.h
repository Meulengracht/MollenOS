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
#include <core/publication.h>
#include <ddk/io.h>
#include <ds/list.h>

struct PciDevice;
struct PciFirmwareMapping;
struct FdtPciHost;
struct PciDmaDescription;

struct PciHostOperations {
    /** 
     * @brief Copy configured DMA ranges without giving callers private host state.
     * Optional: a host with no platform DMA policy leaves this callback NULL.
     * The common wrapper supplies identity and preserves output on failure.
     */
    oserr_t (*GetDmaDescription)(
        struct PciHost*           host,
        struct PciDmaDescription* description);

    /**
     * @brief Read from the PCI configuration space.
     *
     * @param host The PCI host to read from.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg The offset within the configuration space.
     * @param width The size of the read operation.
     * @return The value read from the configuration space.
     */
    size_t (*Read)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        size_t          reg,
        size_t          width);

    /**
     * @brief Write to the PCI configuration space.
     *
     * @param host The PCI host to write to.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param reg The offset within the configuration space.
     * @param width The size of the write operation.
     * @param value The value to write.
     */
    void (*Write)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        size_t          reg,
        size_t          value,
        size_t          width);

    /**
    * @brief Releases state used only by this host implementation. Common teardown
    * releases the main I/O mapping, retained firmware data, and host allocation.
     * @param host The PCI host to destroy.
     */
    void (*Destroy)(struct PciHost*);

    /**
     * @brief Translate a PCI address to a physical address.
     *
     * @param host The PCI host to use for translation.
     * @param space The address space.
     * @param address The PCI address to translate.
     * @param length The length of the address range.
     * @param physicalOut The output physical address.
     * @return An error code indicating success or failure.
     */
    oserr_t (*Translate)(
        struct PciHost* host,
        uint32_t        space,
        uint64_t        address,
        uint64_t        length,
        uint64_t*       physicalOut);

    /**
     * @brief Resolve the interrupt for a PCI device.
     *
     * @param host The PCI host to use for resolution.
     * @param bus The bus number of the target device.
     * @param slot The slot number of the target device.
     * @param function The function number of the target device.
     * @param pin The interrupt pin of the target device.
     * @param lineOut The output interrupt line.
     * @param flagsOut The output interrupt flags.
     * @return An error code indicating success or failure.
     */
    oserr_t (*ResolveInterrupt)(
        struct PciHost* host,
        unsigned int    bus,
        unsigned int    slot,
        unsigned int    function,
        unsigned int    pin,
        int*            lineOut,
        unsigned int*   flagsOut);
};

/**
 * @brief Represents one PCI controller, its segment and bus range, and any
 * firmware data used to describe its devices. Configuration access is provided
 * separately from the way device I/O resources are exposed to drivers.
 */
typedef struct PciHost {
    DeviceIo_t               IoSpace;
    enum PciIoResourcePolicy IoResourcePolicy;
    int                      IsExtended;
    struct PciHostIdentification Identification;

    const struct PciHostOperations* Operations;
    void*                           OpContext;

    const struct FdtPciHost*   Firmware;
    int                        DriversBlocked;
    // 32 possible PCI buses
    uint8_t                    ScannedBuses[32];
    struct PciDevice*          RootDevice;
    // Includes PCI descriptions and every attached bus, such as RP1.
    struct DmPublicationGroup  Publication;
    struct PciFirmwareMapping* FirmwareMapping;
} PciHost_t;

extern list_t g_pciDevices;
extern list_t g_pciRoots;

/**
 * @brief Tracks mapped firmware data while discovery and registered hosts use it.
 * The data is read-only; the reference count is updated during discovery or teardown.
 */
struct PciFirmwareMapping {
    const void*  Blob;
    size_t       Length;
    unsigned int References;
};

/**
 * @brief Releases one user's reference to mapped firmware data. The final release
 * unmaps the data and frees this record.
 *
 * @param mapping The firmware mapping to release.
 */
__EXTERN void
PciFirmwareRelease(
    _In_ struct PciFirmwareMapping* mapping);


/**
 * @brief Keeps firmware data mapped for a host before registering it. If
 * registration fails, the caller keeps the host and this function drops its
 * temporary reference to the firmware data.
 *
 * @param host The host being attached.
 * @param mapping The firmware mapping to retain.
 */
__EXTERN oserr_t
PciHostAttach(
    _In_ PciHost_t*                 host,
    _In_ struct PciFirmwareMapping* mapping);

/**
 * @brief Finds a function while the caller holds the PCI critical section.
 */
__EXTERN struct PciDevice*
PciFindDevice(
    _In_ unsigned int segment,
    _In_ unsigned int bus,
    _In_ unsigned int slot,
    _In_ unsigned int function);

/**
 * @brief Use the pci critical section lock when accessing shared PCI host or device data.
 */
__EXTERN void
PciCriticalSectionEnter(void);

/**
 * @brief Releases the lock taken by PciCriticalSectionEnter.
 */
__EXTERN void
PciCriticalSectionLeave(void);

#endif // __DEVICED_BUS_PCI_HOST_PRIVATE_H__
