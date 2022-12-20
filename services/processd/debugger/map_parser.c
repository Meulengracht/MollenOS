/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 *
 * Process Manager - Map parser
 *  Contains the implementation of debugging facilities for the process manager which is
 *  invoked once a process crashes
 */

//#define __TRACE

#ifndef __TEST
#include <ddk/utils.h>
#endif

#include <stdlib.h>
#include <string.h>
#include "symbols.h"

static const char*
ParseSection(
        _In_  const char* location,
        _In_  const char* end,
        _Out_ char*       sectionOut);

static const char*
ParseHeader(
        _In_  const char* location,
        _In_  const char* end,
        _Out_ char*       headerOut);

static const char*
ParseSymbol(
        _In_ const char*        location,
        _In_ const char*        end,
        _In_ const char*        section,
        _In_ const char*        file,
        _In_ char*              symbolName,
        _In_ struct map_symbol* symbol);

static const char*
ParseHexValue(
        _In_  const char* location,
        _Out_ size_t*     valueOut);

static const char*
ParseDecimalValue(
        _In_  const char* location,
        _Out_ size_t*     valueOut);

static const char*
ParseString(
        _In_ const char* location,
        _In_ const char* end,
        _In_ char*       buffer,
        _In_ size_t      bufferLength);

static void
CalculateSpaceRequirements(
        _In_  void*   fileBuffer,
        _In_  size_t  fileLength,
        _Out_ size_t* sectionBytesOut,
        _Out_ size_t* fileBytesOut,
        _Out_ size_t* symbolBytesOut,
        _Out_ int*    symbolCountOut);

static void
LoadObjectsInMap(
        _In_ struct MapContext* mapContext,
        _In_ void*              fileBuffer,
        _In_ size_t             fileLength);

// Structure
// 1.       Address  Size     Align Out     In      Symbol
// Section. 00001000 01dc3906  4096 .text
// Header.  00001000 000000dc    16         src/gallium/targets/osmesa/target.o:(.text)
// Line.    00001000 00000000     0                 osmesa_create_screen
// Section. 01dc5000 007bad6d  4096 .rdata
oserr_t
SymbolParseMapFile(
        _In_ struct MapContext* mapContext,
        _In_ void*              fileBuffer,
        _In_ size_t             fileLength)
{
    size_t sectionBytes;
    size_t fileBytes;
    size_t symbolBytes;
    int    symbolCount;
    TRACE(STR("[SymbolParseMapFile]"));

    CalculateSpaceRequirements(
            fileBuffer,
            fileLength,
            &sectionBytes,
            &fileBytes,
            &symbolBytes,
            &symbolCount
    );

    TRACE(STR("[SymbolParseMapFile] sectionBytes 0x%" PRIxIN ", fileBytes 0x%" PRIxIN ", symbolBytes 0x%" PRIxIN ", symbolCount 0x%x"),
          sectionBytes, fileBytes, symbolBytes, symbolCount);

    if (!symbolCount) {
        return OS_EUNKNOWN;
    }

    mapContext->SectionStorage = (char*)malloc(sectionBytes);
    mapContext->FileStorage   = (char*)malloc(fileBytes);
    mapContext->SymbolStorage = (char*)malloc(symbolBytes);
    mapContext->Symbols        = (struct map_symbol*)malloc(sizeof(struct map_symbol) * symbolCount);
    if (!mapContext->SectionStorage || !mapContext->FileStorage ||
        !mapContext->SymbolStorage || !mapContext->Symbols) {
        MapContextDelete(mapContext);
        return OS_EOOM;
    }

    memset(mapContext->SectionStorage, 0, sectionBytes);
    memset(mapContext->FileStorage, 0, fileBytes);
    memset(mapContext->SymbolStorage, 0, symbolBytes);

    mapContext->SymbolCount = symbolCount;

    TRACE(STR("[SymbolParseMapFile] Loading objects.."));
    LoadObjectsInMap(mapContext, fileBuffer, fileLength);
    return OS_EOK;
}

void
MapContextDelete(
        _In_ struct MapContext* mapContext)
{
    if (mapContext == NULL) {
        return;
    }

    free(mapContext->SectionStorage);
    free(mapContext->FileStorage);
    free(mapContext->SymbolStorage);
    free(mapContext->Symbols);
}

