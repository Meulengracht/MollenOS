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

/* Sdl Event Loop */
void EventLoop(SDL_Renderer *Target)
{
	/* Vars */
	SDL_Event sEvent;
	int bQuit = 0;

	/* Load Background */
	SDL_Surface *BgImage = IMG_LoadTexture(Target, "Themes/Default/GfxBg.png");

	/* Sanity */
	if (BgImage == NULL) {
		MollenOSSystemLog(SDL_GetError());
	}

	/* Re-render screen */
	SDL_RenderCopy(Target, BgImage, NULL, NULL);

	/* Update */
	SDL_RenderPresent(Target);

	/* Loop waiting for ESC+Mouse_Button */
	while (!bQuit) 
	{
		/* Wait for event */
		SDL_WaitEvent(&sEvent);

		/* Only handle quit events */
		switch (sEvent.type) {
			/* Exit Event */
			case SDL_QUIT: 
				break;
		}

		/* Re-render screen */
		//SDL_RenderCopy(Target, BgTexture, NULL, NULL);

		/* Render Windows */

		/* Update */
		//SDL_RenderPresent(Target);
	}
}

/* Entry Point */
int main(int argc, char* argv[])
{
	/* Variables */
	SDL_Window *MainWnd;
	SDL_Renderer *MainRenderer;

	/* Init SDL (Main) */
	SDL_SetMainReady();

	/* Init SDL (Full) */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		MollenOSSystemLog(SDL_GetError());
		return -1;
	}

	/* Create a window */
	MainWnd = SDL_CreateWindow("Sapphire", 0, 0, 640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
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

	/* Event Loop */
	EventLoop(MainRenderer);

	/* Cleanup */
	SDL_DestroyRenderer(MainRenderer);
	SDL_DestroyWindow(MainWnd);
	SDL_Quit();

	/* Done! */
	return 0;
}

