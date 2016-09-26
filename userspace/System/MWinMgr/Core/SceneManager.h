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

#ifndef _SAPPHIRE_SCENEMGR_H_
#define _SAPPHIRE_SCENEMGR_H_

/* Includes */
#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>

/* Ui Includes */
#include <SDL.h>

/* List -> Scenes */
#include <os/MollenOS.h>
#include <ds/list.h>
#include "Window.h"

/* Definitions */


/* Structures */
typedef struct _sSceneManager {

	/* Id Generator */
	int IdGen;
	int IdWindowGen;

	/* List of scenes */
	List_t *Scenes;

} SceneManager_t;


/* Prototypes */

/* Initializor
 * Sets up the scene manager
 * and creates the default scene */
EXTERN void SceneManagerInit(SDL_Renderer *Renderer, Rect_t *ScreenDims);

/* Destructor
 * Cleans up all scenes
 * and releases resources allocated */
EXTERN void SceneManagerDestruct(void);

/* Add Window 
 * Adds a newly created window to the 
 * current scene. The window is not immediately 
 * rendered before a call to Render */
EXTERN void SceneManagerAddWindow(Window_t *Window);

/* Get Window 
 * This looks up a window by id in the current
 * active scene, if not found, NULL is returned */
EXTERN Window_t *SceneManagerGetWindow(int WindowId);

/* Update 
 * This updates the current scene 
 * and makes all neccessary changes to windows 
 * a call with NULL updates entire scene */
EXTERN void SceneManagerUpdate(Rect_t *DirtyArea);

/* Render
 * This renders the current scene 
 * to the screen */
EXTERN void SceneManagerRender(SDL_Renderer *Renderer);

#endif //!_SAPPHIRE_SCENEMGR_H_