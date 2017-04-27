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
* MollenOS Window - Sapphire
*/

/* Includes */
#include <SDL.h>
#include <SDL_image.h>
#include "Scene.h"
#include "Window.h"

/* CLib */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Window decoration definitions
 * generally size and such */
#define DECO_BAR_TOP_HEIGHT		45
#define DECO_BAR_LEFT_WIDTH		2
#define DECO_BAR_RIGHT_WIDTH	2
#define DECO_BAR_BOTTOM_HEIGHT	2

#define DECO_SHADOW				5

/* BitBlt Flags */
#define BITBLT_SRCSET			0x1
#define BITBLT_SRCCOPY			0x2

/* Prototypes */
void SDL_DrawRound(SDL_Texture *Target,
	Sint16 x0, Sint16 y0, Uint16 w, Uint16 h, Uint16 corner, Uint32 color);

/* This is a dword size memset 
 * utilized to clear a surface to a given color */
void memsetd(void *Buffer, uint32_t Color, size_t Count)
{
	/* Calculate the number of iterations
	 * in bytes of 4 */
	uint32_t *ItrPtr = (uint32_t*)Buffer;

	/* Iterate and set color */
	for (size_t i = 0; i < Count; i++, ItrPtr++) {
		*ItrPtr = Color;
	}
}

/* Utility to fill a rect with a rect 
 * can be usefull when we don't want to copy a full buffer
 * to another full buffer */
void BitBlt(uint8_t *Dest, size_t DestPitch, int DestX, int DestY,
	uint8_t *Src, size_t SrcPitch, int SrcX, int SrcY, int Width, int Height, uint Flags)
{
	/* Calculate indices before we start
	 * to optimize to a one-time thing */
	uint8_t *SrcPointer = Src + (SrcPitch * SrcY) + (SrcX * 4);
	uint8_t *DestPointer = Dest + (DestPitch * DestY) + (DestX * 4);

	/* Start a loop */
	for (int Row = 0; Row < Height; Row++) {
		if (Flags & BITBLT_SRCSET) {
			memsetd(DestPointer, (uint32_t)((uint32_t*)Src), Width);
			DestPointer += DestPitch;
		}
		else {
			memcpy(DestPointer, SrcPointer, Width * 4);
			SrcPointer += SrcPitch;
			DestPointer += DestPitch;
		}
	}
}

/* Utility to intersect two rectangles, we utilize 
 * the inbuilt SDL function for this, but we use our own struct */
void RectIntersect(Rect_t *RectA, Rect_t *RectB, Rect_t *Result)
{
	/* We have to proxy some values ... */
	SDL_Rect A, B, R;

	/* Copy over first rectangle data */
	A.x = RectA->x;
	A.y = RectA->y;
	A.w = RectA->w;
	A.h = RectA->h;

	/* Copy over second rectangle data */
	B.x = RectB->x;
	B.y = RectB->y;
	B.w = RectB->w;
	B.h = RectB->h;

	/* Do the call */
	if (SDL_IntersectRect(&A, &B, &R)) {
		Result->x = R.x;
		Result->y = R.y;
		Result->w = R.w;
		Result->h = R.h;
	}
	else {
		Result->w = 0;
		Result->h = 0;
	}
}

/* Utility to draw rounded shadows in edges
 * of texture, uses SDL_draw code */
void SDL_Shadows(SDL_Texture *Texture, int Width, int Height)
{
	for (int i = 0; i < DECO_SHADOW; i++) {
		SDL_DrawRound(Texture, i, i, Width - (i * 2), Height - (i * 2), 
			2, 0x00000000 + (i * 0x3F000000));
	}
}

/* Constructor
 * Allocates a new window of the given
 * dimensions and initializes it */
