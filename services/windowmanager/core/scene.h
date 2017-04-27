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
 * MollenOS Sapphire - Scene
 * - Contains the scene implementation that acts as a virtual desktop
 */

#ifndef _SAPPHIRE_SCENE_H_
#define _SAPPHIRE_SCENE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <ds/list.h>

/* Includes
 * - Ui */
#include "backends/irenderer.h"
#include "window.h"

/* CScene
 * Acts a virtual desktop, managing a bunch of windows */
class CScene
{
public:
	CScene();
	~CScene();


private:

};

/* Structures */
typedef struct _sScene {

	/* Scene Id */
	int Id;

	/* Scene dimensions */
	Rect_t Dimensions;

	/* Background */
	SDL_Texture *Background;

	/* Backbuffer */
	SDL_Texture *Texture;

	/* List of windows */
	List_t *Windows;

	/* Array of dirty rectangles
	 * up to a max 16 per iteration */
	Rect_t Dirty[16];

	/* The index for how many of the 
	 * above rectangles are valid */
	int ValidRectangles;

} Scene_t;


/* Prototypes */

/* Initializor
 * Creates a new scene */
__CRT_EXTERN Scene_t *SceneCreate(int Id, Rect_t *Dimensions, SDL_Renderer *Renderer);

/* Destructor
 * Cleans up all windows
 * and releases resources allocated */
__CRT_EXTERN void SceneDestroy(Scene_t *Scene);

/* Add Window
 * Adds a newly created window to the
 * given scene. The window is not immediately
 * rendered before a call to Render */
__CRT_EXTERN void SceneAddWindow(Scene_t *Scene, Window_t *Window);

/* Get Window 
 * Looks up a window by id in the given scene
 * returns NULL if none is found */
__CRT_EXTERN Window_t *SceneGetWindow(Scene_t *Scene, int WindowId);

/* Get Active Window
 * Looks up the active window in the given scene
 * returns NULL if none is found */
__CRT_EXTERN Window_t *SceneGetActiveWindow(Scene_t *Scene);

/* Update
 * This updates any changes to windows
 * for this scene, but only for the given rectangle */
__CRT_EXTERN void SceneUpdate(Scene_t *Scene, Rect_t *DirtyArea);

/* Render
 * This renders all windows for this scene */
__CRT_EXTERN void SceneRender(Scene_t *Scene, SDL_Renderer *Renderer);

#endif //!_SAPPHIRE_SCENE_H_