/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Machine Structure
 * - Is the definition of what makes the currently running machine. From this
 *   structure all components in the system are accessible.
 */

#ifndef __VALI_MACHINE__
#define __VALI_MACHINE__

#include <os/osdefs.h>
#include <multiboot.h>

// Components
#include <component/domain.h>
#include <component/cpu.h>
#include <component/ic.h>

typedef struct _SystemMachine {
    // System Information
    char                    Architecture[16];
    char                    Author[32];
    char                    Date[16];
    unsigned                VersionMajor;
    unsigned                VersionMinor;
    unsigned                VersionRevision;
    Multiboot_t             BootInformation;

    // Hardware information
    Collection_t            SystemDomains;

} SystemMachine_t;

/* GetMachine
 * Retrieves a pointer for the machine structure. */
KERNELAPI SystemMachine_t* KERNELABI
GetMachine(void);

#endif // !__VALI_MACHINE__
