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
 */

#ifndef __ETHERNET_MDIO_H__
#define __ETHERNET_MDIO_H__

#include <ddk/ddkdefs.h>

// Clause 22 uses five bits for both the PHY address and the register number.
#define MDIO_MAX_ADDRESS  31U
#define MDIO_MAX_REGISTER 31U

/**
 * @brief MAC-specific Clause 22 transactions and a sleep operation.
 * Read and Write must bound hardware waits and report bus errors. The caller
 * must serialize all PHY operations, including multi-register shadow accesses.
 * DelayUs waits at least the requested microseconds or returns an error.
 */
struct MdioOperations {
    oserr_t (*Read)(void* context, uint8_t address, uint8_t reg, uint16_t* value);
    oserr_t (*Write)(void* context, uint8_t address, uint8_t reg, uint16_t value);
    oserr_t (*DelayUs)(void* context, uint32_t microseconds);
};

/**
 * @brief MDIO callbacks and context for the PHY address selected by firmware.
 */
typedef struct MdioBus {
    const struct MdioOperations* Operations;
    void*                        Context;
    uint8_t                      Address;
} MdioBus_t;

/**
 * @brief Validate and select one Clause 22 PHY address.
 * @param bus Output endpoint.
 * @param operations Complete, long-lived MAC callback table.
 * @param context Callback context.
 * @param address Firmware PHY address, from zero through 31.
 * @return OS_EOK on success, or OS_EINVALPARAMS with bus unchanged.
 */
__EXTERN oserr_t
MdioInitialize(
    _Out_ MdioBus_t*                   bus,
    _In_  const struct MdioOperations* operations,
    _In_  void*                        context,
    _In_  uint32_t                     address);

/**
 * @brief Read a register from an initialized, serialized endpoint.
 * @param bus Fixed PHY endpoint.
 * @param reg Clause 22 register, zero through 31.
 * @param value Result, unchanged on error.
 * @return MAC transaction status, or OS_EINVALPARAMS if reg exceeds 31.
 */
__EXTERN oserr_t
MdioRead(
    _In_  MdioBus_t* bus,
    _In_  uint8_t    reg,
    _Out_ uint16_t*  value);

/**
 * @brief Write a register to an initialized, serialized endpoint.
 * @param bus Fixed PHY endpoint.
 * @param reg Clause 22 register, zero through 31.
 * @param value Register value.
 * @return MAC transaction status, or OS_EINVALPARAMS if reg exceeds 31.
 *         A failed write may have reached the PHY.
 */
__EXTERN oserr_t
MdioWrite(
    _In_ MdioBus_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   value);

/**
 * @brief Replace selected bits without changing the remaining register fields.
 * The caller must serialize the complete operation. An unchanged value skips
 * the write. Registers that require a write even for unchanged bits must use
 * MdioWrite directly.
 * @param bus Serialized endpoint.
 * @param reg Clause 22 register.
 * @param mask Bits to replace.
 * @param value New masked bits.
 * @return OS_EOK if unchanged, otherwise the read or write status.
 *         OS_EINVALPARAMS if reg exceeds 31.
 */
__EXTERN oserr_t
MdioModify(
    _In_ MdioBus_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   mask,
    _In_ uint16_t   value);

#endif //!__ETHERNET_MDIO_H__
