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


/*
  Readline emulation (quite limited):
  -----------------------------------

  Bare Essentials
  ---------------
   left:
     Move back one character. 

   right:
     Move forward one character. 

   backspace: 
     Delete the character to the left of the cursor. 

   printing characters:
     Insert the character into the line at the cursor. 


  Movement Commands
  -----------------
   C-a:
     Move to the start of the line. 
   C-e:
     Move to the end of the line. 
   C-l:
     Clear the screen, reprinting the current line at the top.


  Killing Commands
  ----------------
   C-k:
     Kill the text from the current cursor position to the end of the line.
   C-y:
     Yank the most recently killed text back into the buffer at the cursor.
*/

 
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <SDL.h>
#include "TerminalPrivate.h"

#undef HAVE_OPENGL

/* 
 * This is the active terminal, the one that process SDL events.
 */
static SDL_Terminal *SDL_ActiveTerminal = 0;


/*
 * Create a new SDL terminal with default settings and modify
 * some SDL settings:
 *   - SDL_UNICODE is enabled
 *   - SDL_KeyRepeat is enabled
 *   - A filter is added to SDL events
 *
 * @return	If the creation is successful, it returns a new terminal, otherwise it returns 0.
 */
SDL_Terminal *
SDL_CreateTerminal(Window_t *window)
{
    int i;

    SDL_Terminal *terminal = (SDL_Terminal *) malloc (sizeof (SDL_Terminal));
    if (terminal == NULL) {
        SDL_SetError ("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return 0;
    }

    /* Initialization */
	terminal->window = window;
    terminal->status = 0;
    terminal->visible = 1;
    terminal->active = 0;

    /* Position (in pixels) */
    terminal->position.x = 0;
    terminal->position.y = 0;

    /* Terminal size */
    terminal->size.row    = 80;
    terminal->size.column = 24;

    /* Cursor position */
    terminal->cpos.row    = 0;
    terminal->cpos.column = 0;
    
    /* Font information */
    terminal->font = 0;
    terminal->font_filename = 0;
    terminal->font_size = 0;
    terminal->glyph_size.w = 0;
    terminal->glyph_size.h = 0;
    
    /* Defaults color settings */
    SDL_TerminalSetColor (terminal, 255,255,255,180);
    SDL_TerminalSetBorderColor (terminal, 255,255,255,255);
    SDL_TerminalSetDefaultForeground (terminal, 0,0,0,255);
    SDL_TerminalSetDefaultBackground (terminal, 0,0,0,0);
    SDL_TerminalSetForeground (terminal, 0,0,0,255);
    SDL_TerminalSetBackground (terminal, 0,0,0,0);
    
    terminal->br_size = 4;
    terminal->tabsize = 4;
    
    /* Edited line allocation */
    terminal->line = (char *) malloc (SDL_TERMINAL_MAX_LINE_SIZE*sizeof(char));
    if (terminal->line == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return 0;
    }
    terminal->line[0] = '\0';
    terminal->line_start.x = 0;
    terminal->line_start.y = 0;
    terminal->line_pos = 0;

    /* History allocation */
    terminal->history_size = 25;
    terminal->history_current = terminal->history_size-1;
    terminal->history = (char **) (malloc (terminal->history_size*sizeof(char *)));
    if (terminal->history == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return 0;
    }
    for (i=0; i<terminal->history_size; i++)
        terminal->history[i] = 0;


    /* Text buffer Allocation */
    terminal->buffer_size = 250;
    terminal->buffer = (char **) (malloc (terminal->buffer_size*sizeof(char *)));
    if (terminal->buffer == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return 0;
    }
    for (i=0; i<terminal->buffer_size; i++)
        terminal->buffer[i] = 0;

    terminal->surface = 0;
    terminal->texture = 0;
    terminal->texture_size.w = 0;
    terminal->texture_size.h = 0;
#ifdef HAVE_OPENGL
    SDL_Surface *screen = SDL_GetVideoSurface();
    if (screen->flags & SDL_OPENGL) {
        terminal->texture_size.w = 1024;
        terminal->texture_size.h = 1024;    
        glGenTextures (1, &terminal->texture);
        glBindTexture (GL_TEXTURE_2D, terminal->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        unsigned char *data = (unsigned char *) calloc (terminal->texture_size.w*terminal->texture_size.h, 4);
        if (data == NULL) {
            SDL_SetError (SDL_ENOMEM);
            return 0;
        }
        glTexImage2D (GL_TEXTURE_2D, 0, 4, terminal->texture_size.w, terminal->texture_size.h,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        free (data);
    }    
#endif
    terminal->event.user.data1 = terminal;
    terminal->event.user.data2 = 0;

    /* Modify some SDL settings */
    SDL_ActiveTerminal = terminal;
	SDL_GetEventFilter(&terminal->event_filter, &terminal->event_data);
    SDL_SetEventFilter(SDL_TerminalEventFilter, NULL);
    return terminal;
}


/*
 * Destroy a SDL terminal and free the allocated memory
 */
void
SDL_DestroyTerminal (SDL_Terminal *terminal)
{
    int i;
    
    terminal->status = 0;
    if (terminal->line)
        free (terminal->line);
    for (i=0; i<terminal->history_size; i++)
        if (terminal->history[i])
            free (terminal->history[i]);
    free (terminal->history);
    for (i=0; i<terminal->buffer_size; i++)
        if (terminal->buffer[i])
            free (terminal->buffer[i]);
    free (terminal->buffer);
    if (terminal->font)
        TTF_CloseFont (terminal->font);
    if (terminal->surface)
        SDL_FreeSurface (terminal->surface);

#ifdef HAVE_OPENGL
    if (terminal->texture)
        glDeleteTextures (1, &terminal->texture);
#endif
}


/*
 * Event filter
 */
int SDL_TerminalEventFilter (void *userdata, SDL_Event *event)
{
    int status = 1;
    if (SDL_ActiveTerminal) {
        status = SDL_TerminalEvent (SDL_ActiveTerminal, event);
        if ((SDL_ActiveTerminal->event_filter != NULL) && (!status))
            return (SDL_ActiveTerminal->event_filter)(SDL_ActiveTerminal->event_data, event);
    }
    return status;
}


/*
 * Force the processing of an event
 */
int
SDL_TerminalProcessEvent (SDL_Terminal *terminal, SDL_Event *event)
{
    int active = terminal->active;
    int visible = terminal->visible;
    terminal->active  = 1;
    terminal->visible = 1;
    int ret = SDL_TerminalEvent (terminal, event);
    terminal->active  = active;
    terminal->visible = visible;
    return ret;
}


/*
 *
 */
int
SDL_TerminalEvent (SDL_Terminal *terminal, SDL_Event *event)
{
    if (!terminal->visible)
        return 1;

    switch(event->type) {
    case SDL_KEYDOWN:
        if (!terminal->active)
            return 1;
        
        else if (event->key.keysym.sym == SDLK_BACKSPACE) {
            if ((terminal->line_pos > 0) && (terminal->line[terminal->line_pos-1] == '\t')) {
                if (SDL_TerminalLineRemoveChar(terminal, terminal->line_pos-1) == 0) {
                    SDL_TerminalEraseCursor (terminal);
                    terminal->cpos.x -= terminal->tabsize;
                    if (terminal->cpos.x < 0) {
                        terminal->cpos.x += terminal->size.column;
                        terminal->cpos.y--;
                    }

                    /* If we were in the middle of the line, we have to render the rest of the line */
                    if (terminal->line_pos < (int)strlen(terminal->line)) {
                        int i;
                        Vec2i saved_cpos = terminal->cpos;
						for (i = terminal->line_pos; i<(int)strlen(terminal->line); i++)
                            SDL_TerminalAddChar (terminal, terminal->line[i]);
                        for (i=0; i<terminal->tabsize; i++)
                            SDL_TerminalAddChar (terminal, ' ');
                        terminal->cpos = saved_cpos;
                    }
                    SDL_TerminalRenderCursor (terminal);
                }
                return 0;
            }
            else if (SDL_TerminalLineRemoveChar(terminal, terminal->line_pos-1) == 0) {
                SDL_TerminalEraseCursor (terminal);
                if (terminal->cpos.x > 0)
                    terminal->cpos.x--;
                else {
                    terminal->cpos.x = terminal->size.column-1;
                    terminal->cpos.y--;
                }
                /* If we were in the middle of the line, we have to render the ret of the line */
				if (terminal->line_pos < (int)strlen(terminal->line)) {
                    int i;
                    Vec2i saved_cpos = terminal->cpos;
					for (i = terminal->line_pos; i<(int)strlen(terminal->line); i++)
                        SDL_TerminalAddChar (terminal, terminal->line[i]);
                    SDL_TerminalAddChar (terminal, ' ');
                    terminal->cpos = saved_cpos;
                }
                SDL_TerminalRenderCursor (terminal);
                return 0;
            }
        } else if ((event->key.keysym.sym == SDLK_RETURN) || (event->key.keysym.sym == SDLK_KP_ENTER)) {
            SDL_TerminalEraseCursor (terminal);
            SDL_TerminalNewline (terminal);
            return 0;

        } else if (event->key.keysym.sym == SDLK_UP) {
            SDL_TerminalHistoryPrev (terminal);

        } else if (event->key.keysym.sym == SDLK_DOWN) {
            SDL_TerminalHistoryNext (terminal);

        } else if (event->key.keysym.sym == SDLK_LEFT) {
            if (terminal->line_pos > 0) {
                SDL_TerminalEraseCursor (terminal);
                terminal->line_pos--;
                int dx = 1;
                if (terminal->line[terminal->line_pos] == '\t')
                    dx = terminal->tabsize;
                terminal->cpos.x -= dx;
                if (terminal->cpos.x < 0) {
                    terminal->cpos.x += terminal->size.column;
                    terminal->cpos.y--;
                }
                SDL_TerminalRenderCursor (terminal);
            }
            return 0;

        } else if (event->key.keysym.sym == SDLK_RIGHT) {
			if (terminal->line_pos < (int)strlen(terminal->line)) {
                SDL_TerminalEraseCursor (terminal);
                int dx = +1;
				if ((terminal->line_pos < (int)strlen(terminal->line)) && terminal->line[terminal->line_pos] == '\t')
                    dx = terminal->tabsize;
                terminal->cpos.x += dx;
                if (terminal->cpos.x >= terminal->size.column) {
                    terminal->cpos.x -= terminal->size.column;
                    terminal->cpos.y++;
                }
                terminal->line_pos++;
                SDL_TerminalRenderCursor (terminal);
            }
            return 0;

        } else if (event->key.keysym.mod & KMOD_CTRL) {
            if (event->key.keysym.sym == SDLK_a) {
                /* go to start of line */
                SDL_TerminalEraseCursor (terminal);
                terminal->line_pos = 0;
                terminal->cpos = terminal->line_start;
                SDL_TerminalRenderCursor (terminal);
                return 0;
            }
            else if (event->key.keysym.sym == SDLK_e) {
                /* go to end of line */
                SDL_TerminalEraseCursor (terminal);
				while (terminal->line_pos < (int)strlen(terminal->line)) {
                    int i, dx = 1;
                    if (terminal->line[terminal->line_pos] == '\t')
                        dx = terminal->tabsize;
                    for (i=0; i<dx; i++) {
                        terminal->cpos.x++;
                        if (terminal->cpos.x > (terminal->size.column-1)) {
                            terminal->cpos.x = 0;
                            terminal->cpos.y++;
                        }
                    }
                    terminal->line_pos++;
                }
                SDL_TerminalRenderCursor (terminal);
                return 0;
            }
            else if (event->key.keysym.sym == SDLK_k) {
                int i, j;
                Vec2i saved_cpos = terminal->cpos;
				for (i = terminal->line_pos; i<(int)strlen(terminal->line); i++)
                    {
                        if (terminal->line[i] != '\t')
                            SDL_TerminalAddChar (terminal, ' ');
                        else
                            for (j=0; j<terminal->tabsize; j++)
                                SDL_TerminalAddChar (terminal, ' ');
                    }
                terminal->cpos = saved_cpos;
				if (terminal->line_pos < (int)strlen(terminal->line))
                    terminal->line[terminal->line_pos] = '\0';
                return 0;
            }            
            return 1;
        } else if (event->key.keysym.sym == SDLK_ESCAPE) { /* || (event->key.keysym.sym == SDLK_TAB)) { */
            return 1;

        } 
        break;

    case SDL_MOUSEMOTION:
        if ((event->motion.x > terminal->position.x) &&
            (event->motion.y > terminal->position.y) &&
            (event->motion.x < (terminal->position.x+terminal->psize.w)) &&
            (event->motion.y < (terminal->position.y+terminal->psize.h))) {
            terminal->active = 1;
        } else {
            terminal->active = 0;
        }
        
        break;
    }

    return 1;
}



/*
 * Blit the terminal onto the current video display surface
 * 
 */
int
SDL_TerminalBlit (SDL_Terminal *terminal)
{
	/* Variables */
	void *mPixels = NULL;
	int mPitch = 0;

	/* Render Cursor */
    SDL_TerminalRenderCursor (terminal);

    if (!(terminal->visible))
        return -1;

#ifdef HAVE_OPENGL
    if (screen->flags & SDL_OPENGL) {
        /* Enter "2D mode" */
        glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_POLYGON_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable (GL_LIGHTING);
        glDisable (GL_LINE_SMOOTH);
        glDisable (GL_CULL_FACE);
        glEnable (GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, screen->w, screen->h);
        glMatrixMode (GL_PROJECTION);
        glPushMatrix ();
        glLoadIdentity ();
        glOrtho (0, (GLdouble)screen->w, 0, (GLdouble)screen->h, -1, 1);
        glMatrixMode (GL_MODELVIEW);
        glPushMatrix ();
        glLoadIdentity ();

        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture (GL_TEXTURE_2D, terminal->texture);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        glPushMatrix ();
        glTranslated (terminal->position.x, screen->h-terminal->position.y, 0);
        glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
        glBegin (GL_QUADS);
        {
            glTexCoord2f (0, 0);
            glVertex2i (0, 0);
            glTexCoord2f (terminal->psize.w/(float)(terminal->texture_size.w), 0);
            glVertex2i (terminal->psize.w, 0);
            glTexCoord2f (terminal->psize.w/(float)(terminal->texture_size.w), terminal->psize.h/(float)(terminal->texture_size.h));
            glVertex2i (terminal->psize.w, -terminal->psize.h);
            glTexCoord2f (0, terminal->psize.h/(float)(terminal->texture_size.h));
            glVertex2i (0, -terminal->psize.h);
        }
        glEnd ();
        glPopMatrix ();	

        /* Leave "2D mode" */
        glMatrixMode (GL_PROJECTION);
        glPopMatrix ();
        glMatrixMode (GL_MODELVIEW);
        glPopMatrix();
        glPopAttrib();
	}

	return 0;
#else

	/* Get texture information */
	SDL_LockTexture(terminal->window->Texture, NULL, &mPixels, &mPitch);

	/* Copy pixels */
	memcpy(mPixels, terminal->surface->pixels, terminal->psize.h * mPitch);

	/* Unlock texture */
	SDL_UnlockTexture(terminal->window->Texture);

	return 0;
#endif
}


/*
 * Clear terminal
 *
 * @param	terminal		SDL Terminal to clear
 *
 * @return					If clearing was successful, it returns 0, otherwise it returns -1
 */
int
SDL_TerminalClear (SDL_Terminal *terminal)
{
    if (!terminal->surface)
        return -1;

    SDL_PixelFormat *fmt = terminal->surface->format;
    Uint32 color;

    /* Outer border */
    color = SDL_MapRGBA (fmt, terminal->br_color.r,terminal->br_color.g, terminal->br_color.b, terminal->br_color.a);
    SDL_Rect outer = {0, 0, terminal->psize.w, terminal->psize.h};
	SDL_SetSurfaceAlphaMod(terminal->surface, 0);
    SDL_FillRect (terminal->surface, &outer, color);

    /* Inner background */
    color = SDL_MapRGBA (fmt, terminal->color.r, terminal->color.g, terminal->color.b, terminal->color.a);
    SDL_Rect inner = {1, 1, terminal->psize.w-2, terminal->psize.h-2};
    SDL_FillRect (terminal->surface, &inner, color);

#ifdef HAVE_OPENGL
    if (terminal->texture) {
        glBindTexture (GL_TEXTURE_2D, terminal->texture);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, terminal->psize.w, terminal->psize.h,
                         GL_RGBA, GL_UNSIGNED_BYTE, terminal->surface->pixels);
    }
#endif

    /* Reset current edited line settings */
    terminal->cpos.x = 0;
    terminal->cpos.y = 0;
    terminal->line_start.x = 0;
    terminal->line_start.y = 0;
    /* SDL_TerminalAddText (terminal, terminal->line); */
    return 0;
}


/*
 * Print text at current cursor position
 *
 * 
 * @return	If print was successful, it returns 0, otherwise it returns -1
 */
int
SDL_TerminalPrint (SDL_Terminal *terminal, char *text, ...)
{
    va_list argument;
    char *line = (char *) malloc (SDL_TERMINAL_MAX_LINE_SIZE*sizeof(char));
    if (line == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return -1;
    }
    va_start (argument, text);
    vsnprintf (line, SDL_TERMINAL_MAX_LINE_SIZE-1, text, argument);
    va_end (argument);

    SDL_TerminalAddText (terminal, line);
    SDL_TerminalBufferAddText (terminal, line);

    terminal->line_start.x = terminal->cpos.x;
    terminal->line_start.y = terminal->cpos.y;

    free (line);
    return 0;
}


/*
 * Print text at given cursor position
 *
 * 
 * @return	If print was successful, it returns 0, otherwise it returns -1
 */
int
SDL_TerminalPrintAt (SDL_Terminal *terminal, int row, int column, char *text, ...)
{
    if ((row > 0) && (column > 0) && (row < terminal->size.row) && (column < terminal->size.column)) {
        terminal->cpos.row = row;
        terminal->cpos.column = column;

        va_list argument;
        va_start (argument, text);
        int ret = SDL_TerminalPrint (terminal, text, argument);
        va_end (argument);
        return ret;
    } else {
        return -1;
    }
    return 0;
}


int
SDL_TerminalSetSize (SDL_Terminal *terminal, int column, int row)
{
    int w = column * terminal->glyph_size.w + 2*terminal->br_size;
    int h = row    * terminal->glyph_size.h + 2*terminal->br_size;

#ifdef HAVE_OPENGL
    if (terminal->texture) {
        if (w > terminal->texture_size.w) {
            w = (terminal->texture_size.w - 2*terminal->br_size) / terminal->glyph_size.w;
            w *= terminal->glyph_size.w;
            w += 2*terminal->br_size;
        }
        if (h > terminal->texture_size.h) {
            h = (terminal->texture_size.h - 2*terminal->br_size) / terminal->glyph_size.h;
            h *= terminal->glyph_size.h;
            h += 2*terminal->br_size;
        }        
    }
#endif
    terminal->psize.w = w;
    terminal->psize.h = h;
    terminal->size.row = (h-2*terminal->br_size)/terminal->glyph_size.h;
    terminal->size.column = (w-2*terminal->br_size)/terminal->glyph_size.w;

	if (terminal->window) {
		
		/* Create a new texture */
		SDL_Texture *nTexture = SDL_CreateTexture(terminal->window->Renderer, 
			SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		
		/* Update texture if neccessary */
		if (terminal->window->Texture) {
			SDL_Texture *oTexture = terminal->window->Texture;
			terminal->window->Texture = nTexture;
			SDL_DestroyTexture(oTexture);
		}
		else
			terminal->window->Texture = nTexture;

		/* Update dimensions */
		terminal->window->Dimensions.w = w;
		terminal->window->Dimensions.h = h;
	}

    if (terminal->surface)
        SDL_FreeSurface (terminal->surface);
    terminal->surface = SDL_CreateRGBSurface (SDL_SWSURFACE, w, h, 32, SDL_TERMINAL_MASK);
    if (terminal->surface == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        terminal->status = 0;
        return -1;
    }
	SDL_SetSurfaceAlphaMod(terminal->surface, 0);
    SDL_TerminalRefresh (terminal);
    return 0;
}

int
SDL_TerminalSetPosition (SDL_Terminal *terminal, int x, int y)
{
    terminal->position.x = x;
    terminal->position.y = y;
    return 0;
}

int
SDL_TerminalSetColor (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{
    terminal->color.r = red;
    terminal->color.g = green;
    terminal->color.b = blue;
    terminal->color.a = alpha;
    return 0;
}

int
SDL_TerminalSetBorderColor (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{
    terminal->br_color.r = red;
    terminal->br_color.g = green;
    terminal->br_color.b = blue;
    terminal->br_color.a = alpha;
    return 0;
}

int
SDL_TerminalSetBorderSize (SDL_Terminal *terminal, int size)
{
    if (size > 1) {
        terminal->br_size = size;
        SDL_TerminalSetSize (terminal, terminal->size.row, terminal->size.column);
        return 0;
    }
    return -1;
}

int
SDL_TerminalReset (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_STYLE_NORMAL);
    SDL_TerminalSetForeground (terminal,
                               terminal->default_fg_color.r,
                               terminal->default_fg_color.g,
                               terminal->default_fg_color.b,
                               terminal->default_fg_color.a);
    SDL_TerminalSetBackground (terminal,
                               terminal->default_bg_color.r,
                               terminal->default_bg_color.g,
                               terminal->default_bg_color.b,
                               0);    
    return 0;
}

int
SDL_TerminalEnableBold (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) | TTF_STYLE_BOLD);
    return 0;
}

int
SDL_TerminalDisableBold (SDL_Terminal *terminal)
{ 
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) & ~TTF_STYLE_BOLD);
    return 0;
}

