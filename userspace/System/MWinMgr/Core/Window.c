/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS Window - Sapphire
*/

/* Includes */
#include <SDL.h>
#include <SDL_image.h>
#include "Scene.h"
#include "Window.h"

/* CLib */
#include <string.h>
#include <stdlib.h>

/* Constructor
 * Allocates a new window of the given
 * dimensions and initializes it */
Window_t *WindowCreate(int Id, const char *Title, Rect_t *Dimensions, 
	int Flags, SDL_Renderer *Renderer)
{
	/* Allocate a new window instance */
	Window_t *Window = (Window_t*)malloc(sizeof(Window_t));
	
	/* Set initial stuff */
	Window->Id = Id;
	Window->Flip = SDL_FLIP_NONE;
	Window->Rotation = 0.0;
	Window->zIndex = 1000;

	/* Convert dims */
	Window->Dimensions.x = Dimensions->x;
	Window->Dimensions.y = Dimensions->y;
	Window->Dimensions.h = Dimensions->h;
	Window->Dimensions.w = Dimensions->w;

	/* Allocate a texture */
	Window->Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGBA8888, 
		SDL_TEXTUREACCESS_STREAMING, Dimensions->w, Dimensions->h);

	/* Done */
	return Window;
}

/* Destructor
 * Cleans up and releases
 * resources allocated */
void WindowDestroy(Window_t *Window)
{
	/* Sanity */
	if (Window == NULL)
		return;

	/* Free resources */
	SDL_DestroyTexture(Window->Texture);
	free(Window);
}

/* Update
 * Updates all neccessary state and
 * buffers before rendering */
void WindowUpdate(Window_t *Window)
{
	/* Sanity */
	if (Window == NULL)
		return;

	//Lock Texture

	//Modify pixels

	//Unlock
}

/* Render
 * Renders the window to the
 * given renderer */
void WindowRender(Window_t *Window, SDL_Renderer *Renderer)
{
	/* Sanity */
	if (Window == NULL)
		return;

	/* Copy window texture to render */
	SDL_RenderCopyEx(Renderer, Window->Texture, NULL, &Window->Dimensions, 
		Window->Rotation, NULL, Window->Flip);
}