static void
CalculateSpaceRequirements(
        _In_  void*   fileBuffer,
        _In_  size_t  fileLength,
        _Out_ size_t* sectionBytesOut,
        _Out_ size_t* fileBytesOut,
        _Out_ size_t* symbolBytesOut,
        _Out_ int*    symbolCountOut)
{
    const char* iterator     = (const char*)fileBuffer;
    const char* endOfFile    = (const char*)((uintptr_t)fileBuffer + fileLength);
    size_t      sectionBytes = 0;
    size_t      fileBytes    = 0;
    size_t      symbolBytes  = 0;
    int         symbolCount  = 0;
    char        buffer[_MAXPATH];

    // skip inital line
    iterator = (const char*)strchr(iterator, '\n');
    if (iterator) iterator++;

    while (iterator && iterator < endOfFile) {
        const char* parsed = ParseSymbol(iterator, endOfFile, NULL, NULL, &buffer[0], NULL);
        if (parsed) {
            iterator = parsed;
            symbolBytes += strlen(&buffer[0]) + 1; // include terminating null
            symbolCount++;
            continue;
        }

        parsed = ParseHeader(iterator, endOfFile, &buffer[0]);
        if (parsed) {
            iterator = parsed;
            fileBytes += strlen(&buffer[0]) + 1; // include terminating null
            continue;
        }

        parsed = ParseSection(iterator, endOfFile, &buffer[0]);
        if (parsed) {
            iterator = parsed;
            sectionBytes += strlen(&buffer[0]) + 1; // include terminating null
            continue;
        }

        iterator = (const char*)strchr(iterator, '\n');
        if (iterator) iterator++;
    }

    *sectionBytesOut = sectionBytes;
    *fileBytesOut    = fileBytes;
    *symbolBytesOut  = symbolBytes;
    *symbolCountOut  = symbolCount;
}

static void
LoadObjectsInMap(
        _In_ struct MapContext* mapContext,
        _In_ void*              fileBuffer,
        _In_ size_t             fileLength)
{
    const char* iterator        = (const char*)fileBuffer;
    const char* endOfFile       = (const char*)((uintptr_t)fileBuffer + fileLength);
    size_t      sectionIndex    = 0;
    size_t      headerIndex     = 0;
    size_t      symbolNameIndex = 0;
    int         symbolIndex     = 0;

    // skip inital line
    iterator = (const char*)strchr(iterator, '\n');
    if (iterator) iterator++;

    while (iterator && iterator < endOfFile) {
        const char* parsed = ParseSymbol(iterator, endOfFile,
                               &mapContext->SectionStorage[sectionIndex],
                               &mapContext->FileStorage[headerIndex],
                               &mapContext->SymbolStorage[symbolNameIndex],
                               &mapContext->Symbols[symbolIndex]);
        if (parsed) {
            iterator = parsed;
            symbolNameIndex += strlen(&mapContext->SymbolStorage[symbolNameIndex]) + 1;
            symbolIndex++;
            continue;
        }

        // try to parse as file / header
        {
            size_t lastLength = strlen(&mapContext->FileStorage[headerIndex]);
            size_t nextIndex  = headerIndex;
            if (lastLength) {
                nextIndex += lastLength + 1;
            }

            parsed = ParseHeader(iterator, endOfFile, &mapContext->FileStorage[nextIndex]);
            if (parsed) {
                iterator = parsed;
                headerIndex = nextIndex;
                continue;
            }
        }

        // try to parse as section
        {
            size_t lastLength = strlen(&mapContext->SectionStorage[sectionIndex]);
            size_t nextIndex  = sectionIndex;
            if (lastLength) {
                nextIndex += lastLength + 1;
            }

            parsed = ParseSection(iterator, endOfFile, &mapContext->SectionStorage[nextIndex]);
            if (parsed) {
                iterator = parsed;
                sectionIndex = nextIndex;
                continue;
            }
        }

        // OK line matched nothing, skip it
        iterator = (const char*)strchr(iterator, '\n');
        if (iterator) iterator++;
    }
}