Window_t *WindowCreate(IpcComm_t Owner, Rect_t *Dimensions, int Flags, SDL_Renderer *Renderer)
{
	/* Allocate a new window instance */
	Window_t *Window = (Window_t*)malloc(sizeof(Window_t));
	void *mPixels = NULL;
	int mPitch = 0;
	
	/* Set initial stuff */
	Window->Id = 0;
	Window->Owner = Owner;
	Window->Flags = Flags;
	Window->Flip = SDL_FLIP_NONE;
	Window->Rotation = 0.0;
	Window->zIndex = 1000;
	Window->Texture = NULL;
	Window->Renderer = Renderer;

	/* Convert dims 
	 * The full-dimension is screen absolute */
	Window->FullDimensions.x = Dimensions->x;
	Window->FullDimensions.y = Dimensions->y;
	
	/* Adjust by header and shadow 
	 * it's important to do this in full-dims */
	Window->FullDimensions.h = Dimensions->h + DECO_BAR_TOP_HEIGHT + (2 * DECO_SHADOW);
	Window->FullDimensions.w = Dimensions->w + (2 * DECO_SHADOW);

	/* Convert dims 
	 * The content-dimensions are relative 
	 * But x is adjusted by shadow, y is adjusted by both shadow and header */
	Window->ContentDimensions.x = Dimensions->x + DECO_SHADOW;
	Window->ContentDimensions.y = Dimensions->y + (DECO_BAR_TOP_HEIGHT + DECO_SHADOW);
	
	/* These are kept intact */
	Window->ContentDimensions.h = Dimensions->h;
	Window->ContentDimensions.w = Dimensions->w;

	/* Allocate a texture for the full dimensions of 
	 * the window */
	Window->Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, Window->FullDimensions.w, Window->FullDimensions.h);

	/* Set blend mode for the backbuffer
	 * texture, we only do this for it */
	SDL_SetTextureBlendMode(Window->Texture, SDL_BLENDMODE_BLEND);

	/* Get texture information */
	SDL_LockTexture(Window->Texture, NULL, &mPixels, &mPitch);
	memset(mPixels, 0, Window->FullDimensions.h * mPitch);
	SDL_UnlockTexture(Window->Texture);

	/* Draw Shadows */
	SDL_Shadows(Window->Texture, Window->FullDimensions.w, Window->FullDimensions.h);

	/* Allocate a user-backbuffer for only the content
	 * therefore we can't exactly use mPitch if Content.w != Full.w */
	Window->Backbuffer = malloc(Window->ContentDimensions.h * mPitch);
	Window->BackbufferSize = Window->ContentDimensions.h * mPitch;
	memsetd(Window->Backbuffer, 0xFFFFFFFF, (Window->ContentDimensions.h * mPitch) / 4);

	/* Now lock the texture again */
	SDL_LockTexture(Window->Texture, NULL, &mPixels, &mPitch);

	/* Fill the header */
	BitBlt(mPixels, mPitch, DECO_SHADOW, DECO_SHADOW, (uint8_t*)0xFFCCCCCC, 0, 0, 0,
		Window->ContentDimensions.w, DECO_BAR_TOP_HEIGHT, BITBLT_SRCSET);

	/* Fill the content => Copy pixels */
	BitBlt(mPixels, mPitch, DECO_SHADOW, DECO_BAR_TOP_HEIGHT + DECO_SHADOW,
		Window->Backbuffer, mPitch, 0, 0,
		Window->ContentDimensions.w, Window->ContentDimensions.h, BITBLT_SRCCOPY);

	/* Unlock texture */
	SDL_UnlockTexture(Window->Texture);

	/* Draw the image */

	/* Draw the text */

	/* Done */
	return Window;
}

/* Destructor
 * Cleans up and releases
 * resources allocated */
void WindowDestroy(Window_t *Window)
{
	/* Sanity */
	if (Window == NULL)
		return;

	/* Free sdl-stuff */
	SDL_DestroyTexture(Window->Texture);

	/* Free resources */
	free(Window->Backbuffer);
	free(Window);
}

/* Update
 * Updates all neccessary state and
 * buffers before rendering */
void WindowUpdate(Window_t *Window, Rect_t *DirtyArea)
{
	/* Variables needed for update */
	Rect_t Intersection;
	void *mPixels = NULL;
	int mPitch = 0;

	/* Sanity */
	if (Window == NULL)
		return;

	/* Check intersection */
	if (DirtyArea != NULL) {
		RectIntersect(&Window->ContentDimensions, DirtyArea, &Intersection);
	}
	else {
		Intersection.x = Window->ContentDimensions.x;
		Intersection.y = Window->ContentDimensions.y;
		Intersection.w = Window->ContentDimensions.w;
		Intersection.h = Window->ContentDimensions.h;
	}

	/* Sanity 
	 * In case there was no intersection */
	if (Intersection.w == 0 && Intersection.h == 0) {
		return;
	}

	/* Lock texture */
	SDL_LockTexture(Window->Texture, NULL, &mPixels, &mPitch);

	/* Copy pixels */
	BitBlt(mPixels, mPitch, (Intersection.x - Window->FullDimensions.x), (Intersection.y - Window->FullDimensions.x),
		Window->Backbuffer, mPitch, (Intersection.x - Window->ContentDimensions.x), (Intersection.y - Window->ContentDimensions.y),
		Intersection.w, Intersection.h, BITBLT_SRCCOPY);

	/* Unlock texture */
	SDL_UnlockTexture(Window->Texture);
}

/* Render
 * Renders the window to the given renderer 
 * Right now it does not use rect-invalidation
 * so any invalidate out of scope faults */
void WindowRender(Window_t *Window, SDL_Renderer *Renderer, Rect_t *DirtyArea)
{
	/* The rects to be updated */
	SDL_Rect Source, Destination;

	/* Sanity */
	if (Window == NULL)
		return;

	/* Copy window texture to render */
	if (DirtyArea == NULL
		|| (DirtyArea->x == Window->FullDimensions.x
			&& DirtyArea->y == Window->FullDimensions.y
			&& DirtyArea->w == Window->FullDimensions.w
			&& DirtyArea->h == Window->FullDimensions.h)) 
	{
		/* Set destination (just copy) */
		Destination.x = Window->FullDimensions.x;
		Destination.y = Window->FullDimensions.y;
		Destination.w = Window->FullDimensions.w;
		Destination.h = Window->FullDimensions.h;

		/* Full redraw */
		SDL_RenderCopyEx(Renderer, Window->Texture, NULL, &Destination,
			Window->Rotation, NULL, Window->Flip); 
	}
	else {

		/* Set source variables 
		 * We do this by adjusting the rectangle
		 * by the window position */
		Source.x = DirtyArea->x - Window->FullDimensions.x;
		Source.y = DirtyArea->y - Window->FullDimensions.y;
		Source.w = DirtyArea->w;
		Source.h = DirtyArea->h;

		/* Set destination (just copy) */
		Destination.x = DirtyArea->x;
		Destination.y = DirtyArea->y;
		Destination.w = DirtyArea->w;
		Destination.h = DirtyArea->h;

		/* Copy buffer */
		SDL_RenderCopyEx(Renderer, Window->Texture, &Source, &Destination,
			Window->Rotation, NULL, Window->Flip);
	}
}
