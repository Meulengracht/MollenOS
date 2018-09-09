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
* MollenOS Terminal Implementation
* - Project Alumnious (First C++ Project)
*/

#include <ds/mstring.h>
#include <cassert>
#include <cctype>
#include <cmath>
#include "terminal.hpp"

/* ZERO WIDTH NO-BREAKSPACE (Unicode byte order mark) */
#define UNICODE_BOM_NATIVE  0xFEFF
#define UNICODE_BOM_SWAPPED 0xFFFE

CTerminal::CTerminal(CSurface& Surface, , uint8_t BgR, uint8_t BgG, uint8_t m_BgB)
    : m_Surface(Surface), m_Font(nullptr)
{
    bool Status;

    m_CmdStart[0] = '$';
    m_CmdStart[1] = ' ';
    m_CmdStart[2] = '\0';
    Status = FT_Init_FreeType(&m_FreeType) == 0;
    assert(Status);

    m_BgR = BgR;
    m_BgG = BgG;
    m_BgB = BgB;

    /* Initialize lines */
    m_CurrentLine = (char*)malloc(1024 * sizeof(char));
    m_CurrentLine[0] = '\0';
    m_LineStartX = 0;
    m_LineStartY = 0;
    m_LinePos = 0;

    /* History allocation */
    m_HistorySize = 25;
    m_HistoryIndex = m_iHistorySize - 1;
    m_History = (char**)malloc(m_iHistorySize * sizeof(char*));

    /* Reset history */
    for (int i = 0; i < m_iHistorySize; i++)
        m_pHistory[i] = NULL;

    /* Text buffer Allocation */
    m_BufferSize = 250;
    m_Buffer = (char**)malloc(m_iBufferSize * sizeof(char*));

    /* Reset buffer */
    for (int i = 0; i < m_iBufferSize; i++)
        m_pBuffer[i] = NULL;
    m_Surface.Clear(m_Surface.GetColor(BgR, BgG, BgB, 255));
}

Terminal::~Terminal()
{
    free(m_CurrentLine);
    free(m_History);
    free(m_Buffer);

    if (m_Font != nullptr) {
        delete m_Font;
    }

    if (m_FreeType != NULL) {
        FT_Done_FreeType(m_FreeType);
    }
}

/* New Command - Prints the command
 * character and preps for new input */
