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
#include "pci.h"

/** Resolved RESCAL, bridge reset selector, and always-on fixed reference clock. */
struct FdtPciDependencies {
    uint64_t ResetBase;
    uint64_t ResetLength;
    uint32_t ClockFrequency;
    uint32_t BridgeResetController;
    uint32_t BridgeResetId;
    uint64_t BridgeResetBase;
    uint64_t BridgeResetLength;
};

/** @brief Resolves the declared MSI parent and, for MIP, its SPI range and offset. */
extern oserr_t
FdtResolvePciMsi(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciMsi* msi);

/** @brief Resolves supported host dependencies; declared unknown providers fail closed. */
extern oserr_t
FdtResolvePciDependencies(
        _In_ const struct FdtPciHost* host,
        _Out_ struct FdtPciDependencies* dependencies);

/** @brief Match Broadcom hosts and decode their firmware link policy. */
int
FdtBcmHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type);
oserr_t
FdtBcmHostPolicy(
    _In_ const struct FdtNode* node,
    _In_ struct FdtPciHost* host);
#endif
