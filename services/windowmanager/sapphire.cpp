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
 * MollenOS - Window Manager Service
 */
#define __TRACE

/* Includes
 * - System */
#include <os/driver/window.h>
#include <os/mollenos.h>
#include <os/utils.h>

#include "core/scenemanager.h"
#include "core/backends/sdl/sdlrenderer.h"

/* Handle Message */
void HandleMessage(SDL_Renderer *Target, MEventMessage_t *Message)
{
	/* First of all, 
	 * which kind of message is it? 
	 * User-messages are generic */
	switch (Message->Base.Type)
	{
		/* Generic Message 
		 * Comes from processes */
		case EventGeneric:
		{
			/* Let's see.. */
			switch (Message->Generic.Type)
			{
				/* Create new window */
				case GenericWindowCreate:
				{
					/* Get window information structure */
					IPCWindowCreate_t *WndInformation = 
						(IPCWindowCreate_t*)Message->Generic.LoParam;
					Window_t *Wnd = NULL;
					void *BackbufferHandle = NULL;

					/* Create window */
					Wnd = WindowCreate(Message->Base.Sender, &WndInformation->Dimensions, 
						WndInformation->Flags, Target);

					/* Share backbuffer */
					BackbufferHandle = MollenOSMemoryShare(Message->Base.Sender, 
						Wnd->Backbuffer, Wnd->BackbufferSize);

					/* Update structure */
					Wnd->BackbufferHandle = BackbufferHandle;
					WndInformation->Backbuffer = BackbufferHandle;
					WndInformation->BackbufferSize = Wnd->BackbufferSize;

					/* Update dimensions */
					WndInformation->WndDimensions.x = Wnd->FullDimensions.x;
					WndInformation->WndDimensions.y = Wnd->FullDimensions.y;
					WndInformation->WndDimensions.w = Wnd->FullDimensions.w;
					WndInformation->WndDimensions.h = Wnd->FullDimensions.h;
					
					WndInformation->BbDimensions.x = Wnd->ContentDimensions.x;
					WndInformation->BbDimensions.y = Wnd->ContentDimensions.y;
					WndInformation->BbDimensions.w = Wnd->ContentDimensions.w;
					WndInformation->BbDimensions.h = Wnd->ContentDimensions.h;

					/* Add to scene manager */
					SceneManagerAddWindow(Wnd);

					/* Invalidate Rectangle */
					SceneManagerUpdate(&Wnd->FullDimensions);

					/* Update id */
					WndInformation->WindowId = Wnd->Id;

					/* Signal the process */
					MollenOSSignalWake(Message->Base.Sender);

				} break;

				/* Destroy a window */
				case GenericWindowDestroy:
				{

				} break;

				/* Invalidate a window */
				case GenericWindowInvalidate:
				{
					/* Since the coordinates for the dirty rect
					 * actually are relative to the window 
					 * we need to offset them */
					Window_t *Window = SceneManagerGetWindow((int)Message->Generic.LoParam);
					Rect_t AbsRect;

					/* Append global coords */
					AbsRect.x = Window->FullDimensions.x + Message->Generic.RcParam.x;
					AbsRect.y = Window->FullDimensions.y + Message->Generic.RcParam.y;
					AbsRect.w = Message->Generic.RcParam.w;
					AbsRect.h = Message->Generic.RcParam.h;

					/* Ok, so mark the rectangle dirty 
					 * and update screen */
					SceneManagerUpdate(&AbsRect);

				} break;

				/* Ignore other events */
				default:
					break;
			}

		} break;

		/* Input Message
		 * Comes from input drivers */
		case EventInput:
		{
			/* First of all, we want to intercept the input by 
			 * checking whether or not we should handle it 
			 * so start out by checking key-combinations, and
			 * check whether or not a window decoration has been hit */

			/* Ok, if not, we redirect it to active window */
			Window_t *Active = SceneManagerGetActiveWindow();

			/* Is there not any? */
			if (Active != NULL) {
				MollenOSMessageSend(Active->Owner, &Message, Message->Base.Length);
			}

		} break;

		/* Fuck rest */
		default:
			break;
	}
}

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t OnLoad(void)
{
	// Variables
	Rect_t ScreenSize;

	// Trace
	TRACE("WindowManager.OnLoad");

	// Query screen dimensions
	ScreenQueryGeometry(&ScreenSize);

	// Create renderer
	IRenderer *Renderer = new CSdlRenderer();
	if (!Renderer->Create(&ScreenSize)) {
		delete Renderer;
		ERROR("Failed to initialize the renderer");
		return OsError;
	}

	// Initialize the scenemanager
	if (!sSceneManager.Initialize(Renderer, &ScreenSize)) {
		ERROR("Failed to initialize the scene-manager");
		return OsError;
	}

	// Register us with server manager
	RegisterService(__WINDOWMANAGER_TARGET);

	// End boot
	//MollenOSEndBoot();
	
	// Perform initial update
	sSceneManager.Invalidate(NULL);
	sSceneManager.Update();

	// Done
	return OsSuccess;
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t OnUnload(void)
{
	// Cleanup engine resources
	delete &sSceneManager;
	return OsSuccess;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t OnEvent(MRemoteCall_t *Message)
{
	// Variables
	OsStatus_t Result = OsSuccess;

	// Which function is called?
	switch (Message->Function)
	{
		case __WINDOWMANAGER_CREATE: {

		} break;
		case __WINDOWMANAGER_DESTROY: {

		} break;
		case __WINDOWMANAGER_INVALIDATE: {

		} break;
		case __WINDOWMANAGER_QUERY: {

		} break;
		case __WINDOWMANAGER_NEWINPUT: {

		} break;
	}

	// Done
	return Result;
}
