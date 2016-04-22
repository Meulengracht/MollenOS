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
#include <SDL.h>

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
	MainWnd = SDL_CreateWindow("Hello World!", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
	if (MainWnd == NULL) {
		MollenOSSystemLog(SDL_GetError());
		SDL_Quit();
		return -1;
	}

	/* Create renderer */
	MainRenderer = SDL_CreateRenderer(MainWnd, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	if (MainRenderer == NULL) {
		MollenOSSystemLog(SDL_GetError());
		SDL_DestroyWindow(MainWnd);
		SDL_Quit();
		return -1;
	}

	/* Cleanup */
	SDL_DestroyRenderer(MainRenderer);
	SDL_DestroyWindow(MainWnd);
	SDL_Quit();

	/* Done! */
	return 0;
}