void Terminal::NewCommand()
{
    /* Keep track */
    bool Reading = true;

    /* Resolve current path */
    EnvironmentResolve(PathCurrentWorkingDir, m_pCurrentLine);

    /* Print new command */
    PrintLine("[%s | %s | 09/12/2016 - 13:00]\n", m_pCurrentLine, "Philip");
    PrintLine("$ ");

    /* Empty */
    m_pCurrentLine[0] = '\0';

    /* Print a new command */
    while (Reading) {

        /* Read a new character from input */
        VKey Character = (VKey)getchar();

        /* Switch */ 
        if (Character == VK_BACK) {
            if ((m_iLinePos > 0) && (m_pCurrentLine[m_iLinePos - 1] == '\t')) {
                if (!RemoveChar(m_iLinePos - 1)) {
                    HideCursor();
                    m_iCursorPositionX -= 4;

                    /* Sanity check boundary on y */
                    if (m_iCursorPositionX < 0) {
                        m_iCursorPositionX += m_iColumns;
                        m_iCursorPositionY--;
                    }

                    /* If we were in the middle of the line, 
                     * we have to render the rest of the line */
                    if (m_iLinePos < strlen(m_pCurrentLine)) 
                    {
                        /* Save current cursor */
                        int SavedX = m_iCursorPositionX, SavedY = m_iCursorPositionY;

                        for (int i = m_iLinePos; i<strlen(m_pCurrentLine); i++)
                            AddCharacter(m_pCurrentLine[i]); //AddChar
                        for (int i = 0; i < 4; i++)
                            AddCharacter(' '); //AddChar

                        /* Restore position */
                        m_iCursorPositionX = SavedX;
                        m_iCursorPositionY = SavedY;
                    }

                    /* Render Cursor */
                    ShowCursor();
                }
            }
            else if (RemoveChar(m_iLinePos - 1) == 0) {
                
                HideCursor();
                
                if (m_iCursorPositionX > 0)
                    m_iCursorPositionX--;
                else {
                    m_iCursorPositionX = m_iColumns - 1;
                    m_iCursorPositionY--;
                }

                /* If we were in the middle of the line, we have to render the ret of the line */
                if (m_iLinePos < strlen(m_pCurrentLine)) {
                    
                    /* Save current cursor */
                    int SavedX = m_iCursorPositionX, SavedY = m_iCursorPositionY;

                    /* Add the characters */
                    for (int i = m_iLinePos; i<strlen(m_pCurrentLine); i++)
                        AddCharacter(m_pCurrentLine[i]);
                    AddCharacter(' ');
                    
                    /* Restore position */
                    m_iCursorPositionX = SavedX;
                    m_iCursorPositionY = SavedY;
                }
                
                /* Render Cursor */
                ShowCursor();
            }
        }
        else if (Character == VK_ENTER) {
            HideCursor();
            NewLine();
            break;
        }
        else if (Character == VK_UP) {
            HistoryPrevious();
        }
        else if (Character == VK_DOWN) {
            HistoryNext();
        }
        else if (Character == VK_LEFT) {
            if (m_iLinePos > 0) {
                HideCursor();
                m_iLinePos--;
                int dx = 1;
                if (m_pCurrentLine[m_iLinePos] == '\t')
                    dx = 4;
                m_iCursorPositionX -= dx;
                if (m_iCursorPositionX < 0) {
                    m_iCursorPositionX += m_iColumns;
                    m_iCursorPositionY--;
                }
                
                /* Render Cursor */
                ShowCursor();
            }
        }
        else if (Character == VK_RIGHT) {
            if (m_iLinePos < strlen(m_pCurrentLine)) {
                HideCursor();
                int dx = +1;
                if ((m_iLinePos < strlen(m_pCurrentLine)) && m_pCurrentLine[m_iLinePos] == '\t')
                    dx = 4;
                m_iCursorPositionX += dx;
                if (m_iCursorPositionX >= m_iColumns) {
                    m_iCursorPositionX -= m_iColumns;
                    m_iCursorPositionY++;
                }
                m_iLinePos++;
                
                /* Render Cursor */
                ShowCursor();
            }
        }
        else
        {
            /* ASCII ? */
            if (isalpha(Character)) {
                AddCharacter((char)Character);
            }
        }
    }
}

bool Terminal::SetHistorySize(int NumLines)
{
    return true;
}

/* Yank previous history line */
void Terminal::HistoryPrevious()
{
    /* Sanitize if enough history available */
    if ((m_iHistoryIndex <= 0) || (m_pHistory[m_iHistoryIndex - 1] == 0))
        return;

    /* Save current entry within history */
    if (m_iHistoryIndex == m_iHistorySize - 1) {
        strcpy(m_pHistory[m_iHistorySize - 1], m_pCurrentLine);
    }

    /* Copy history to entry */
    m_iHistoryIndex--;
    strcpy(m_pCurrentLine, m_pHistory[m_iHistoryIndex]);
    m_iLinePos = strlen(m_pCurrentLine);

    /* Refresh terminal */
    ClearFrom(m_iLineStartX, m_iLineStartY);
    m_iCursorPositionX = m_iLineStartX;
    m_iCursorPositionY = m_iLineStartY;
    AddText(m_pCurrentLine);
}

/* Yank next history line */
void Terminal::HistoryNext()
{
    /* Make sure we have enough room for next */
    if (m_iHistoryIndex >= m_iHistorySize - 1)
        return;

    /* Copy history to entry */
    m_iHistoryIndex++;
    strcpy(m_pCurrentLine, m_pHistory[m_iHistoryIndex]);
    m_iLinePos = strlen(m_pCurrentLine);

    /* Refresh terminal */
    ClearFrom(m_iLineStartX, m_iLineStartY);
    m_iCursorPositionX = m_iLineStartX;
    m_iCursorPositionY = m_iLineStartY;
    AddText(m_pCurrentLine);
}

