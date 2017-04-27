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

/* Includes
 * - System */
#include "sdlrenderer.h"
#include <os/utils.h>

// Constructor 
// Initializes and prepares the sdl library
CSdlRenderer::CSdlRenderer()
{
	// Null out members
	m_pRenderer = NULL;
	m_pTarget = NULL;

	// TRACE
	TRACE("CSdlRenderer::CSdlRenderer()");

	// Initialize our rendering engine
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		ERROR("Failed to initialize SDL: %s", SDL_GetError());
	}
}

// Create
// Creates and initializes the renderer for usage
bool CSdlRenderer::Create(Rect_t *Dimensions)
{
	// Create the primary window
	m_pTarget = SDL_CreateWindow("Sapphire", 
		Dimensions->x, Dimensions->y,
		Dimensions->w, Dimensions->h,
		SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);

	// Sanitize result
	if (m_pTarget == NULL) {
		ERROR("Failed to create SDL window: %s", SDL_GetError());
		return false;
	}

	// Create the primary renderer
	m_pRenderer = SDL_CreateRenderer(m_pTarget, -1,
		SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);

	// Sanitize result
	if (m_pRenderer == NULL) {
		ERROR("Failed to create SDL renderer: %s", SDL_GetError());
		SDL_DestroyWindow(m_pTarget);
		m_pTarget = NULL;
		return false;
	}

	// Initialize image libraries
	if (!IMG_Init(IMG_INIT_PNG)) {
		ERROR("Failed to initialize image-libraries");
		SDL_DestroyRenderer(m_pRenderer);
		SDL_DestroyWindow(m_pTarget);
		m_pRenderer = NULL;
		m_pTarget = NULL;
		return false;
	}

	// No errors
	return true;
}
