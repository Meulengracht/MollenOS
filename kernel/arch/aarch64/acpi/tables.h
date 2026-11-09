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

#ifndef __VALI_AARCH64_ACPI_TABLES_H__
#define __VALI_AARCH64_ACPI_TABLES_H__

#include <acpi.h>
#include "../common/private.h"

// GICv2 targets CPUs with an 8-bit mask, so it can reach at most 8 CPUs. A
// MADT with more CPUs is rejected, as the interrupt driver could not address
// the extra ones.
#define ARM64_ACPI_GICV2_CPUS 8U

// Represents one MADT processor
struct __AcpiCpuDescription {
    // Affinity identifies the CPU to PSCI
    uint64_t Affinity;
    // Uid identifies its ACPI namespace device
    uint32_t Uid;
    // InterfaceNumber identifies its GICv2 target bit
    uint32_t InterfaceNumber;
    // Redistributor is the GICR base of this CPU, only used if MADT has no GICR ranges
    uint64_t Redistributor;
};

// Acpi information gathered during initialization
struct __AcpiPlatformDescription {
    struct Arm64Platform        Platform;
    struct __AcpiCpuDescription* Cpus;
    unsigned int                Count;
    unsigned int                BootIndex;
    uint32_t                    GicId;
};

/**
 * @brief Checks the signature, minimum length and checksum of a mapped ACPI
 * table.
 *
 * All Header.Length bytes of the table must already be mapped by ACPICA. By
 * default ACPICA may only warn about a bad checksum, so we check it here
 * ourselves.
 *
 * @param header The table, with its header and body mapped.
 * @param signature The expected four-character signature.
 * @param minimumLength The number of bytes we read from the table, including
 *                      the ACPI header.
 * @return OS_EOK, or OS_EINVALPARAMS if the table is malformed.
 */
__EXTERN oserr_t
Arm64AcpiValidateTable(
    _In_ const ACPI_TABLE_HEADER* header,
    _In_ const char*              signature,
    _In_ size_t                   minimumLength);

/**
 * @brief Gets a required ACPI table from ACPICA and checks it.
 *
 * ACPICA must already be initialized. Only the first table with the given
 * signature is used.
 *
 * @param signature The expected signature.
 * @param minimumLength The number of bytes we read from the table, including
 *                      the ACPI header.
 * @param header Receives the checked table. Release it with AcpiPutTable.
 * @return OS_EOK, OS_ENOENT if the table is missing, or an error if the lookup
 *         or the checks fail.
 */
__EXTERN oserr_t
Arm64AcpiGetTable(
    _In_  const char*         signature,
    _In_  size_t              minimumLength,
    _Out_ ACPI_TABLE_HEADER** header);

/**
 * @brief Reads the enabled CPUs and the GIC interrupt controller from the
 * MADT.
 *
 * A single GICv2 or GICv3 distributor is supported. Nothing global is changed
 * here, so on failure the platform state is left as it was.
 *
 * @param description Receives the CPUs and GIC addresses. BootIndex is set to
 *                    the entry that matches the MPIDR of the CPU we run on.
 * @return OS_EOK, or an error if the MADT is missing, malformed or describes
 *         something we don't support.
 */
__EXTERN oserr_t
Arm64AcpiReadMadt(
    _InOut_ struct __AcpiPlatformDescription* description);

/**
 * @brief Reads the interrupt of the non-secure EL1 physical timer from the
 * GTDT. This is the timer accessed through system registers.
 *
 * @param platform Receives the timer's PPI number and its kernel interrupt
 *                 flags.
 * @return OS_EOK, or an error if the GTDT is missing or describes a timer we
 *         don't support.
 */
__EXTERN oserr_t
Arm64AcpiReadGtdt(
    _InOut_ struct Arm64Platform* platform);

/**
 * @brief Reads the PSCI conduit from the FADT.
 *
 * The FADT must be revision 5.1 or newer, describe a hardware-reduced ACPI
 * platform and report PSCI support.
 *
 * @param platform Receives whether later CPU_ON calls use SMC or HVC.
 * @return OS_EOK, or an error if the FADT is missing or the firmware does not
 *         meet these requirements.
 */
__EXTERN oserr_t
Arm64AcpiReadFadt(
    _InOut_ struct Arm64Platform* platform);

/**
 * @brief Frees the temporary CPU records, and the GICR ranges if they were
 * never published.
 *
 * @param description The description that was read. It must have been zeroed
 *                    before the tables were read.
 * @param published   Non-zero once Platform has been copied to its permanent
 *                    location.
 */
__EXTERN void
Arm64AcpiReleaseDescription(
    _InOut_ struct __AcpiPlatformDescription* description,
    _In_ int                                  published);

#endif //!__VALI_AARCH64_ACPI_TABLES_H__