void Terminal::SetFont(const std::string& FontPath, std::size_t PixelSize)
{
    if (m_Font != nullptr) {
        delete m_Font;
    }
    m_Font = new CTerminalFont(m_FreeType, FontPath, PixelSize);
    m_Font->SetColors(m_Surface.GetColor(255, 255, 255, 255), m_Surface.GetColor(m_BgR, m_BgG, m_BgB, 255));
}

void Terminal::SetTextColor(uint8_t r, uint8_t b, uint8_t g, uint8_t a)
{
    m_Font->SetColors(m_Surface.GetColor(r, g, b, a), m_Surface.GetColor(m_BgR, m_BgG, m_BgB, 255));
}

/* Print raw messages to the terminal,
 * this could be a header or a warning message */
void Terminal::PrintLine(const char *Message, ...)
{
    /* VA */
    va_list Args;
    char *Line = (char*)malloc(1024 * sizeof(char));

    /* Combine into buffer */
    va_start(Args, Message);
    vsnprintf(Line, 1024 - 1, Message, Args);
    va_end(Args);

    /* Now actually add the text */
    AddTextBuffer(Line);
    AddText(Line);

    /* Update line start coords */
    m_iLineStartX = m_iCursorPositionX;
    m_iLineStartY = m_iCursorPositionY;

    /* Cleanup line */
    free(Line);
}

/* This actually renders the text in the end
 * by processng it, and scrolls if necessary */
void Terminal::AddText(char *Message, ...)
{
    /* Variables need for formatting
     * Use static so we don't allocate 1kb on stack */
    static char Line[1024];
    va_list Args;
    int i = 0;

    /* Sanitize params */
    if (Message == NULL) {
        return;
    }

    /* Clear out buffer */
    memset(Line, 0, sizeof(Line));

    /* Combine */
    va_start(Args, Message);
    vsnprintf(Line, 1024 - 1, Message, Args);
    va_end(Args);

    /* Iterate characters */
    while (Line[i]) 
    {
        /* Extract character 
         * and keep a copy of the char */
        int c = Line[i++];
        int cc = c;
        int dx = 1;

        /* Newline? */
        if (c == '\n') {
            m_iCursorPositionX = 0;
            m_iCursorPositionY++;

            /* Boundary check, maybe scroll */
            if (m_iCursorPositionY >= m_iRows) {
                ScrollText(1);
                m_iCursorPositionY--;
            }

            /* Done, go to next */
            continue;
        }

        /* Taburator, override in that case
         * since \t is not printable */
        if (c == '\t') {
            cc = ' ';
            dx = 4;
        }

        /* Iterate how many chars we need to output */
        for (int j = 0; j < dx; j++)  {
            AddCharacter(cc);
        }
    }

    /* Invalidate surface */
    m_pSurface->Invalidate(0, 0, -1, -1);
}

/* Shorthand for the above function, instead of 
 * adding an entire message, just add a single character */
void Terminal::AddCharacter(char Character)
{
    /* Buffer */
    char cBuffer[] = " ";

    /* Update */
    cBuffer[0] = Character;

    /* Render the character */
    RenderText(m_iCursorPositionX * m_iFontWidth,
        m_iCursorPositionY * m_iFontHeight, cBuffer);
    m_iCursorPositionX++;

    /* Boundary check on width */
    if (m_iCursorPositionX >= m_iColumns) {
        m_iCursorPositionX -= m_iColumns;
        m_iCursorPositionY++;

        /* Boundary check, maybe scroll */
        if (m_iCursorPositionY >= m_iRows) {
            ScrollText(1);
            m_iCursorPositionY--;
        }
    }
}

/* Add text to the buffer, this ensures
 * we can transfer it to history afterwards */
void Terminal::AddTextBuffer(char *Message, ...)
{
    /* VA */
    va_list Args;

    /* Sanitize params */
    if (Message == NULL)
        return;

    /* Sanitize the buffer first index, 
     * we need the space */
    if (m_pBuffer[0] != NULL)
        free(m_pBuffer[0]);

    /* Iterate and move the buffers */
    for (int i = 1; i < m_iBufferSize; i++)
        m_pBuffer[i - 1] = m_pBuffer[i];

    /* Allocate a new buffer */
    m_pBuffer[m_iBufferSize - 1] = (char*)malloc(1024 * sizeof(char));
    if (m_pBuffer[m_iBufferSize - 1] == NULL) {
        /* Out of memory probably */
        return;
    }

    /* Combine it into buffer */
    va_start(Args, Message);
    vsnprintf(m_pBuffer[m_iBufferSize - 1], 1024 - 1, Message, Args);
    va_end(Args);

    /* Null terminate the new string */
    m_pBuffer[m_iBufferSize - 1][1024 - 1] = '\0';
}

