/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * MMU Interface
 * - Contains a glue layer to access hardware functionality
 *   that all sub-layers / architectures must conform to
 */
#ifndef __SYSTEM_MMU_INTEFACE_H__
#define __SYSTEM_MMU_INTEFACE_H__

#include <os/osdefs.h>

extern OsStatus_t InitializeVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t CloneVirtualSpace(SystemMemorySpace_t*, SystemMemorySpace_t*, int);
extern OsStatus_t DestroyVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t SwitchVirtualSpace(SystemMemorySpace_t*);

extern OsStatus_t GetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t*);
extern OsStatus_t SetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t);

extern uintptr_t  GetVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);

extern OsStatus_t CommitVirtualPageMapping(SystemMemorySpace_t*, PhysicalAddress_t, VirtualAddress_t);
extern OsStatus_t SetVirtualPageMapping(SystemMemorySpace_t*, PhysicalAddress_t, VirtualAddress_t, Flags_t);
extern OsStatus_t ClearVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);

#endif //!__SYSTEM_MMU_INTEFACE_H__
