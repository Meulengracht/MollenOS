/**
 * Copyright 2023, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ds/mstring.h>
#include "screen.h"
#include <stdlib.h>
#include <string.h>

struct Rectangle {
    uint32_t Left, Top, Right, Bottom;
};

struct OutputFB {
    struct Output    Base;
    void*            Framebuffer;
    void*            Backbuffer;
    uint32_t         PositionX;
    uint32_t         PositionY;
    struct Rectangle ScreenSize;
    struct Rectangle Bounds;
    uint32_t         ColorBackground;
    uint32_t         ColorText;
    uint32_t         BytesPerScanline;
    uint32_t         Depth; // Bytes

    VideoColorComponent_t Red;
    VideoColorComponent_t Green;
    VideoColorComponent_t Blue;
    VideoColorComponent_t Alpha;
};

extern const uint32_t g_fontHeight;
extern const uint32_t g_fontWidth;
extern const uint32_t g_fontCharCount;
extern const uint8_t  g_font[];
extern const uint16_t g_fontIndices[];

static void __fb_print(struct Output*, const char*);
static void __fb_clear(struct Output*, uint8_t, uint8_t, uint8_t);
static void __fb_flush(struct Output*);
static void __fb_inhibit(struct Output*);
static void __fb_uninhibit(struct Output*);
static struct OutputOperations g_fbOps = {
        .Print = __fb_print,
        .Clear = __fb_clear,
        .Flush = __fb_flush,
        .Inhibit = __fb_inhibit,
        .Uninhibit = __fb_uninhibit
};

static uint32_t
__CreateColor(
        _In_ struct OutputFB* fb,
        _In_ uint8_t          r,
        _In_ uint8_t          g,
        _In_ uint8_t          b,
        _In_ uint8_t          a)
{
    uint32_t color =
            ((uint32_t)r << fb->Red.Position) |
            ((uint32_t)g << fb->Green.Position) |
            ((uint32_t)b << fb->Blue.Position);
    if (fb->Depth == 4) {
        return color | ((uint32_t)a << fb->Alpha.Position);
    }
    return color;
}

struct Output*
ScreenCreateFB(
        _In_ OSBootVideoDescriptor_t* video)
{
    struct OutputFB* fb;
    void*            bb;
    size_t           size;

    if (video->Type != OSBOOTVIDEO_FRAMEBUFFER) {
        return NULL;
    }

    // Calculate and allocate a backbuffer first, it's easier to cleanup
    // again should anything fail
    size = video->BytesPerScanline * video->Height;
    bb = malloc(size);
    if (bb == NULL) {
        return NULL;
    }

    fb = malloc(sizeof(struct OutputFB));
    if (fb == NULL) {
        free(bb);
        return NULL;
    }
    memset(fb, 0, sizeof(struct OutputFB));

    fb->Framebuffer = CreateDisplayFramebuffer();
    if (fb == NULL) {
        free(bb);
        free(fb);
        return NULL;
    }
    fb->Backbuffer = bb;
    fb->Base.Ops = &g_fbOps;
    return &fb->Base;
}

static void
__RenderCharacter(
        _In_ struct OutputFB* output,
        _In_ uint32_t         cp)
{
    uint32_t*    fbPointer;
    uint32_t*    bbPointer;
    uint8_t*     charData;
    size_t       offset;
    unsigned int row, i;

    // Calculate the video-offset
    offset    = (output->PositionY * output->BytesPerScanline) + (output->PositionX * output->Depth);
    fbPointer = (uint32_t*)((uint8_t*)output->Framebuffer + offset);
    bbPointer = (uint32_t*)((uint8_t*)output->Backbuffer + offset);

    // TODO cache these entries
    for (i = 0; i < g_fontCharCount; i++) {
        if (g_fontIndices[i] == (uint16_t)cp) {
            break;
        }
    }
    if (i == g_fontCharCount) {
        return;
    }

    charData = (uint8_t*)&g_fontIndices[i * g_fontHeight];
    for (row = 0; row < g_fontHeight; row++) {
        uint8_t bmpData = charData[row];
        for (i = 0; i < 8; i++) {
            bbPointer[i] = fbPointer[i] =
                    (bmpData >> (7 - i)) & 0x1 ? (0xFF000000 | output->ColorText) : (0xFF000000 | output->ColorBackground);
        }
        fbPointer = (uint32_t*)((uint8_t*)fbPointer + output->BytesPerScanline);
        bbPointer = (uint32_t*)((uint8_t*)bbPointer + output->BytesPerScanline);
    }
}

static void
__Scroll(
        _In_ struct OutputFB* output,
        _In_ int              lineCount)
{
    uint8_t* bb = (uint8_t*)output->Backbuffer;
    uint8_t* videoPointer;
    size_t   bytesToCopy;
    int      lines;
    int      i, j;

    // How many lines do we need to modify?
    lines = (int)(output->Bounds.Bottom - output->Bounds.Top);

    // Calculate the initial screen position
    videoPointer = bb
            + (output->Bounds.Top * output->BytesPerScanline)
            + (output->Bounds.Left * output->Depth);
    bytesToCopy = ((output->Bounds.Right - output->Bounds.Left) * output->Depth);
    for (i = 0; i < lineCount; i++) {
        for (j = 0; j < lines; j++) {
            memcpy(
                    videoPointer,
                    videoPointer + (output->BytesPerScanline * g_fontHeight),
                    bytesToCopy
            );
            videoPointer += output->BytesPerScanline;
        }
    }

    // Clear out the lines that was scrolled
    // Scroll pointer down to bottom - n lines
    videoPointer = bb + (output->Bounds.Left * output->Depth);
    videoPointer += output->BytesPerScanline * (output->Bounds.Bottom - (g_fontHeight * lineCount));
    for (i = 0; i < ((int)g_fontHeight * lineCount); i++) {
        memset(videoPointer, 0xFF, bytesToCopy);
        videoPointer += output->BytesPerScanline;
    }

    // We did the scroll, modify cursor
    output->PositionY -= (g_fontHeight * lineCount);
}

static void
__PrintCharacter(
        _In_ struct OutputFB* output,
        _In_ uint32_t         cp)
{
    switch (cp) {
        // New-Line
        // Reset X and increase Y
        case U'\n': {
            output->PositionX = output->Bounds.Left;
            output->PositionY += g_fontHeight;
        } break;

        // Carriage Return
        // Reset X don't increase Y
        case U'\r': {
            output->PositionX = output->Bounds.Left;
        } break;

        // Default
        // Printable character
        default: {
        // Call print with the current location
        // and use the current colors
            __RenderCharacter(output, cp);
            output->PositionX += (g_fontWidth + 1);
        } break;
    }

    // Next step is to do some post-print
    // checks, including new-line and scroll-checks

    // Are we at last X position? - New-line
    if ((output->PositionX + (g_fontWidth + 1)) >= output->Bounds.Right) {
        output->PositionX = output->Bounds.Left;
        output->PositionY += g_fontHeight;
    }

    // Do we need to scroll the terminal?
    if ((output->PositionY + g_fontHeight) >= output->Bounds.Bottom) {
        __Scroll(output, 1);
    }
}

static void
__fb_print(
        _In_ struct Output* output,
        _In_ const char*    string)
{
    struct OutputFB* fb    = (struct OutputFB*)output;
    size_t           count = mstr_len_u8(string);
    int              idx   = 0;

    for (size_t i = 0; i < count; i++) {
        uint32_t cp = mstr_next(string, &idx);
        __PrintCharacter(fb, cp);
    }
}

static void
__fb_flush(
        _In_ struct Output* output)
{
    struct OutputFB* fb = (struct OutputFB*)output;
    size_t byteCount = fb->BytesPerScanline * fb->ScreenSize.Bottom;
    memcpy(fb->Framebuffer, fb->Backbuffer, byteCount);
}

static inline void
memset32(uint32_t* out, uint32_t value, size_t byteCount)
{
    size_t count = byteCount >> 2;
    while (count) {
        *(out++) = value;
        count--;
    }
}

static void
__fb_clear(
        _In_ struct Output* output,
        _In_ uint8_t        r,
        _In_ uint8_t        g,
        _In_ uint8_t        b)
{
    struct OutputFB* fb        = (struct OutputFB*)output;
    uint32_t         color     = __CreateColor(fb, r, g, b, 0xFF);
    size_t           byteCount = fb->BytesPerScanline * fb->ScreenSize.Bottom;
    memset32(fb->Backbuffer, color, byteCount);
    memset32(fb->Framebuffer, color, byteCount);
}

static void
__fb_inhibit(
        _In_ struct Output* output)
{
    struct OutputFB* fb = (struct OutputFB*)output;
    _CRT_UNUSED(fb);
}

static void
__fb_uninhibit(
        _In_ struct Output* output)
{
    struct OutputFB* fb = (struct OutputFB*)output;
    _CRT_UNUSED(fb);
}
