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

#ifndef __KERNEL_ACPI_PCI_H__
#define __KERNEL_ACPI_PCI_H__

#include <acpiinterface.h>
#include <ddk/pci/acpi.h>

/**
 * @brief Validates the MCFG table, which must be done before namespace loading
 * can run PCI_Config AML. The ACPICA table manager and the kernel memory
 * manager must both be initialized. Once validated, the table is never released.
 */
void
AcpiPciInitializeConfig(void);

/**
 * @brief Builds the list of PCI roots after the namespace has been initialized.
 * The list does not change afterwards. Must be called once before userspace is
 * started. If any root is invalid, the whole list is rejected, as the missing
 * ranges could make the remaining roots look unambiguous when they are not.
 */
void
AcpiPciScan(void);

/**
 * @brief Answers an ACPI PCI query from userspace, using the list of PCI roots
 * built by AcpiPciScan.
 *
 * @param query  The request, already copied from userspace by the syscall layer.
 * @param result A zero-initialized result, which is only copied out on success.
 * @return OS_EOK, OS_ENOENT if the item does not exist, or a validation/AML error.
 */
oserr_t
AcpiPciQuery(
    _In_  const struct AcpiPciQuery* query,
    _Out_ union AcpiPciResult*      result);

/**
 * @brief Looks up an interrupt route in the _PRT of a single bus. No swizzling
 * through PCI bridges is done here.
 *
 * @param bus    The namespace bus, selected by the already validated bridge path.
 * @param query  The validated device/function and INTx pin (1-based).
 * @param result Receives the full GSI and the interrupt conform flags.
 * @return OS_ENOENT if no route matched. Any other error must be returned as
 *         is, without swizzling.
 */
oserr_t
AcpiPciRoute(
    _In_  ACPI_HANDLE               bus,
    _In_  const struct AcpiPciQuery* query,
    _Out_ struct AcpiPciInterrupt*   result);

/**
 * @brief Reads or writes PCI configuration space for AML, with bounds and
 * segment checks. No hosts are enumerated or device resources allocated.
 *
 * @param id    ACPICA's segment, bus, device and function.
 * @param reg   Byte offset in the function's configuration space.
 * @param value The value to write, or receives the value read.
 * @param width Access size (8, 16 or 32 bits).
 * @param write Non-zero for a write.
 * @return ACPICA status. Invalid or unsupported accesses never touch hardware.
 */
ACPI_STATUS
AcpiPciConfig(
    _In_    ACPI_PCI_ID* id,
    _In_    UINT32       reg,
    _InOut_ UINT64*      value,
    _In_    UINT32       width,
    _In_    int          write);

#endif //!__KERNEL_ACPI_PCI_H__
