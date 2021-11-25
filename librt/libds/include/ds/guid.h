/**
 * MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * GUID Implementation
 *  - Implements GUID utilities
 */

#ifndef __DS_GUID_H__
#define __DS_GUID_H__

#include <ds/dsdefs.h>
#include <ds/shared.h>

typedef struct guid {
    uint32_t data0;
    uint16_t data1;
    uint16_t data2;
    uint8_t  data3[8];
} guid_t;

#define GUID_EMPTY { 0, 0, 0, { 0 } };

_CODE_BEGIN

DSDECL(void, guid_new(guid_t*));
DSDECL(void, guid_parse_raw(guid_t*, const uint8_t*));
DSDECL(void, guid_parse_string(guid_t*, const char* string));
DSDECL(int,  guid_cmp(guid_t*, guid_t*));
DSDECL(void, guid_format(guid_t*, char* buffer, size_t len));

_CODE_END

#endif //!__DS_GUID_H__
