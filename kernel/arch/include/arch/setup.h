/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS System Setup Interface
 * - Contains the shared kernel system setup interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_SYSTEMSETUP_H_
#define _MCORE_SYSTEMSETUP_H_

#include <os/osdefs.h>
#include <vboot.h>

/* Setup Systems Definitions 
 * Contains bit definitions and magic constants */
#define SYSTEM_FEATURE_FINALIZE         0x00000010
#define SYSTEM_FEATURE_INTERRUPTS       0x00000020

/* SystemFeaturesInitialize
 * Called by the kernel to initialize a supported system */
KERNELAPI OsStatus_t KERNELABI
SystemFeaturesInitialize(
    _In_ struct VBoot* BootInformation,
    _In_ unsigned int  Systems);

#endif // !_MCORE_SYSTEMSETUP_H_
