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

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <multiboot.h>

/* Setup Systems Definitions 
 * Contains bit definitions and magic constants */
#define SYSTEM_FEATURE_INITIALIZE       0x00000001
#define SYSTEM_FEATURE_OUTPUT           0x00000002
#define SYSTEM_FEATURE_MEMORY           0x00000004
#define SYSTEM_FEATURE_TOPOLOGY         0x00000008 // Hardware Topology
#define SYSTEM_FEATURE_FINALIZE         0x00000010

#define SYSTEM_FEATURE_INTERRUPTS       0x00000020
#define SYSTEM_FEATURE_ACPI             0x00000040
#define SYSTEM_FEATURE_THREADING        0x00000080
#define SYSTEM_FEATURE_TIMERS           0x00000100


/* MCoreInitialize
 * Callable by the architecture layer to initialize the kernel */
KERNELAPI
void
KERNELABI
MCoreInitialize(
	_In_ Multiboot_t *BootInformation);

/* SystemFeaturesQuery
 * Called by the kernel to get which systems we support */
KERNELAPI
OsStatus_t
KERNELABI
SystemFeaturesQuery(
    _In_ Multiboot_t *BootInformation,
    _Out_ Flags_t *SystemsSupported);

/* SystemFeaturesInitialize
 * Called by the kernel to initialize a supported system */
KERNELAPI
OsStatus_t
KERNELABI
SystemFeaturesInitialize(
    _In_ Multiboot_t *BootInformation,
    _In_ Flags_t Systems);

#endif // !_MCORE_SYSTEMSETUP_H_
