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
 * MollenOS Sapphire - Scene Manager
 * - Contains the scene-manager implementation that acts as a 
 *   virtual desktop manager
 */

#ifndef _SAPPHIRE_SCENEMGR_H_
#define _SAPPHIRE_SCENEMGR_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <ds/list.h>

/* Includes
 * - Ui */
#include "backends/irenderer.h"
#include "scene.h"
#include "window.h"

/* CSceneManager
 * Acts a virtual desktop manager, managing a bunch of desktops
 * which allows the user to quickly switch between desktops */
class CSceneManager
{
public:
	static CSceneManager& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CSceneManager _Instance;
		return _Instance;
	}
private:
	CSceneManager() {}                    // Constructor? (the {} brackets) are needed here.
	CSceneManager(CSceneManager const&);  // Don't Implement
	void operator=(CSceneManager const&); // Don't implement

public:
	CSceneManager(CSceneManager const&) = delete;
	void operator=(CSceneManager const&) = delete;

	// Initialize
	// Initializes the scene manager and creates the first scene
	bool Initialize(IRenderer *Renderer, Rect_t *Size);

	// CreateScene
	// Creates a new scene and returns a pointer to the newly
	// created scene
	

	// Invalidate
	// This updates the current scene and makes all neccessary 
	// changes to windows a call with NULL invalidates entire scene
	bool Invalidate(Rect_t *Area);

	// Update
	// Flushes all the changes since last call to the renderer
	bool Update();

private:
	int				m_iSceneId;
	int				m_iWindowId;
	List_t*			m_pScenes;
};

// Shorthand for the scenemanager
#define sSceneManager CSceneManager::GetInstance()

/* SceneManagerInitialize
 * Sets up the scene manager and creates the default scene */
__EXTERN
void
SceneManagerInitialize(
	_In_ SDL_Renderer *Renderer, 
	_In_ Rect_t *ScreenSize);

/* SceneManagerDestroy
 * Cleans up all scenes and releases resources allocated */
__EXTERN
void
SceneManagerDestroy(void);

/* SceneManagerAddWindow
 * Adds a newly created window to the current scene. 
 * The window is not immediately rendered before a call to Render */
__EXTERN
void
SceneManagerAddWindow(
	_In_ Window_t *Window);

/* SceneManagerGetWindow
 * This looks up a window by id in the current
 * active scene, if not found, NULL is returned */
__EXTERN
Window_t*
SceneManagerGetWindow(
	_In_ int WindowId);

/* SceneManagerGetActiveWindow
 * This looks up the active window by in the current
 * active scene, if not found, NULL is returned */
__EXTERN
Window_t*
SceneManagerGetActiveWindow(void);

/* SceneManagerUpdate 
 * This updates the current scene and makes all neccessary 
 * changes to windows a call with NULL updates entire scene */
__EXTERN
void
SceneManagerUpdate(
	_In_ Rect_t *DirtyArea);

/* SceneManagerRender
 * This renders the current scene to the screen */
__EXTERN
void
SceneManagerRender(
	_In_ SDL_Renderer *Renderer);

#endif //!_SAPPHIRE_SCENEMGR_H_