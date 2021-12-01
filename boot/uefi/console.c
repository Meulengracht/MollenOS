/**
 * Copyright 2021, Philip Meulengracht
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
 *
 */

#include <console.h>
#include <library.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* gConsoleOut = NULL;
static BOOLEAN                          gEnabled = FALSE;

EFI_STATUS ConsoleInitialize(void)
{
    // Initialize the global variable
    gConsoleOut = gSystemTable->ConOut;
    if (!gConsoleOut)
        return EFI_UNSUPPORTED;

    // Clear screen
    gConsoleOut->ClearScreen(gConsoleOut);

    // Enable cursor for console
    gConsoleOut->EnableCursor(gConsoleOut, TRUE);

    // Initialize text color, white on black
    gConsoleOut->SetAttribute(gConsoleOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));

    // Initialize serial port output
    SerialPortInitialize();

    // Enable
    gEnabled = TRUE;

    return EFI_SUCCESS;
}

void ConsoleDisable(void)
{
    gEnabled = FALSE;
}

void ConsoleWrite(
    IN CHAR16* Format,
    IN         ...)
{
    UINT16  Buffer[512];
    VA_LIST Marker;
    UINTN   Length;

    if (gEnabled) {
        VA_START(Marker, Format);
        Length = UnicodeVSPrint(Buffer, sizeof(Buffer) - 1, Format, Marker);
        VA_END(Marker);

        Buffer[Length] = 0;
        gConsoleOut->OutputString(gConsoleOut, &Buffer[0]);
    }

    VA_START(Marker, Format);
    Length = AsciiVSPrintUnicodeFormat((CHAR8*)&Buffer[0], sizeof(Buffer) - 1, Format, Marker);
    VA_END(Marker);
    SerialPortWrite((UINT8*)&Buffer[0], Length);
}