int
SDL_TerminalEnableUnderline (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) | TTF_STYLE_UNDERLINE);
    return 0;
}

int
SDL_TerminalDisableUnderline (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) & ~TTF_STYLE_UNDERLINE);
    return 0;
}

int
SDL_TerminalEnableItalic (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) | TTF_STYLE_ITALIC);
    return 0;
}

int
SDL_TerminalDisableItalic (SDL_Terminal *terminal)
{
    TTF_SetFontStyle (terminal->font, TTF_GetFontStyle (terminal->font) & ~TTF_STYLE_ITALIC);
    return 0;
}

int
SDL_TerminalSetDefaultForeground (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{   
    terminal->default_fg_color.r = red;
    terminal->default_fg_color.g = green;
    terminal->default_fg_color.b = blue;
    terminal->default_fg_color.a = alpha;
    return 0;
}

int
SDL_TerminalSetDefaultBackground (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{   
    terminal->default_bg_color.r = red;
    terminal->default_bg_color.g = green;
    terminal->default_bg_color.b = blue;
    terminal->default_bg_color.a = alpha;
    return 0;
}

int
SDL_TerminalSetForeground (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{   
    terminal->fg_color.r = red;
    terminal->fg_color.g = green;
    terminal->fg_color.b = blue;
    terminal->fg_color.a = alpha;
    return 0;
}

int
SDL_TerminalSetBackground (SDL_Terminal *terminal, int red, int green, int blue, int alpha)
{   
    terminal->bg_color.r = red;
    terminal->bg_color.g = green;
    terminal->bg_color.b = blue;
    terminal->bg_color.a = alpha;
    return 0;
}





/*
 * Load a new font
 *
 * @param	terminal		SDL Terminal to modify
 * @param	filename		font filename
 * @param	size			font size
 *
 * @return					If font loading was successful, it returns 0, otherwise it returns -1
 */
int
SDL_TerminalSetFont (SDL_Terminal *terminal, char *filename, int size) 
{
    /* Is it the same font than the current one ? */
    if ((terminal->font_filename) 
        && (strcmp (terminal->font_filename, filename) == 0)
        && (terminal->font_size == size)) {
        return 0;
    }

    if ((size < 5) || (size > 128))
        return -1;

    TTF_Font *font = 0;
    if (!TTF_WasInit() && (TTF_Init()==-1)) {
        SDL_SetError ("TTF_Init: %s\n", TTF_GetError());
        return -1;
    }

    font = TTF_OpenFont(filename, size);
    if (font == NULL) {
        SDL_SetError ("TTF_OpenFont: %s\n", TTF_GetError());
        return -1;
    }    
    if (!TTF_FontFaceIsFixedWidth (font)) {
        SDL_SetError ("Cannot use non fixed-width font\n");
        TTF_CloseFont (font);
        return -1;
    }

    int font_style = -1;
    if (terminal->font) {
        font_style = TTF_GetFontStyle(terminal->font);
        TTF_CloseFont (terminal->font);
    }
    terminal->font = font;

    if ((terminal->font_filename) && (terminal->font_filename != filename))
        free (terminal->font_filename);
    terminal->font_filename = strdup (filename);
    terminal->font_size = size;
    if (font_style != -1)
        TTF_SetFontStyle (terminal->font, font_style);
    else
        TTF_SetFontStyle (terminal->font, TTF_STYLE_NORMAL);

    /* Get glyph size */
    terminal->glyph_size.h = TTF_FontHeight (terminal->font);    
    TTF_GlyphMetrics(terminal->font, (Uint16)('A'), 0, 0, 0, 0, &terminal->glyph_size.w);


    /* Resize terminal according to new font size */
    SDL_TerminalSetSize (terminal, terminal->size.row, terminal->size.column);

    return 1;
}




/*
 * PRIVATE
 * Add given text at current cursor position
 * Handle some color ansi codes.
 *
 * @param terminal		terminal where text is to be added
 * @param t				text to be added
 * @return				number of added character
 */
int SDL_TerminalAddText (SDL_Terminal *terminal, char *text, ...)
{
    if (!text)
        return 0;

    va_list argument;
    static char line[1024];
    va_start (argument, text);
    vsnprintf (line, 1023, text, argument);
    va_end (argument);

    int i=0;
    int c = 0;
    while (line[i] != 0) {
        if (strstr ((line+i), "\033[") == (char *)(line+i)) {
            int sequence = 1;
            i += 2;

            while (sequence) {
                /* Reset terminal attributes */
                if (strstr ((line+i), "00") == (char *)(line +i)) {
                    SDL_TerminalReset (terminal);
                    i+=2;
                /* Bold on */
                } else if (strstr ((line+i), "01") == (char *)(line +i)) {
                    SDL_TerminalEnableBold (terminal);
                    i+=2;
                /* Bold off */
                } else if (strstr ((line+i), "22") == (char *)(line +i)) {
                    SDL_TerminalDisableBold (terminal);
                    i+=2;
                /* Italic on */
                } else if (strstr ((line+i), "03") == (char *)(line +i)) {
                    SDL_TerminalEnableItalic (terminal);
                    i+=2;                    
                /* Italic off */    
                } else if (strstr ((line+i), "23") == (char *)(line +i)) {
                    SDL_TerminalDisableItalic (terminal);
                    i+=2;
                    
                } else if (strstr ((line+i), "04") == (char *)(line +i)) {
                    SDL_TerminalEnableUnderline (terminal);
                    i+=2;
                /* Underline off */
                } else if (strstr ((line+i), "24") == (char *)(line +i)) {
                    SDL_TerminalDisableUnderline (terminal);
                    i+=2;    
                /* Black foreground */
                } else if (strstr ((line+i), "30") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 0,0,0,255);
                    i += 2;    
                /* Red foreground */
                } else if (strstr ((line+i), "31") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 255,0,0,255);
                    i += 2;
                /* Green foreground */
                } else if (strstr ((line+i), "32") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 0,255,0,255);
                    i += 2;    
                /* Yellow foreground */
                } else if (strstr ((line+i), "33") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 255,255,0,255);
                    i += 2;    
                /* Blue foreground */
                } else if (strstr ((line+i), "34") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 0,0,255,255);
                    i += 2;    
                /* Magenta foreground */
                } else if (strstr ((line+i), "35") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 255,0,255,255);
                    i += 2;
                /* Cyan foreground */
                } else if (strstr ((line+i), "36") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 0,255,255,255);
                    i += 2;
                /* White foreground */
                } else if (strstr ((line+i), "37") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal, 255,255,255,255);
                    i += 2;
                /* Default foreground */
                } else if (strstr ((line+i), "39") == (char *)(line +i)) {
                    SDL_TerminalSetForeground (terminal,
                                               terminal->default_fg_color.r,
                                               terminal->default_fg_color.g,
                                               terminal->default_fg_color.b,
                                               terminal->default_fg_color.a);
                    i += 2;    
                /* Black background */
                } else if (strstr ((line+i), "40") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 0,0,0,255);
                    i += 2;
                /* Red background */
                } else if (strstr ((line+i), "41") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 255,0,0,255);
                    i += 2;    
                /* Green background */
                } else if (strstr ((line+i), "42") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 0,255,0,255);
                    i += 2;    
                /* Yellow background */
                } else if (strstr ((line+i), "43") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 255,255,0,255);
                    i += 2;
                /* Blue background */
                } else if (strstr ((line+i), "44") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 0,0,255,255);
                    i += 2;
                /* Magenta background */
                } else if (strstr ((line+i), "45") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 255,0,255,255);
                    i += 2;    
                /* Cyan background */
                } else if (strstr ((line+i), "46") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 0,255,255,255);
                    i += 2;    
                /* White (default) background */
                } else if (strstr ((line+i), "47") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal, 255,255,255,255);
                    i += 2;
                /* Default foreground */
                } else if (strstr ((line+i), "49") == (char *)(line +i)) {
                    SDL_TerminalSetBackground (terminal,
                                               terminal->default_bg_color.r,
                                               terminal->default_bg_color.g,
                                               terminal->default_bg_color.b,
                                               0);
                    i+=2;
                /* Reset terminal attributes */
                } else if (strstr ((line+i), "0") == (char *)(line +i)) {
                    SDL_TerminalReset (terminal);
                    i+=1;
                /* Bold on */
                } else if (strstr ((line+i), "1") == (char *)(line +i)) {
                    SDL_TerminalEnableBold (terminal);
                    i+=1;
                /* Italic on */
                } else if (strstr ((line+i), "3") == (char *)(line +i)) {
                    SDL_TerminalEnableItalic (terminal);
                    i+=1;
                /* Underline on */
                } else if (strstr ((line+i), "4") == (char *)(line +i)) {
                    SDL_TerminalEnableUnderline (terminal);
                    i+=1;
                }

                /* End of sequence */
                if (line[i++] == ';')
                    sequence = 1;
                else 
                    sequence = 0;
            }

            if (line[i] == 0)
                break;
        }
        else
            SDL_TerminalAddChar (terminal, line[i++]);
        c++;
    }
    return c;
}

