/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __TYPES_SIGNAL_H__
#define __TYPES_SIGNAL_H__

enum OSPageFaultCode {
    // Indicate that a standard allocated page was hit, and the address was
    // successfully mapped.
    OSPAGEFAULT_RESULT_MAPPED,
    // Indicate that a guard page was hit, meaning a likely stack overflow
    // has occurred in a stack region. No mapping was created and this is a fatal
    // error code.
    OSPAGEFAULT_RESULT_OVERFLOW,
    // Indicate that a trap page was hit and successfully mapped. This is
    // a separate error code as there should still be an exception propagated
    // to userspace, to inform of the trap.
    OSPAGEFAULT_RESULT_TRAP,
    // Indicate that the page-fault resulted in a failed mapping
    // and that it could not be fixed.
    OSPAGEFAULT_RESULT_FAULT,
};

/**
 * @brief Flags
 */
#define SIGNAL_FLAG_SEPERATE_STACK 0x00000001U // Signal is executing in a separate stack
#define SIGNAL_FLAG_HARDWARE_TRAP  0x00000002U // Signal originated from a hardware signal
#define SIGNAL_FLAG_PAGEFAULT      0x00000004U // Signal contains a page-fault address and code
#define SIGNAL_FLAG_NO(flags)      ((flags >> 24) && 0xFF)

#endif //!__TYPES_SIGNAL_H__