/* Process a new line by updating history */
void Terminal::NewLine()
{
    /* Store a copy within history
    do not store blank lines
    do not store line if it is just before in history
    */
    if ((strcmp(m_pCurrentLine, "") != 0) &&
        ((m_pHistory[m_iHistorySize - 2] == 0) ||
        (strcmp(m_pCurrentLine, m_pHistory[m_iHistorySize - 2]) != 0)))
    {
        /* Variables */
        int i;

        /* Sanitize the first member of history */
        if (m_pHistory[0])
            free(m_pHistory[0]);

        /* Allocate a new history entry if 
         * it isn't already allocated */
        if (!m_pHistory[m_iHistorySize - 1]) {
            m_pHistory[m_iHistorySize - 1] = (char*)malloc(1024 * sizeof(char));
            if (m_pHistory[m_iHistorySize - 1] == NULL) {
                /* Out of memory */
                return;
            }
        }

        /* Move history up */
        strcpy(m_pHistory[m_iHistorySize - 1], m_pCurrentLine);
        for (i = 1; i< m_iHistorySize; i++)
            m_pHistory[i - 1] = m_pHistory[i];

        /* Allocate a history entry */
        m_pHistory[m_iHistorySize - 1] = (char*)malloc(1024 * sizeof(char));
        if (m_pHistory[m_iHistorySize - 1] == NULL) {
            /* Out of memory */
            return;
        }

        /* Null terminate history */
        m_pHistory[0] = '\0';
        m_iHistoryIndex = m_iHistorySize - 1;
    }


    /* Update cursor position */
    m_iCursorPositionX = 0;
    m_iCursorPositionY++;

    /* Boundary Check */
    if (m_iCursorPositionY >= m_iRows) {
        ScrollText(1);
        m_iCursorPositionY--;
    }

    /* Add trailing '\n' to go to next line */
    InsertChar('\n');

    /* Update text buffer (with '\n') */
    AddTextBuffer(m_pCurrentLine);

    /* Misc. updates */
    m_pCurrentLine[0] = '\0';
    m_iLinePos = 0;
    m_iLineStartX = m_iCursorPositionX;
    m_iLineStartY = m_iCursorPositionY;
}

/* 'Render' the cursor at our current position 
 * in it's normal colors, this will effectively hide it */
void Terminal::HideCursor()
{
    /* Variables */
    char cBuffer[] = " ";
    char c = ' ';

    /* Determine where to put the cursor in case
     * it's to far */
    if (m_iLinePos < strlen(m_pCurrentLine)) {
        c = m_pCurrentLine[m_iLinePos];
        if (c == '\t')
            c = ' ';
    }

    /* Update buffer */
    cBuffer[0] = c;

    /* 'Render' the cursor */
    RenderText(m_iCursorPositionX * m_iFontWidth,
        m_iCursorPositionY * m_iFontHeight, cBuffer);
}

/* Render the cursor in reverse colors, this will give the
 * effect of a big fat block that acts as cursor */
void Terminal::ShowCursor()
{
    /* Get foreground and bg colors */
    char cBuffer[] = " ";
    char c = ' ';
    int r = 0;

    /* Store colors */
    uint8_t BgR = m_cBgR; uint8_t BgG = m_cBgG;
    uint8_t BgB = m_cBgB; uint8_t BgA = m_cBgA;

    uint8_t FgR = m_cFgR; uint8_t FgG = m_cFgG;
    uint8_t FgB = m_cFgB; uint8_t FgA = m_cFgA;

    /* Setup cursor colors, by swapping fg/bg */
    m_cFgR = BgR; m_cFgG = BgG; m_cFgB = BgB; m_cFgA = BgA;
    m_cBgR = FgR; m_cBgG = FgG; m_cBgB = FgB; m_cBgA = FgA;

    /* Where should we render it? */
    if (m_iLinePos < strlen(m_pCurrentLine)) {
        c = m_pCurrentLine[m_iLinePos];
        if (c == '\t')
            c = ' ';
    }

    /* Update buffer */
    cBuffer[0] = c;

    /* Render the cursor */
    RenderText(m_iCursorPositionX * m_iFontWidth,
        m_iCursorPositionY * m_iFontHeight, cBuffer);

    /* Restore colors */
    m_cFgR = FgR; m_cFgG = FgG; m_cFgB = FgB; m_cFgA = FgA;
    m_cBgR = BgR; m_cBgG = BgG; m_cBgB = BgB; m_cBgA = BgA;
}

