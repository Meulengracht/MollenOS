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
* MollenOS Window Manager
*/

/* Includes */
#include <os/MollenOS.h>

/* UI Includes */
#include <SDL.h>
#include <SDL_image.h>

/* Sapphire System */
#include "Core/SceneManager.h"
#include "Core/WindowIpc.h"

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
					WndInformation->ResultDimensions.x = Wnd->Dimensions.x;
					WndInformation->ResultDimensions.y = Wnd->Dimensions.y;
					WndInformation->ResultDimensions.w = Wnd->Dimensions.w;
					WndInformation->ResultDimensions.h = Wnd->Dimensions.h;

					/* Add to scene manager */
					SceneManagerAddWindow(Wnd);

					/* Invalidate Rectangle */
					SceneManagerUpdate(&WndInformation->Dimensions);

					MollenOSSystemLog("Rendering window of size %x,%x",
						WndInformation->Dimensions.w, WndInformation->Dimensions.y);

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
					AbsRect.x = Window->Dimensions.x + Message->Generic.RcParam.x;
					AbsRect.y = Window->Dimensions.y + Message->Generic.RcParam.y;
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

/* Sdl Event Loop */
void EventLoop(SDL_Renderer *Target)
{
	/* Vars */
	MEventMessage_t Message;
	int bQuit = 0;

	/* Pre-Render */
	SceneManagerUpdate(NULL);
	SceneManagerRender(Target);

	/* Start terminal */
	ProcessSpawn("%Sys%/Terminal.mxi", NULL);

	/* Loop forever */
	while (!bQuit) 
	{
		/* Wait for message */
		MollenOSMessageWait(&Message);

		/* Handle Message */
		HandleMessage(Target, &Message);

		/* Render updates */
		SceneManagerRender(Target);
	}
}

/* Entry Point */
int main(int argc, char* argv[])
{
	/* Variables */
	SDL_Window *MainWnd;
	SDL_Renderer *MainRenderer;
	Rect_t ScreenDims;

	/* Init SDL (Main) */
	SDL_SetMainReady();

	/* Init SDL (Video, Events) */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		MollenOSSystemLog(SDL_GetError());
		return -1;
	}

	/* Get screen dimensions */
	MollenOSGetScreenGeometry(&ScreenDims);

	/* Create a window */
	MainWnd = SDL_CreateWindow("Sapphire", 0, 0, ScreenDims.w, ScreenDims.h, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
	if (MainWnd == NULL) {
		MollenOSSystemLog(SDL_GetError());
		SDL_Quit();
		return -2;
	}

	/* Create renderer */
	MainRenderer = SDL_CreateRenderer(MainWnd, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	if (MainRenderer == NULL) {
		MollenOSSystemLog(SDL_GetError());
		SDL_DestroyWindow(MainWnd);
		SDL_Quit();
		return -3;
	}

	/* Initialize SDL Image */
	if (!IMG_Init(IMG_INIT_PNG)) {
		MollenOSSystemLog("Failed to initialize SdlImage");
		SDL_DestroyRenderer(MainRenderer);
		SDL_DestroyWindow(MainWnd);
		SDL_Quit();
		return -4;
	}

	/* End Boot */
	//MollenOSEndBoot();
	//MollenOSRegisterWM();

	/* Initialize Sapphire */
	SceneManagerInit(MainRenderer, &ScreenDims);

	/* Event Loop */
	EventLoop(MainRenderer);

	/* Destroy Sapphire */
	SceneManagerDestruct();

	/* Cleanup */
	SDL_DestroyRenderer(MainRenderer);
	SDL_DestroyWindow(MainWnd);
	SDL_Quit();

	/* Done! */
	return 0;
}