/*
 * Refresh terminal according to text buffer
 *
 */
int
SDL_TerminalRefresh (SDL_Terminal *terminal)
{
    int line = terminal->cpos.y;
    SDL_TerminalClear (terminal);
    int i = terminal->buffer_size-1;
    while ((i > 0) && (line > 0)) {
        line -= SDL_TerminalGetNumberOfLine (terminal, terminal->buffer[i--]);
    }
    
    i++;
    int j;

    for (j=i; j<terminal->buffer_size; j++)
        SDL_TerminalAddText (terminal, terminal->buffer[j]);
    SDL_TerminalAddText (terminal, terminal->line);
    return 0;
}

/*
 * Add given character at current cursor position
 *
 */
int
SDL_TerminalAddChar (SDL_Terminal *terminal, char c)
{
    if (c == '\n') {
        terminal->cpos.x = 0;
        terminal->cpos.y++;
        if (terminal->cpos.y >= terminal->size.row) {
            SDL_TerminalScroll (terminal, 1);
            terminal->cpos.y--;
        }
        return 0;
    }
    
    char cc = c;
    int dx = 1;
    if (c == '\t') {
        cc = ' ';
        dx = terminal->tabsize;
    }
    
    int i;
    for (i=0; i<dx; i++) {
        SDL_TerminalRenderChar (terminal, 
                                terminal->br_size+terminal->cpos.x * terminal->glyph_size.w,
                                terminal->br_size+terminal->cpos.y * terminal->glyph_size.h,
                                cc);
        terminal->cpos.x++;
        if (terminal->cpos.x >= terminal->size.column) {
            terminal->cpos.x -= terminal->size.column;
            terminal->cpos.y++;
            if (terminal->cpos.y >= terminal->size.row) {
                SDL_TerminalScroll (terminal, 1);
                terminal->cpos.y--;
            }
        }   
    }

    return 0;
}