/* Add given character to current line */
void Terminal::InsertChar(char Character)
{
    /* Sanitize length */
    if (strlen(m_pCurrentLine) >= (1024 - 1))
        return;

    /* Move the characters one space */
    memmove(&m_pCurrentLine[m_iLinePos + 1],
        &m_pCurrentLine[m_iLinePos],
        strlen(m_pCurrentLine) - m_iLinePos + 1);

    /* Insert the new character */
    m_pCurrentLine[m_iLinePos++] = Character;
}

/* Delete character of current line at current pos */
int Terminal::RemoveChar(int Position)
{
    /* Sanitize some params and the current line */
    if (Position < 0 
        || strlen(m_pCurrentLine) == 0)
        return -1;

    /* Move the line one space and simoultanously override the
     * character spot we want to delete */
    memmove(&m_pCurrentLine[Position], &m_pCurrentLine[Position + 1],
        strlen(m_pCurrentLine) - Position + 1);

    /* Reduce line position */
    m_iLinePos--;

    /* Done! */
    return 0;
}

/* Clear out lines from the given col/row 
 * so it is ready for new data */
void Terminal::ClearFrom(int Column, int Row)
{
    /* Make sure columns are valid */
    if ((Column <= (m_iColumns - 1)) && (Row <= (m_iRows - 1))) 
    {
        /* Create dest area */
        Rect_t Destination = {
            Column * m_iFontWidth,
            Row * m_iFontHeight,
            (m_iRows - Row) * m_iFontHeight,
            (m_iColumns - Column) * m_iFontWidth
        };

        /* Clear */
        m_pSurface->Clear(m_pSurface->GetColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA), &Destination);
    }

    /* Make sure row is valid */
    if (Row < (m_iRows - 1)) 
    {
        /* Create dest area */
        Rect_t Destination = { 
            0, 
            (Row + 1) * m_iFontHeight,
            (m_iRows - Row - 1) * m_iFontHeight,
            m_iColumns * m_iFontWidth
        };

        /* Clear */
        m_pSurface->Clear(m_pSurface->GetColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA), &Destination);
    }
}

/* Scroll the terminal by a number of lines
 * and clear below the scrolled lines */
void Terminal::ScrollText(int Lines)
{
    /* Sanitize limits */
    if (Lines >= m_iRows)
        Lines = m_iRows;

    /* We do a memcpy */
    if (Lines < m_iRows) 
    {
        /* Calculate source pointer and how much to copy */
        uint8_t *SourcePtr = (uint8_t*)m_pSurface->DataPtr(0, Lines * m_iFontHeight);
        size_t BytesToCopy = m_pSurface->GetDimensions()->w * 
            (m_pSurface->GetDimensions()->h - Lines * m_iFontHeight) * 4;

        /* Calculate destination pointer */
        uint8_t *DestPtr = (uint8_t*)m_pSurface->DataPtr(0, 0);

        /* Copy */
        memcpy(DestPtr, SourcePtr, BytesToCopy);
    }

    /* Clear remaining lines */
    ClearFrom(0, m_iRows - Lines);
}

/* Text Rendering 
 * This is our primary render function, 
 * it renders text at a specific position on the buffer 
 * TODO ERASE */
