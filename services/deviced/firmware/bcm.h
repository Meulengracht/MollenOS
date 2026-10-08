/**
 * Copyright 2026, Philip Meulengracht
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

#ifndef DEVICED_FIRMWARE_BCM_H
#define DEVICED_FIRMWARE_BCM_H

#include <firmware/pci.h>

/** Registers and settings needed to reset and clock a Broadcom PCI host.
 * ResetBase and ResetLength describe the RESCAL hardware used for calibration.
 * BridgeResetController and BridgeResetId identify the controller and reset
 * signal for the PCI bridge; BridgeResetBase and BridgeResetLength locate its
 * registers. ClockFrequency is the always-on reference clock frequency in hertz. */
struct FdtPciDependencies {
    uint64_t ResetBase;
    uint64_t ResetLength;
    uint32_t ClockFrequency;
    uint32_t BridgeResetController;
    uint32_t BridgeResetId;
    uint64_t BridgeResetBase;
    uint64_t BridgeResetLength;
};

/**
 * @brief Find the controller that handles the host's message-signaled interrupts.
 *
 * For a Broadcom MIP controller, also read the range of GIC interrupt lines and
 * the offset used to convert a message number into an interrupt line.
 *
 * @param host Host whose "msi-parent" property names the controller.
 * @param msi Receives the controller's registers and interrupt details on success.
 * @return OS_EOK on success, or an error if missing, malformed, or unsupported.
 */
extern oserr_t
FdtResolvePciMsi(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciMsi* msi);

/**
 * @brief Read the reset controls and fixed clock declared for a Broadcom host.
 *
 * A declared clock or reset that this code does not support causes an error.
 * Missing optional resources leave their output fields zero.
 *
 * @param host Host whose reset and clock properties should be read.
 * @param dependencies Receives the reset and clock settings on success.
 * @return OS_EOK on success, or an error for malformed or unsupported settings.
 */
extern oserr_t
FdtResolvePciDependencies(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciDependencies* dependencies);

/**
 * @brief Check whether a node describes a supported Broadcom PCI host.
 *
 * @param node Node whose "compatible" names should be checked.
 * @param type Receives the controller type when a match is found.
 * @return 1 for a supported Broadcom host, otherwise 0.
 */
int
FdtBcmHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type);
/**
 * @brief Read firmware settings for the host's PCI Express connection.
 *
 * @param node Node containing connection speed, lane count, and clock settings.
 * @param host Host to update; initialize its fields to zero before calling.
 *             Some fields may already be updated when an error is returned.
 * @return OS_EOK on success, or OS_EINVALPARAMS for invalid connection settings.
 */
oserr_t
FdtBcmHostPolicy(
    _In_ const struct FdtNode* node,
    _In_ struct FdtPciHost* host);
#endif
