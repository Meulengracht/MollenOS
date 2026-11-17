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

// GICv2 addresses target CPUs with an 8-bit bitmask, limiting it to 8 CPUs.
// Reject any MADT listing more CPUs, because the driver cannot route interrupts
// to extra cores.
#define ARM64_ACPI_GICV2_CPUS 8U

// Cap how many ITS (Interrupt Translation Service) controllers we accept.
// Each entry is checked against all earlier ones for duplicate IDs and
// overlapping register ranges, so an unbounded count could stall boot.
#define ARM64_ACPI_MAX_ITS 256U

// Parsed description of one enabled processor from the MADT.
struct __AcpiCpuDescription {
    // Hardware CPU ID (MPIDR affinity) passed to PSCI when starting the core.
    uint64_t Affinity;
    // ACPI processor UID matching the device in the ACPI namespace.
    uint32_t Uid;
    // GICv2 target bit index (0-7) for this CPU.
    uint32_t InterfaceNumber;
    // Per-CPU GICR redistributor address, used only if MADT omits GICR ranges.
    uint64_t Redistributor;
};

// Temporary ACPI platform state collected during boot initialization.
struct __AcpiPlatformDescription {
    struct Arm64Platform         Platform;
    struct __AcpiCpuDescription* Cpus;
    unsigned int                 Count;
    unsigned int                 BootIndex;
    uint32_t                     GicId;
};

/**
 * @brief Checks the signature, minimum length, and checksum of a mapped ACPI table.
 *
 * ACPICA must already have mapped all Header.Length bytes. By default ACPICA
 * may only log a warning on a bad checksum, so this check enforces it.
 *
 * @param header        Mapped ACPI table header and body.
 * @param signature     Expected four-character table signature (such as "MADT").
 * @param minimumLength Minimum byte size needed by our reader, including the header.
 * @return OS_EOK on success, or OS_EINVALPARAMS if the table fails validation.
 */
__EXTERN oserr_t
Arm64AcpiValidateTable(
    _In_ const ACPI_TABLE_HEADER* header,
    _In_ const char*              signature,
    _In_ size_t                   minimumLength);

/**
 * @brief Looks up a required ACPI table through ACPICA and validates it.
 *
 * ACPICA must already be initialized. If multiple tables share the signature,
 * only the first one is read.
 *
 * @param signature     Four-character ACPI table signature.
 * @param minimumLength Minimum byte size needed by our reader, including the header.
 * @param header        Receives the validated table. Release with AcpiPutTable.
 * @return OS_EOK on success, OS_ENOENT if missing, or an error if lookup or
 *         validation fails.
 */
__EXTERN oserr_t
Arm64AcpiGetTable(
    _In_  const char*         signature,
    _In_  size_t              minimumLength,
    _Out_ ACPI_TABLE_HEADER** header);

/**
 * @brief Reads enabled CPUs and GIC hardware details from the MADT.
 *
 * Supports one GICv2 or GICv3 distributor (the main interrupt controller block).
 * Global platform state stays untouched if anything fails.
 *
 * For GICv3, Interrupt Translation Service (ITS) IDs and register ranges are
 * copied out before releasing the table. No ITS hardware registers are accessed.
 *
 * @param description Receives CPU records and GIC addresses. BootIndex is set
 *                    to the entry matching the boot CPU's hardware ID (MPIDR).
 * @return OS_EOK on success, or an error if the MADT is missing, malformed, or
 *         uses unsupported features.
 */
__EXTERN oserr_t
Arm64AcpiReadMadt(
    _InOut_ struct __AcpiPlatformDescription* description);

/**
 * @brief Reads the non-secure EL1 physical timer interrupt from the GTDT.
 *
 * This is the per-CPU hardware timer that the kernel accesses through CPU
 * system registers.
 *
 * @param platform Receives the timer's Private Peripheral Interrupt (PPI) number
 *                 and kernel interrupt flags.
 * @return OS_EOK on success, or an error if the GTDT is missing or uses an
 *         unsupported timer configuration.
 */
__EXTERN oserr_t
Arm64AcpiReadGtdt(
    _InOut_ struct Arm64Platform* platform);

/**
 * @brief Reads which firmware call method (SMC or HVC) PSCI uses from the FADT.
 *
 * PSCI (Power State Coordination Interface) is the firmware interface used to
 * power on secondary CPUs. The FADT must be revision 5.1 or newer, mark the
 * system as hardware-reduced ACPI (no legacy x86 power hardware), and advertise
 * PSCI support.
 *
 * @param platform Receives whether CPU power-on calls use SMC (secure monitor)
 *                 or HVC (hypervisor).
 * @return OS_EOK on success, or an error if the FADT is missing or fails these
 *         requirements.
 */
__EXTERN oserr_t
Arm64AcpiReadFadt(
    _InOut_ struct Arm64Platform* platform);

/**
 * @brief Frees temporary CPU records, plus unpublished GICR and ITS arrays.
 *
 * @param description Platform description populated during boot. Must have been
 *                    zero-initialized before reading tables.
 * @param published   Non-zero once Platform has been copied to global state, so
 *                    its GICR and ITS arrays are kept alive.
 */
__EXTERN void
Arm64AcpiReleaseDescription(
    _InOut_ struct __AcpiPlatformDescription* description,
    _In_ int                                  published);

#endif //!__VALI_AARCH64_ACPI_TABLES_H__