/*
 * Add given character to current line
 *
 */
int
SDL_TerminalLineInsertChar (SDL_Terminal *terminal, char c)
{
    if (strlen(terminal->line) >= (SDL_TERMINAL_MAX_LINE_SIZE-1))
        return -1;
    memmove (&terminal->line[terminal->line_pos+1], &terminal->line[terminal->line_pos], strlen(terminal->line)-terminal->line_pos+1);
    terminal->line[terminal->line_pos++] = c;
    return 0;
}

/*
 * Delete character of current line at current pos
 *
 */
int
SDL_TerminalLineRemoveChar (SDL_Terminal *terminal, int pos)
{
    if (pos < 0) 
        return -1;
    if (strlen(terminal->line) == 0)
        return -1;
    memmove (&terminal->line[pos], &terminal->line[pos+1], strlen(terminal->line)-pos+1);
    terminal->line_pos--;
    return 0;
}

/*
 * Add given text to the text buffer
 *
 */
int SDL_TerminalBufferAddText (SDL_Terminal *terminal, char *text, ...)
{
    if (!text)
        return -1;

    if (terminal->buffer[0])
        free (terminal->buffer[0]);
    int i;
    for (i=1; i<terminal->buffer_size; i++)
        terminal->buffer[i-1] = terminal->buffer[i];
    
    terminal->buffer[terminal->buffer_size-1] = (char *) malloc (SDL_TERMINAL_MAX_LINE_SIZE*sizeof(char));
    if (terminal->buffer[terminal->buffer_size-1] == NULL) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        terminal->status = 0;
        return -1;
    }

    va_list argument;
    va_start (argument, text);
    vsnprintf (terminal->buffer[terminal->buffer_size-1], SDL_TERMINAL_MAX_LINE_SIZE-1, text, argument);
    va_end (argument);
    terminal->buffer[terminal->buffer_size-1][SDL_TERMINAL_MAX_LINE_SIZE-1] = '\0';
    return 0;
}

