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
 * MollenOS Sapphire - SDL Renderer Backend
 * - Contains the implementation of the SDL renderer
 */

#ifndef _SAPPHIRE_RENDERER_SDL_H_
#define _SAPPHIRE_RENDERER_SDL_H_

/* Includes
 * - System */
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include "../irenderer.h"

/* CSdlRenderer
 * Implementation of IRenderer with SDL as backend */
class CSdlRenderer : public IRenderer
{
public:
	CSdlRenderer();
	~CSdlRenderer();

	// Create
	// Creates and initializes the renderer for usage
	bool Create(Rect_t *Dimensions);

	// Destroy
	// Destroys and cleansup the renderer resources
	bool Destroy();

private:
	SDL_Renderer*		m_pRenderer;
	SDL_Window*			m_pTarget;
};

#endif //!_SAPPHIRE_RENDERER_SDL_H_
