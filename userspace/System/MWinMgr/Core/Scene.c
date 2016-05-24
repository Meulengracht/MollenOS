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
	/* Allocate a scene instance */
	Scene_t *Scene = (Scene_t*)malloc(sizeof(Scene_t));
	
	/* Setup */
	Scene->Id = Id;
	Scene->Windows = ListCreate(KeyInteger, LIST_NORMAL);
	memcpy(&Scene->Dimensions, Dimensions, sizeof(Rect_t));

	/* Load default background for this scene */
	Scene->Background = IMG_LoadTexture(Renderer, "Themes/Default/GfxBg.png");

	/* Allocate a texture */
	Scene->Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_TARGET, Dimensions->w, Dimensions->h);

	/* Set blend mode */
	SDL_SetTextureBlendMode(Scene->Texture, SDL_BLENDMODE_BLEND);

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
	ListAppend(Scene->Windows, ListCreateNode(Key, Window));
}

/* Update
 * This updates any changes to windows
 * for this scene */
void SceneUpdate(Scene_t *Scene)
{
	/* Sanity */
	if (Scene == NULL)
		return;
	
	/* Update windows */
	foreach(wNode, Scene->Windows)
	{
		/* Cast */
		Window_t *Window = (Window_t*)wNode->Data;

		/* Update window */
		WindowUpdate(Window);
	}
}

/* Render
 * This renders all windows for this scene */
void SceneRender(Scene_t *Scene, SDL_Renderer *Renderer)
{
	/* Sanity */
	if (Scene == NULL
		|| Renderer == NULL)
		return;

	/* Change render target to backbuffer */
	SDL_SetRenderTarget(Renderer, Scene->Texture);

	/* Start by rendering background */
	SDL_RenderCopy(Renderer, Scene->Background, NULL, NULL);

	/* Render windows */
	foreach(wNode, Scene->Windows)
	{
		/* Cast */
		Window_t *Window = (Window_t*)wNode->Data;

		/* Render window */
		WindowRender(Window, Renderer);
	}

	/* Change target back to screen */
	SDL_SetRenderTarget(Renderer, NULL);

	/* Render */
	SDL_RenderCopy(Renderer, Scene->Texture, NULL, NULL);
}