/*
 * Process a new line by updating history and pushing an event on SDL
 * queue.
 */
int
SDL_TerminalNewline (SDL_Terminal *terminal)
{
    /*  Store a copy (without trailing \n) of the line within the event structure */
    if (terminal->event.user.data2)
        free (terminal->event.user.data2);
    terminal->event.user.data2 = strdup (terminal->line);

            
    /* Store a copy within history
       do not store blank lines
       do not store line if it is just before in history
    */
    if ((strcmp(terminal->line, "") != 0) &&
        ((terminal->history[terminal->history_size-2] == 0) ||
         (strcmp (terminal->line, terminal->history[terminal->history_size-2]) != 0))) {

        int i;
        if (terminal->history[0])
            free (terminal->history[0]);

        if (!terminal->history[terminal->history_size-1]) {
            terminal->history[terminal->history_size-1] = (char *) malloc (SDL_TERMINAL_MAX_LINE_SIZE*sizeof(char));
            if (terminal->history[terminal->history_size-1] == NULL) {
				SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
                return -1;
            }
        }
        strcpy (terminal->history[terminal->history_size-1], terminal->line);
        for (i=1; i<terminal->history_size; i++)
            terminal->history[i-1] = terminal->history[i];

        terminal->history[terminal->history_size-1] = (char *) malloc (SDL_TERMINAL_MAX_LINE_SIZE*sizeof(char));
        if (terminal->history[terminal->history_size-1] == NULL) {
			SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
            return -1;
        }
        terminal->history[0] = '\0';
        terminal->history_current = terminal->history_size-1;
    }


    /* Update cursor position */
    terminal->cpos.x = 0;
    terminal->cpos.y++;
    if (terminal->cpos.y >= terminal->size.row) {
        SDL_TerminalScroll (terminal, 1);
        terminal->cpos.y--;
    }

    /* Add trailing '\n' to go to next line */
    SDL_TerminalLineInsertChar (terminal, '\n');

    /* Update text buffer (with '\n') */
    SDL_TerminalBufferAddText (terminal, terminal->line);

    /* Misc. updates */
    terminal->line[0] = '\0';
    terminal->line_pos = 0;
    terminal->line_start.x = terminal->cpos.x;
    terminal->line_start.y = terminal->cpos.y;
    
    /* Push event on stack */
    terminal->event.type = SDL_TERMINALEVENT;
    terminal->event.user.code = 0;
    terminal->event.user.data1 = terminal;
    SDL_PushEvent (&terminal->event);

    return 0;
}

