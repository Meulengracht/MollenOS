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
* MollenOS Scene Manager - Sapphire
*/

/* Includes */
#include <SDL.h>
#include <SDL_image.h>
#include "Scene.h"
#include "Window.h"

/* CLib */
#include <string.h>
#include <stdlib.h>

/* Initializor
 * Creates a new scene */
Scene_t *SceneCreate(int Id, Rect_t *Dimensions, SDL_Renderer *Renderer)
{
	/* Variables */
	SDL_Texture *Background = NULL;
	Scene_t *Scene = NULL;

	/* Allocate a scene instance and reset it */
	Scene = (Scene_t*)malloc(sizeof(Scene_t));
	memset(Scene, 0, sizeof(Scene_t));
	
	/* Setup */
	Scene->Id = Id;
	Scene->Windows = ListCreate(KeyInteger, LIST_NORMAL);
	memcpy(&Scene->Dimensions, Dimensions, sizeof(Rect_t));

	/* Load default background for this scene */
	Background = IMG_LoadTexture(Renderer, "Themes/Default/GfxBg.png");
	if (Background == NULL) {
		MollenOSSystemLog("BACKGROUND::SDL Error: (%s)", SDL_GetError());
		for (;;);
	}

	/* Allocate two textures, one as a backbuffer
	 * and the second one for a scaled background image */
	Scene->Background = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_TARGET, Dimensions->w, Dimensions->h);
	Scene->Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_TARGET, Dimensions->w, Dimensions->h);

	/* Scale background image to our 
	 * newly allocated background texture */
	SDL_SetRenderTarget(Renderer, Scene->Background);
	SDL_RenderCopy(Renderer, Background, NULL, NULL);
	SDL_SetRenderTarget(Renderer, NULL);

	/* Set blend mode for the backbuffer 
	 * texture, we only do this for it */
	SDL_SetTextureBlendMode(Scene->Texture, SDL_BLENDMODE_BLEND);

	/* Cleanup the allocated image */
	SDL_DestroyTexture(Background);

	/* Done! */
	return Scene;
}

/* Destructor
 * Cleans up all windows
 * and releases resources allocated */
void SceneDestroy(Scene_t *Scene)
{
	/* Sanity */
	if (Scene == NULL)
		return;

	/* Iterate through all nodes
	 * and destroy windows */
	foreach(wNode, Scene->Windows)
	{
		/* Cast */
		Window_t *Window = (Window_t*)wNode->Data;

		/* Destroy */
		WindowDestroy(Window);
	}

	/* Cleanup SDL */
	SDL_DestroyTexture(Scene->Background);
	SDL_DestroyTexture(Scene->Texture);

	/* Cleanup */
	ListDestroy(Scene->Windows);
	free(Scene);
}

/* Add Window
 * Adds a newly created window to the
 * given scene. The window is not immediately
 * rendered before a call to Render */
void SceneAddWindow(Scene_t *Scene, Window_t *Window)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (Scene == NULL
		|| Window == NULL)
		return;

	/* Append window to scene */
	Key.Value = Window->Id;
	ListAppend(Scene->Windows, ListCreateNode(Key, Key, Window));
}

/* Get Window
 * Looks up a window by id in the given scene
 * returns NULL if none is found */
Window_t *SceneGetWindow(Scene_t *Scene, int WindowId)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (Scene == NULL)
		return NULL;

	/* Setup key */
	Key.Value = WindowId;

	/* Lookup node by Id */
	return (Window_t*)ListGetDataByKey(Scene->Windows, Key, 0);
}

/* Get Active Window
 * Looks up the active window in the given scene
 * returns NULL if none is found */
Window_t *SceneGetActiveWindow(Scene_t *Scene)
{
	/* Sanity */
	if (Scene == NULL
		|| Scene->Windows->Tailp == NULL)
		return NULL;

	/* Get last window, it's the upper  */
	return (Window_t*)Scene->Windows->Tailp->Data;
}

/* Update
 * This updates any changes to windows
 * for this scene, but only for the given rectangle */
void SceneUpdate(Scene_t *Scene, Rect_t *DirtyArea)
{
	/* Sanity */
	if (Scene == NULL)
		return;

	/* Append dirty rectangles */
	if (DirtyArea == NULL || Scene->ValidRectangles == -1) {
		Scene->ValidRectangles = -1;
	}
	else {
		memcpy(&Scene->Dirty[Scene->ValidRectangles], DirtyArea, sizeof(Rect_t));
		Scene->ValidRectangles++;
	}
}

/* Render
 * This renders all windows for this scene */
void SceneRender(Scene_t *Scene, SDL_Renderer *Renderer)
{
	/* Variables */
	SDL_Rect InvalidationRect;

	/* Sanity */
	if (Scene == NULL
		|| Renderer == NULL)
		return;

	/* Change render target to backbuffer */
	SDL_SetRenderTarget(Renderer, Scene->Texture);

	/* Are we rerendering entire scene? */
	if (Scene->ValidRectangles == -1) {

		/* Start by rendering background */
		SDL_RenderCopy(Renderer, Scene->Background, NULL, NULL);

		/* Render windows */
		foreach(wNode, Scene->Windows)
		{
			/* Cast */
			Window_t *Window = (Window_t*)wNode->Data;

			/* Update window */
			WindowUpdate(Window, NULL);

			/* Render window */
			WindowRender(Window, Renderer, NULL);
		}

		/* Change target back to screen */
		SDL_SetRenderTarget(Renderer, NULL);

		/* Render */
		SDL_RenderCopy(Renderer, Scene->Texture, NULL, NULL);
	}
	else if (Scene->ValidRectangles > 0)
	{
		/* Go through dirty rectangles and update/render */
		for (int i = 0; i < Scene->ValidRectangles; i++) 
		{
			/* Copy data over */
			InvalidationRect.x = Scene->Dirty[i].x;
			InvalidationRect.y = Scene->Dirty[i].y;
			InvalidationRect.w = Scene->Dirty[i].w;
			InvalidationRect.h = Scene->Dirty[i].h;

			/* Start by rendering background */
			SDL_RenderCopy(Renderer, Scene->Background, &InvalidationRect, &InvalidationRect);

			/* Render windows */
			foreach(wNode, Scene->Windows)
			{
				/* Cast */
				Window_t *Window = (Window_t*)wNode->Data;

				/* Update window */
				WindowUpdate(Window, &Scene->Dirty[i]);

				/* Render window */
				WindowRender(Window, Renderer, &Scene->Dirty[i]);
			}
		}

		/* Change target back to screen */
		SDL_SetRenderTarget(Renderer, NULL);

		/* Render */
		for (int i = 0; i < Scene->ValidRectangles; i++) 
		{
			/* Copy data over */
			InvalidationRect.x = Scene->Dirty[i].x;
			InvalidationRect.y = Scene->Dirty[i].y;
			InvalidationRect.w = Scene->Dirty[i].w;
			InvalidationRect.h = Scene->Dirty[i].h;

			/* Write to screen */
			SDL_RenderCopy(Renderer, Scene->Texture, &InvalidationRect, &InvalidationRect);
		}
	}

	/* Reset dirties */
	Scene->ValidRectangles = 0;
}
