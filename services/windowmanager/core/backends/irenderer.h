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
 * MollenOS Sapphire - Render Interface
 * - Contains the definition of a renderer backend that all renders
 *   must implement
 */

#ifndef _SAPPHIRE_RENDERER_INTERFACE_H_
#define _SAPPHIRE_RENDERER_INTERFACE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>

/* IRenderer
 * Defines the methods that all renderer backends must implement */
class IRenderer
{
public:
	// Create
	// Creates and initializes the renderer for usage
	virtual bool Create(Rect_t *Dimensions);

	// Destroy
	// Destroys and cleansup the renderer resources
	virtual bool Destroy();
};

#endif //!_SAPPHIRE_RENDERER_INTERFACE_H_
