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
 */

#ifndef __VALI_ARM64_ITS_H__
#define __VALI_ARM64_ITS_H__

#include <os/osdefs.h>

/**
 * @brief Physical location and ID of one GICv3 Interrupt Translation Service (ITS).
 *
 * On ARM64 systems, PCI devices signal interrupts by writing a message to a
 * memory address, and the ITS is the GICv3 hardware block that receives those
 * writes and delivers them to a CPU.
 *
 * Firmware lists each ITS block in the ACPI MADT table and links devices to it
 * from the IORT table using Identifier. Base and Length give the physical
 * memory range for the ITS control and translation registers. Boot discovery
 * only copies these numbers. Driver code must still map the registers and
 * verify the hardware before handing out interrupts.
 */
typedef struct Arm64ItsDescription {
    uint32_t Identifier;
    uint64_t Base;
    uint64_t Length;
} Arm64ItsDescription_t;

#endif //!__VALI_ARM64_ITS_H__