// 00001000 01dc3906  4096 .text
static const char*
ParseSection(
        _In_  const char* location,
        _In_  const char* end,
        _Out_ char*       sectionOut)
{
    size_t      startAddress;
    size_t      length;
    size_t      pageSize;
    const char* itr;
    char        buffer[16] = { 0 };

    itr = ParseHexValue(location, &startAddress);
    itr = ParseHexValue(itr, &length);
    itr = ParseDecimalValue(itr, &pageSize);
    itr = ParseString(itr, end, &buffer[0], sizeof(buffer) - 1);
    if (!itr || length == 0 || pageSize < 512 || buffer[0] != '.') {
        return NULL;
    }

    strcpy(sectionOut, &buffer[0]);

    while (itr && (*itr == '\r' || *itr == '\n')) itr++;
    return itr;
}

// 00001000 000000dc    16         src/gallium/targets/osmesa/target.o:(.text)
static const char*
ParseHeader(
        _In_  const char* location,
        _In_  const char* end,
        _Out_ char*       headerOut)
{
    size_t      startAddress;
    size_t      length;
    size_t      alignment;
    const char* itr;
    char        buffer[_MAXPATH] = { 0 };

    itr = ParseHexValue(location, &startAddress);
    itr = ParseHexValue(itr, &length);
    itr = ParseDecimalValue(itr, &alignment);
    itr = ParseString(itr, end, &buffer[0], sizeof(buffer) - 1);
    if (!itr || length == 0 || alignment > 64 || !strchr(&buffer[0], ':')) {
        return NULL;
    }

    strcpy(headerOut, &buffer[0]);

    while (itr && (*itr == '\r' || *itr == '\n')) itr++;
    return itr;
}

// 00001000 00000000     0                 osmesa_create_screen
static const char*
ParseSymbol(
        _In_ const char*        location,
        _In_ const char*        end,
        _In_ const char*        section,
        _In_ const char*        file,
        _In_ char*              symbolName,
        _In_ struct map_symbol* symbol)
{
    size_t      startAddress;
    size_t      length;
    size_t      alignment;
    const char* itr;
    char        buffer[_MAXPATH] = { 0 };

    itr = ParseHexValue(location, &startAddress);
    itr = ParseHexValue(itr, &length);
    itr = ParseDecimalValue(itr, &alignment);
    itr = ParseString(itr, end, &buffer[0], sizeof(buffer) - 1);
    if (!itr || alignment >= 512 || strchr(&buffer[0], ':')) {
        return NULL;
    }

    strcpy(symbolName, &buffer[0]);
    if (symbol) {
        symbol->section = section;
        symbol->file    = file;
        symbol->name    = symbolName;
        symbol->address = startAddress;
        symbol->length  = length;
    }

    while (itr && itr != end && (*itr == '\r' || *itr == '\n')) itr++;
    return itr;
}

static const char*
ParseHexValue(
        _In_  const char* location,
        _Out_ size_t*     valueOut)
{
    char*  end = NULL;
    size_t value;

    if (!location) {
        return NULL;
    }

#if __BITS == 32
    value = strtoul(location, &end, 16);
#elif __BITS == 64
    value = strtoull(location, &end, 16);
#endif
    *valueOut = value;
    return end;
}

static const char*
ParseDecimalValue(
        _In_  const char* location,
        _Out_ size_t*     valueOut)
{
    char*  end = NULL;
    size_t value;

    if (!location) {
        return NULL;
    }

#if __BITS == 32
    value = strtoul(location, &end, 10);
#elif __BITS == 64
    value = strtoull(location, &end, 10);
#endif
    *valueOut = value;
    return end;
}

static const char*
ParseString(
        _In_ const char* location,
        _In_ const char* end,
        _In_ char*       buffer,
        _In_ size_t      bufferLength)
{
    const char* itr         = location;
    char*       destination = buffer;
    size_t      bytesLeft   = bufferLength;

    if (!itr) {
        return NULL;
    }

    // skip leading spaces
    while (itr && itr != end && *itr == ' ') itr++;

    // copy string
    while (itr && itr != end && *itr != ' ' && *itr != '\r' && *itr != '\n') {
        if (bytesLeft > 0) {
            *destination = *itr;
            destination++;
            bytesLeft--;
        }
        itr++;
    }

    // skip trailing spaces and newlines
    while (itr && itr != end && (*itr == ' ' || *itr == '\r' || *itr == '\n')) itr++;
    return itr;
}
