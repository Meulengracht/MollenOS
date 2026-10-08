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

#ifndef __DEVICED_BCM_PCI_H__
#define __DEVICED_BCM_PCI_H__

#include <bus/pci/host.h>
#include <ddk/io.h>
#include <firmware/bcm.h>
#include <threads.h>

// Forward declarations
struct PciHostOperations;

/**
 * @brief Register locations and setup functions for one Broadcom chip type.
 *
 * Validate checks firmware settings before any controller writes. Prepare
 * resets the controller and sets up its electrical interface (PHY).
 * ProgramInbound sets the address ranges that devices can use to reach system
 * memory or registers. Start applies connection settings and allows the
 * connected device to start. Stop must also handle an incomplete Prepare.
 * RegisterLength is in bytes; ConfigIndex and ConfigData are register offsets
 * used to select a PCI device and access its configuration.
 */
struct BcmPciVariant {
    enum FdtPciHostType Type;
    size_t              RegisterLength;
    size_t              ConfigIndex;
    size_t              ConfigData;
    
    oserr_t (*Validate)(const struct FdtPciHost*, unsigned int*);
    oserr_t (*Prepare)(PciHost_t*, const struct FdtPciHost*);
    oserr_t (*ProgramInbound)(PciHost_t*, const struct FdtPciHost*, unsigned int);
    oserr_t (*Start)(PciHost_t*, const struct FdtPciHost*);
    void    (*Stop)(PciHost_t*);
};

/**
 * @brief Saved firmware settings and setup state for one PCI host controller.
 *
 * ConfigLock prevents two callers from selecting and accessing different PCI
 * devices at the same time. Ready means the connection is active and callers
 * can access device configuration registers. Firmware contains pointers into
 * the original device-tree data; the caller must keep that data unchanged and
 * mapped, along with the main controller registers, until BcmPciDestroy returns.
 */
struct BcmPciHost {
    struct FdtPciHost           Firmware;
    const struct BcmPciVariant* Variant;
    mtx_t                       ConfigLock;
    int                         Ready;

    // BCM2712 resets the host through separate registers described by firmware.
    DeviceIo_t BridgeReset;
    uint32_t   BridgeResetId;
    int        BridgeResetMapped;
};

__EXTERN const struct BcmPciVariant g_bcm2711PciVariant;
__EXTERN const struct BcmPciVariant g_bcm2712PciVariant;

/**
 * @brief Find the register layout and setup functions for a supported chip.
 *
 * @param type Host controller type read from firmware.
 * @return BCM2711 or BCM2712 settings, or NULL for an unsupported type.
 */
__EXTERN const struct BcmPciVariant*
BcmPciGetVariant(
    _In_ enum FdtPciHostType type);

/**
 * @brief Set up a controller and make configuration access available once connected.
 *
 * @param bus Host with its main registers already mapped and acquired. Operations
 *            and OpContext must be NULL. Device drivers remain blocked after setup.
 * @param controller Receives the host state; keep it available until Destroy returns.
 * @param firmware Settings and resources to use. Its referenced device-tree data
 *                 must remain mapped and unchanged while the host is in use.
 * @return OS_EOK when the connection is active, or an error for invalid inputs,
 *         unsupported settings, failed allocation or hardware setup, or a timeout.
 *         Hardware setup failures call the chip's Stop function before returning.
 */
__EXTERN oserr_t
BcmPciInitialize(
    _InOut_ PciHost_t*               bus,
    _Out_   struct BcmPciHost*       controller,
    _In_    const struct FdtPciHost* firmware);

/**
 * @brief Stop the controller and remove its configuration-access callbacks.
 *
 * @param bus Host to stop. All callers must finish using it before this call.
 * @param controller State previously attached to bus by BcmPciInitialize.
 *                   The caller releases the main register mapping afterward.
 *                   NULL arguments or a controller not attached to bus are ignored.
 */
__EXTERN void
BcmPciDestroy(
    _InOut_ PciHost_t*         bus,
    _InOut_ struct BcmPciHost* controller);

/**
 * @brief Find the PCI address a device can use to access a buffer in system memory.
 *
 * Direct memory access (DMA) lets devices read or write memory themselves.
 * The entire buffer must fit one firmware address range identified as RAM.
 *
 * @param controller Initialized host with an active connection.
 * @param physical CPU physical address of the buffer's first byte.
 * @param length Buffer size in bytes, greater than zero.
 * @param address Receives the PCI bus address on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs or an unready
 *         controller, or OS_ENOENT if no declared RAM range contains the buffer.
 */
__EXTERN oserr_t
BcmPciDmaAddress(
    _In_  const struct BcmPciHost* controller,
    _In_  uint64_t                 physical,
    _In_  uint64_t                 length,
    _Out_ uint64_t*                address);

/**
 * @brief Pause the calling thread to give hardware time to respond.
 *
 * @param milliseconds Duration to sleep; this does not check hardware status.
 */
__EXTERN void
BcmPciDelay(
    _In_ long milliseconds);

/**
 * @brief Read a 32-bit controller register, clear selected bits, and set new bits.
 *
 * @param bus Host whose registers are mapped.
 * @param reg Byte offset of the register.
 * @param mask Bits to clear from the old value before applying value.
 * @param value Bits to set; these may also include bits outside mask.
 * @return Result of writing (old value & ~mask) | value to the register.
 */
__EXTERN oserr_t
BcmPciUpdate(
    _In_ PciHost_t* bus,
    _In_ size_t     reg,
    _In_ uint32_t   mask,
    _In_ uint32_t   value);

/**
 * @brief Check that the controller is the CPU end of an active PCI Express connection.
 *
 * @param bus Host whose status register should be read.
 * @return 1 if the controller is a root port (the CPU end) and both the electrical
 *         connection and data transfer are ready, otherwise 0.
 */
__EXTERN int
BcmPciLinkUp(
    _In_ PciHost_t* bus);

/**
 * @brief Make a physical register range accessible to this service.
 *
 * @param io Receives the created and acquired mapping on success. The caller
 *           must later call ReleaseDeviceIo and DestroyDeviceIo.
 * @param base CPU physical address of the first register byte.
 * @param length Size in bytes; the entire range must fit this build's address size.
 * @return OS_EOK on success, OS_ENOTSUPPORTED for an empty or out-of-range
 *         region, or an error from creating or acquiring the mapping.
 */
__EXTERN oserr_t
BcmPciMapRegisters(
    _Out_ DeviceIo_t* io,
    _In_  uint64_t    base,
    _In_  uint64_t    length);

/**
 * @brief Run resistor calibration and check for completion with a limited wait.
 *
 * RESCAL is the hardware block that calibrates the electrical interface's
 * resistors. Check it up to ten times, with a 1 ms pause between checks.
 *
 * @param dependencies Firmware description of the calibration registers. If
 *                     ResetLength is zero, return success without doing anything.
 * @param reuseCompleted Nonzero to keep an already completed result. Pi 5 ports
 *                       share calibration, so restarting it can affect another port.
 * @return OS_EOK if calibration completes, is reused, or is absent; otherwise
 *         a mapping, register-write, sleep, or timeout error.
 */
__EXTERN oserr_t
BcmPciRescal(
    _In_ const struct FdtPciDependencies* dependencies,
    _In_ int                              reuseCompleted);

#endif
