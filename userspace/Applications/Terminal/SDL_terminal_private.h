/*
    SDL Terminal - Simple DirectMedia Layer Terminal
    Copyright (C) 2005 Nicolas P. Rougier

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Nicolas P. Rougier
    rougier@loria.fr
*/

#include <SDL.h>
#include <SDL_ttf.h>
#include "SDL_terminal.h"


/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif


int SDL_TerminalEventFilter(void *userdata, SDL_Event *event);
int SDL_TerminalEvent (SDL_Terminal *terminal, SDL_Event *event);

int SDL_TerminalNewline (SDL_Terminal *terminal);
int SDL_TerminalLineInsertChar (SDL_Terminal *terminal, char c);
int SDL_TerminalLineRemoveChar (SDL_Terminal *terminal, int pos);
int SDL_TerminalBufferAddText (SDL_Terminal *terminal, char *text, ...);
int SDL_TerminalHistoryPrev (SDL_Terminal *terminal);
int SDL_TerminalHistoryNext (SDL_Terminal *terminal);

int SDL_TerminalAddChar (SDL_Terminal *terminal, char c);
int SDL_TerminalAddText (SDL_Terminal *terminal, char *text, ...);
int SDL_TerminalClearFrom (SDL_Terminal *terminal, int cx, int cy);
int SDL_TerminalScroll (SDL_Terminal *terminal, int n);
int SDL_TerminalGetNumberOfLine (SDL_Terminal *terminal, char *text);

int SDL_TerminalEraseCursor (SDL_Terminal *terminal);
int SDL_TerminalRenderCursor (SDL_Terminal *terminal);
int SDL_TerminalRenderChar (SDL_Terminal *terminal, int x, int y, char c);
int SDL_TerminalEraseChar  (SDL_Terminal *terminal, int x, int y);
int SDL_TerminalUpdateGLTexture (SDL_Terminal *terminal, SDL_Rect *src_rect);


/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