/*
 * PRIVATE
 * Yank previous history line
 *
 * @param terminal		terminal where history has to be yanked
 * @return				/
 */
int
SDL_TerminalHistoryPrev (SDL_Terminal *terminal)
{
    if ((terminal->history_current <= 0) || (terminal->history[terminal->history_current-1] == 0))
        return -1;

    /* Save current entry within history */
    if (terminal->history_current == terminal->history_size-1) {
        strcpy (terminal->history[terminal->history_size-1], terminal->line);
    }

    /* Copy history to entry */
    terminal->history_current--;
    strcpy (terminal->line, terminal->history[terminal->history_current]);
    terminal->line_pos = strlen(terminal->line);

    /* Refresh terminal */
    SDL_TerminalClearFrom (terminal, terminal->line_start.x, terminal->line_start.y);
    terminal->cpos.x = terminal->line_start.x;
    terminal->cpos.y = terminal->line_start.y;
    SDL_TerminalAddText (terminal, terminal->line);
    
    return 0;
}

/*
 * PRIVATE
 * Yank next history line
 *
 * @param terminal		terminal where history has to be yanked
 * @return				/
 */
int
SDL_TerminalHistoryNext (SDL_Terminal *terminal)
{
    if (terminal->history_current >= terminal->history_size-1)
        return -1;
    
    /* Copy history to entry */
    terminal->history_current++;
    strcpy (terminal->line, terminal->history[terminal->history_current]);
    terminal->line_pos = strlen (terminal->line);

    /* Refresh terminal */
    SDL_TerminalClearFrom (terminal, terminal->line_start.x, terminal->line_start.y);
    terminal->cpos.x = terminal->line_start.x;
    terminal->cpos.y = terminal->line_start.y;
    SDL_TerminalAddText (terminal, terminal->line);

    return 0;
}


