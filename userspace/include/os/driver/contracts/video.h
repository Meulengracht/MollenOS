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
 * MollenOS MCore - Contract Definitions & Structures (Video Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_VIDEO_INTERFACE_H_
#define _CONTRACT_VIDEO_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/contracts/base.h>
#include <os/osdefs.h>

PACKED_TYPESTRUCT(VideoDescriptor, {
	uintptr_t				FrameBufferAddress;
	
	size_t				BytesPerScanline;
	size_t				Height;
	size_t				Width;
	int					Depth;

	int					RedPosition;
	int					BluePosition;
	int					GreenPosition;
	int					ReservedPosition;

	int					RedMask;
	int					BlueMask;
	int					GreenMask;
	int					ReservedMask;
});

#endif //!_CONTRACT_VIDEO_INTERFACE_H_
