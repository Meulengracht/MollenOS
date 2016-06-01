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

	/* Precreate a test window */
	Rect_t WndSize;
	Window_t *Test = NULL;

	WndSize.x = 100;
	WndSize.y = 100;
	WndSize.w = 350;
	WndSize.h = 200;

	Test = WindowCreate(0, &WndSize, 0, Target);

	/* Pre-Render */
	SceneManagerAddWindow(Test);
	SceneManagerUpdate();
	SceneManagerRender(Target);

	/* Loop forever */
	while (!bQuit) 
	{
		/* Wait for message */
		MollenOSMessageWait(&Message);

		/* Handle Message */
		HandleMessage(Target, &Message);

		/* Update Scene */
		SceneManagerUpdate();

		/* Render Scene */
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