/*
 * Computes how many display lines would need a given text.
 * Note: ascii sequences are removed, newline are taken into account.
 */
int
SDL_TerminalGetNumberOfLine (SDL_Terminal *terminal, char *text)
{
    if (text == NULL)
        return 0;

    int line = 0;
    int i=0, j=0;

	while (i<(int)strlen(text)) {
        if (strstr ((text+i), "\033[") == (char *)(text+i)) {
            i+=2;
			while ((text[i] != 'm') && (i<(int)strlen(text)))
                i++;
            i++;
        }
        if (text[i] == '\n') {
            line++;
            j = 0;
        }
        i++;
        j++;
        if (j == (terminal->size.column-1)) {
            j=0;
            line++;
        }
    }
    return line;
}


/*
 * Erase terminal from given character based position 
 *
 */
int SDL_TerminalClearFrom (SDL_Terminal *terminal, int column, int row)
{
    Uint32 color = SDL_MapRGBA (terminal->surface->format, terminal->color.r, terminal->color.g, terminal->color.b, terminal->color.a);
	SDL_SetSurfaceAlphaMod(terminal->surface, 0);

    if ((column <= (terminal->size.column-1)) && (row <= (terminal->size.row-1))) {
        SDL_Rect dst = {terminal->br_size + column*terminal->glyph_size.w,
                        terminal->br_size + row*terminal->glyph_size.h,
                        (terminal->size.column-column)*terminal->glyph_size.w,
                        (terminal->size.row-row)*terminal->glyph_size.h};
        SDL_FillRect (terminal->surface, &dst, color);
        SDL_TerminalUpdateGLTexture (terminal, &dst);
    }
    if (row < (terminal->size.row-1)) {
        SDL_Rect dst = {terminal->br_size,
                        terminal->br_size + (row+1) * terminal->glyph_size.h,
                        terminal->size.column       * terminal->glyph_size.w,
                        (terminal->size.row-row-1) * terminal->glyph_size.h};
        SDL_FillRect (terminal->surface, &dst, color);
        SDL_TerminalUpdateGLTexture (terminal, &dst);
    }

    return 1;
}

/* 
 *
 */
int
SDL_TerminalScroll (SDL_Terminal *terminal, int n)
{
    if (n >= terminal->size.row)
        n = terminal->size.row;

    if (n < terminal->size.row) {
        SDL_Rect src_rect = {terminal->br_size, terminal->br_size+n*terminal->glyph_size.h,
                             terminal->surface->w-2*terminal->br_size, terminal->surface->h - 2*terminal->br_size - n*terminal->glyph_size.h};

        SDL_Rect dst_rect = {terminal->br_size, terminal->br_size,
                             terminal->surface->w-2*terminal->br_size, terminal->surface->h - 2*terminal->br_size - n*terminal->glyph_size.h};
		SDL_SetSurfaceAlphaMod(terminal->surface, 0);
        SDL_BlitSurface (terminal->surface, &src_rect, terminal->surface, &dst_rect);
        SDL_TerminalUpdateGLTexture (terminal, &dst_rect);
    }
    SDL_TerminalClearFrom (terminal, 0, terminal->size.row-n);

    return n;
}

