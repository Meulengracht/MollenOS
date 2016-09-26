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
#include "SceneManager.h"
#include "Scene.h"

/* CLib */
#include <stdlib.h>

/* Globals */
SceneManager_t *GlbSceneManager = NULL;

/* Initializor 
 * Sets up the scene manager 
 * and creates the default scene */
void SceneManagerInit(SDL_Renderer *Renderer, Rect_t *ScreenDims)
{
	/* Vars */
	Scene_t *Scene = NULL;

	/* Allocate scene manager instance */
	GlbSceneManager = (SceneManager_t*)malloc(sizeof(SceneManager_t));
	GlbSceneManager->Scenes = ListCreate(KeyInteger, LIST_NORMAL);
	GlbSceneManager->IdGen = 0;
	GlbSceneManager->IdWindowGen = 0;

	/* Create initial scene */
	Scene = SceneCreate(GlbSceneManager->IdGen, ScreenDims, Renderer);

	/* How to keep track of active? */

	/* Append to list */
	DataKey_t Key;
	Key.Value = GlbSceneManager->IdGen;
	ListAppend(GlbSceneManager->Scenes, ListCreateNode(Key, Key, Scene));

	/* Increase Id */
	GlbSceneManager->IdGen++;
}

/* Destructor 
 * Cleans up all scenes 
 * and releases resources allocated */
void SceneManagerDestruct(void)
{
	/* Sanity */
	if (GlbSceneManager == NULL)
		return;

	/* Iterate through all nodes 
	 * and destroy scenes */
	foreach(sNode, GlbSceneManager->Scenes)
	{
		/* Cast */
		Scene_t *Scene = (Scene_t*)sNode->Data;

		/* Destroy */
		SceneDestroy(Scene);
	}

	/* Free */
	ListDestroy(GlbSceneManager->Scenes);
	free(GlbSceneManager);
}

/* Add Window
 * Adds a newly created window to the
 * current scene. The window is not immediately
 * rendered before a call to Render */
void SceneManagerAddWindow(Window_t *Window)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (GlbSceneManager == NULL)
		return;

	/* Get active scene (todo) */
	Key.Value = 0;
	Scene_t *ActiveScene = (Scene_t*)ListGetDataByKey(GlbSceneManager->Scenes, Key, 0);

	/* Assign a unique id */
	Window->Id = GlbSceneManager->IdWindowGen;
	GlbSceneManager->IdWindowGen++;

	/* Update Scene */
	SceneAddWindow(ActiveScene, Window);
}

/* Get Window 
 * This looks up a window by id in the current
 * active scene, if not found, NULL is returned */
Window_t *SceneManagerGetWindow(int WindowId)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (GlbSceneManager == NULL)
		return NULL;

	/* Get active scene (todo) */
	Key.Value = 0;
	Scene_t *ActiveScene = (Scene_t*)ListGetDataByKey(GlbSceneManager->Scenes, Key, 0);

	/* Now just return for the active scene */
	return SceneGetWindow(ActiveScene, WindowId);
}

/* Update 
 * This updates the current scene 
 * and makes all neccessary changes to windows 
 * a call with NULL updates entire scene */
void SceneManagerUpdate(Rect_t *DirtyArea)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (GlbSceneManager == NULL)
		return;

	/* Get active scene (todo) */
	Key.Value = 0;
	Scene_t *ActiveScene = (Scene_t*)ListGetDataByKey(GlbSceneManager->Scenes, Key, 0);

	/* Update Scene */
	SceneUpdate(ActiveScene, DirtyArea);
}

/* Render
 * This renders the current scene
 * to the screen */
void SceneManagerRender(SDL_Renderer *Renderer)
{
	/* Vars */
	DataKey_t Key;

	/* Sanity */
	if (GlbSceneManager == NULL)
		return;

	/* Get active scene (todo) */
	Key.Value = 0;
	Scene_t *ActiveScene = (Scene_t*)ListGetDataByKey(GlbSceneManager->Scenes, Key, 0);

	/* Update Scene */
	SceneRender(ActiveScene, Renderer);

	/* Present Scene */
	SDL_RenderPresent(Renderer);
}