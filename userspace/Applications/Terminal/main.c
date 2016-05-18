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
#include <SDL.h>
#include "SDL_terminal.h"

/* Entry Point */
int main(int argc, char* argv[])
{
	/* Variables */
	SDL_Window *MainWnd;
	SDL_Renderer *MainRenderer;
	Uint32 last_tick = 0;
	int fps = 100;

	/* Init SDL (Main) */
	SDL_SetMainReady();

	/* Init SDL (Video, Events) */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		return -1;
	}

	/* Create a window */
	MainWnd = SDL_CreateWindow("Sapphire", 100, 100, 800, 600, SDL_WINDOW_SHOWN);
	if (MainWnd == NULL) {
		SDL_Quit();
		return -2;
	}

	/* Create renderer */
	MainRenderer = SDL_CreateRenderer(MainWnd, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	if (MainRenderer == NULL) {
		SDL_DestroyWindow(MainWnd);
		SDL_Quit();
		return -3;
	}

	/* Create terminal */
	SDL_Terminal *terminal = SDL_CreateTerminal(MainWnd);

	SDL_TerminalSetFont(terminal, "Fonts/DejaVuSansMono.ttf", 12);
	SDL_TerminalSetSize(terminal, 80, 25);
	SDL_TerminalSetPosition(terminal, 0, 0);

	SDL_TerminalSetColor(terminal, 255, 255, 255, 128);
	SDL_TerminalSetBorderColor(terminal, 255, 255, 255, 255);
	SDL_TerminalSetForeground(terminal, 0, 0, 0, 255);
	SDL_TerminalSetBackground(terminal, 0, 0, 0, 0); /* No background since alpha=0 */

	SDL_TerminalClear(terminal);
	SDL_TerminalPrint(terminal, "Terminal initialized\n");
	SDL_TerminalPrint(terminal, "Using font DejaVuSansMono, %d\n", terminal->font_size);
	SDL_TerminalPrint(terminal, "Terminal geometry: %d x %d\n\n", terminal->size.column, terminal->size.row);
	SDL_TerminalPrint(terminal, "\033[1mBold on\033[22m - Bold off\n");
	SDL_TerminalPrint(terminal, "\033[3mItalic on\033[23m - Italic off\n");
	SDL_TerminalPrint(terminal, "\033[4mUnderline on\033[24m - Underline off\n");

	int done = 0;
	SDL_Event event;
	last_tick = SDL_GetTicks();

	while (!done) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_KEYDOWN:
				//key_press(&event.key.keysym);
				break;
			case SDL_QUIT:
				done = 1;
				break;
			case SDL_TERMINALEVENT:
				//printf("Terminal event: %s\n", (char *)event.user.data2);
				break;
			default:
				break;
			}
		}

		Uint32 wait = (Uint32)(1000.0f / fps);
		Uint32 new_tick = SDL_GetTicks();
		if ((new_tick - last_tick) < wait)
			SDL_Delay(wait - (new_tick - last_tick));
		last_tick = SDL_GetTicks();

		SDL_TerminalBlit(terminal);
	}

	/* Cleanup */
	SDL_DestroyRenderer(MainRenderer);
	SDL_DestroyWindow(MainWnd);
	SDL_Quit();

	/* Done! */
	return 0;
}