/*
 * Erase cursor at current position
 */
int
SDL_TerminalEraseCursor (SDL_Terminal *terminal) 
{
    char c = ' ';
	if (terminal->line_pos < (int)strlen(terminal->line)) {
        c = terminal->line[terminal->line_pos];
        if (c == '\t')
            c = ' ';
    }
    return SDL_TerminalRenderChar (terminal,
                                   terminal->br_size + terminal->cpos.x * terminal->glyph_size.w,
                                   terminal->br_size + terminal->cpos.y * terminal->glyph_size.h,
                                   c);
}


/*
 * Render cursor at current position
 */
int
SDL_TerminalRenderCursor (SDL_Terminal *terminal) 
{
    SDL_Color fg = terminal->fg_color;
    SDL_Color bg = terminal->bg_color;
    terminal->fg_color = terminal->color;
    terminal->bg_color.a = 255;
    char c = ' ';
	if (terminal->line_pos < (int)strlen(terminal->line)) {
        c = terminal->line[terminal->line_pos];
        if (c == '\t')
            c = ' ';
    }
    int r = SDL_TerminalRenderChar (terminal,
                                    terminal->br_size + terminal->cpos.x * terminal->glyph_size.w,
                                    terminal->br_size + terminal->cpos.y * terminal->glyph_size.h,
                                    c);
    terminal->fg_color = fg;
    terminal->bg_color = bg;    
    return r;
}

/*
 * Render character 'c' onto terminal surface at given position.
 */
int
SDL_TerminalRenderChar (SDL_Terminal *terminal, int x, int y, char c)
{
    char buffer[] = " ";
    buffer[0] = c;
    SDL_Surface *text_surf = 0;

    if (terminal->bg_color.a)
        text_surf = TTF_RenderText_Shaded (terminal->font, buffer, terminal->fg_color, terminal->bg_color);
    else
        text_surf = TTF_RenderText_Blended (terminal->font, buffer, terminal->fg_color);
    if (text_surf == 0) {
		SDL_SetError("SDL Error <out of memory>: %i", SDL_ENOMEM);
        return -1;
    }

    SDL_TerminalEraseChar (terminal, x, y);

    SDL_Rect src = {0, 0, terminal->glyph_size.w, terminal->glyph_size.h};
    SDL_Rect dst = {x, y, terminal->glyph_size.w, terminal->glyph_size.h};
    SDL_BlitSurface (text_surf, &src, terminal->surface, &dst);

    /* Make character opaque when it is rendered on a transparent background */
    if (text_surf->format->BytesPerPixel == 4) {
        int i,j;
        for (i=0; i<terminal->glyph_size.w; i++) {
            for (j=0; j<terminal->glyph_size.h; j++) {
				Uint8 *s = (Uint8 *)((Uint8*)terminal->surface->pixels + (x + i) * 4 + (y + j)*terminal->surface->pitch);
				Uint8 *t = (Uint8 *)((Uint8*)text_surf->pixels + i * 4 + j*text_surf->pitch);
                if (*(t+3) > *(s+3))
                    *(s+3) = *(t+3);
            }
        }
    }
    
    SDL_FreeSurface (text_surf);
    return SDL_TerminalUpdateGLTexture (terminal, &dst);
}

/* 
 * Erase one character from terminal surface at given position.
 */
int
SDL_TerminalEraseChar (SDL_Terminal *terminal, int x, int y)
{
    Uint32 color = SDL_MapRGBA (terminal->surface->format, terminal->color.r, terminal->color.g, terminal->color.b, terminal->color.a);
	SDL_SetSurfaceAlphaMod(terminal->surface, 0);
    SDL_Rect dst = {x, y, terminal->glyph_size.w, terminal->glyph_size.h};
    SDL_FillRect (terminal->surface, &dst, color);
    return SDL_TerminalUpdateGLTexture (terminal, &dst);
}


/* 
 * Update a given rectangular area of the GL texture.
 * Note: Instead of mapping the whole texture, we first copy the
 *       relevant area within a SDL buffer (at the right size) and
 *       then we map this buffer only to the GL texture. This way
 *       we avoid updating the whole texture each time a character
 *       is added.
 *
 * src_rect is the 	rectangular area to be updated
 */
int
SDL_TerminalUpdateGLTexture (SDL_Terminal *terminal, SDL_Rect *src_rect)
{
#ifdef HAVE_OPENGL
    GLint current_texture;
    static SDL_Surface *buffer = 0;

    glGetIntegerv (GL_TEXTURE_BINDING_2D, &current_texture);
    glBindTexture (GL_TEXTURE_2D, terminal->texture);

    if (src_rect) {
        if ((!buffer) || (buffer->w != src_rect->w) || (buffer->h != src_rect->h)) {
            if (buffer)
                SDL_FreeSurface (buffer);
            buffer = SDL_CreateRGBSurface (SDL_SWSURFACE, src_rect->w, src_rect->h, 32, SDL_TERMINAL_MASK );
            if (buffer == NULL ) {
                SDL_SetError(SDL_ENOMEM);
                glBindTexture (GL_TEXTURE_2D, current_texture);
                return -1;
            }
        }
    
        /* Copy relevant area of the terminal surface to the buffer */
        SDL_BlitSurface (terminal->surface, src_rect, buffer, 0);

        /* Map the up-to-date buffer within texture map */
        glTexSubImage2D (GL_TEXTURE_2D, 0, src_rect->x, src_rect->y, src_rect->w, src_rect->h,
                         GL_RGBA, GL_UNSIGNED_BYTE, buffer->pixels);
    } else {
        /* Map whole terminal surface within texture map */
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, terminal->surface->w, terminal->surface->h,
                         GL_RGBA, GL_UNSIGNED_BYTE, terminal->surface->pixels);
    }

    /* Bind saved texture id */
    glBindTexture (GL_TEXTURE_2D, current_texture);
#endif
    return 0;
}