void Terminal::RenderText(int AtX, int AtY, const char *Text)
{
    /* Variables for state tracking, 
     * and formatting, and rendering */
    bool First;
    int xStart;
    int Width;
    uint8_t *Source;
    uint32_t *Destination;
    uint32_t *DestCheck;
    int Row, Col;
    FontGlyph_t *Glyph;

    /* Iterating Text */
    MString_t *mText;
    size_t ItrLength = 0;
    char *mItr = NULL;

    /* Sanity */
    if (m_pActiveFont == NULL) {
        return;
    }

    /* Adding bound checking to avoid all kinds of memory corruption errors
     * that may occur. */
    DestCheck = (uint32_t*)m_pSurface->DataPtr() + 
        ((m_pSurface->GetDimensions()->w * 4) * m_pSurface->GetDimensions()->h);

    /* Initialise for the loop */
    mText = MStringCreate((void*)Text, StrUTF8);
    First = true;
    xStart = 0;

    /* Load and render each character */
    while (true) 
    {
        /* Get next character of text-string */
        uint16_t Character = (uint16_t)MStringGetCharAt(mText, ItrLength++);
        if (Character == UNICODE_BOM_NATIVE 
            || Character == UNICODE_BOM_SWAPPED) {
            continue;
        }

        /* End of string? */
        if (Character == MSTRING_EOS) {
            break;
        }

        /* Lookup glyph for the character, if we have none, 
         * we bail! */
        Error = FindGlyph(m_pActiveFont, Character, CACHED_METRICS | BitmapMode);
        if (Error) {
            break;
        }

        /* Shorthand some stuff */
        Glyph = m_pActiveFont->Current;

        /* Get the appropriate bitmap */
        if (BitmapMode & CACHED_BITMAP) {
            Current = &Glyph->Bitmap;
        }
        else {
            Current = &Glyph->Pixmap;
        }

        /* Ensure the width of the pixmap is correct. On some cases,
         * freetype may report a larger pixmap than possible.*/
        Width = Current->width;
        if (m_pActiveFont->Outline <= 0 && Width > Glyph->MaxX - Glyph->MinX) {
            Width = Glyph->MaxX - Glyph->MinX;
        }

        /* do kerning, if possible AC-Patch */
        if (UseKerning && PreviousIndex && Glyph->Index) {
            FT_Vector Delta;
            FT_Get_Kerning(m_pActiveFont->Face, PreviousIndex, Glyph->Index, FT_KERNING_DEFAULT, &Delta);
            xStart += Delta.x >> 6;
        }

        /* Compensate for wrap around bug with negative minx's */
        if (First && (Glyph->MinX < 0)) {
            xStart -= Glyph->MinX;
        }

        /* No longer the first! */
        First = false;

        /* Iterate over rows and actually draw it */
        for (Row = 0; Row < Current->rows; Row++) {
            /* Make sure we don't go either over, or under the
             * limit */
            if ((Row + Glyph->yOffset) < 0
                || Row + Glyph->yOffset >= m_pSurface->GetDimensions()->h) {
                continue;
            }
            
            /* Calculate destination */
            Destination = (uint32_t*)m_pSurface->DataPtr(
                AtX + xStart + Glyph->MinX, AtY + Row + Glyph->yOffset);
            Source = Current->buffer + (Row * Current->pitch);

            /* Loop ! */
            for (Col = Width; Col > 0 && Destination < DestCheck; --Col) {
                uint8_t Alpha = *Source++;
                if (Alpha == 0) {
                    *Destination++ = m_pSurface->GetColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA);
                }
                else {
                    if (BitmapMode & CACHED_BITMAP) {
                        *Destination++ = m_pSurface->GetColor(m_cFgR, m_cFgG, m_cFgB, m_cFgA);
                    }
                    else {
                        *Destination++ = m_pSurface->GetBlendedColor(m_cBgR, m_cBgG, m_cBgB, m_cBgA, 
                            m_cFgR, m_cFgG, m_cFgB, m_cFgA, Alpha);
                    }
                }
            }
        }

        /* Advance, and handle bold style if neccassary */
        xStart += Glyph->Advance;
        if (TTF_HANDLE_STYLE_BOLD(m_pActiveFont)) {
            xStart += m_pActiveFont->GlyphOverhang;
        }

        /* Set new previous index */
        PreviousIndex = Glyph->Index;
    }

    /* Cleanup */
    MStringDestroy(mText);
}
