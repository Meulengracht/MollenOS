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

#include <ds/ds.h>
#include <ds/guid.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char* g_hex         = "0123456789ABCDEF";
static uint8_t     g_guidmap[16] = { 6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34 };

void __generate_bytes(uint8_t* out, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}

void __generate_guid(guid_t* guid)
{
    // initialize random generator
    // @todo truly random implementation
    srand(clock());
    __generate_bytes((uint8_t*)&guid->data0, 4);
    __generate_bytes((uint8_t*)&guid->data1, 2);
    __generate_bytes((uint8_t*)&guid->data2, 2);
    __generate_bytes(&guid->data3[0], 8);
}

void __set_version_4(guid_t* guid)
{
    guid->data2 = (guid->data2 & 0x0FFF) | 0x4000;
}

void __set_variant_dce(guid_t* guid)
{
    guid->data3[0] = (guid->data3[0] & 0x3F) | 0x80;
}

void guid_new(guid_t* guid)
{
    __generate_guid(guid);
    __set_version_4(guid);
    __set_variant_dce(guid);
}

void guid_parse_raw(guid_t* guid, const uint8_t* data)
{
    guid->data0 = ((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
    guid->data1 = ((data[5] << 8) | data[4]);
    guid->data2 = ((data[7] << 8) | data[6]);
    for (int i = 0; i < 8; i++) {
        guid->data3[i] = data[8 + i];
    }
}

static int __validate_guid(const char* string)
{
    size_t len = strlen(string);
    if (len != 36) {
        return -1;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = string[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return -1;
            }
        }
        else if (!((c >= '0') && (c <= '9')) && !((c >= 'A') && (c <= 'F')) && !((c >= 'a') && (c <= 'f'))) {
            return -1;
        }
    }
    return 0;
}

uint8_t __char_to_hex(char c)
{
    if (c <= '9') {
        return ((uint8_t) (c - '0'));
    }
    if (c <= 'F') {
        return ((uint8_t) (c - 0x37));
    }
    return ((uint8_t) (c - 0x57));
}

void __string_to_hex(size_t gi, uint8_t* out, const char* string, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t m = g_guidmap[gi + i];
        out[i] = (__char_to_hex(string[m]) << 4) | __char_to_hex(string[m + 1]);
    }
}

void guid_parse_string(guid_t* guid, const char* string)
{
    if (__validate_guid(string)) {
        return;
    }

    __string_to_hex(0, (uint8_t*)&guid->data0, string, 4);
    __string_to_hex(4, (uint8_t*)&guid->data1, string, 2);
    __string_to_hex(6, (uint8_t*)&guid->data2, string, 2);
    __string_to_hex(8, &guid->data3[0], string, 2);
    __string_to_hex(10, &guid->data3[2], string, 6);
}

int guid_cmp(guid_t* lh, guid_t* rh)
{
    if (lh->data0 == rh->data0 && lh->data1 == rh->data1 &&
        lh->data2 == rh->data2) {
        return memcmp(&lh->data3[0], &rh->data3[0], 8);
    }
    return -1;
}

void guid_format(guid_t* guid, char* buffer, size_t len)
{
    if (len < 37) {
        return;
    }

    // first 8
    buffer[0] = g_hex[(guid->data0 >> 28) & 0xF];
    buffer[1] = g_hex[(guid->data0 >> 24) & 0xF];
    buffer[2] = g_hex[(guid->data0 >> 20) & 0xF];
    buffer[3] = g_hex[(guid->data0 >> 16) & 0xF];
    buffer[4] = g_hex[(guid->data0 >> 12) & 0xF];
    buffer[5] = g_hex[(guid->data0 >> 8) & 0xF];
    buffer[6] = g_hex[(guid->data0 >> 4) & 0xF];
    buffer[7] = g_hex[guid->data0 & 0xF];
    buffer[8] = '-';

    // next 4
    buffer[9]  = g_hex[(guid->data1 >> 12) & 0xF];
    buffer[10] = g_hex[(guid->data1 >> 8) & 0xF];
    buffer[11] = g_hex[(guid->data1 >> 4) & 0xF];
    buffer[12] = g_hex[guid->data1 & 0xF];
    buffer[13] = '-';

    // next 4
    buffer[14] = g_hex[(guid->data2 >> 12) & 0xF];
    buffer[15] = g_hex[(guid->data2 >> 8) & 0xF];
    buffer[16] = g_hex[(guid->data2 >> 4) & 0xF];
    buffer[17] = g_hex[guid->data2 & 0xF];
    buffer[18] = '-';

    // next 4
    buffer[19] = g_hex[(guid->data3[0] >> 4) & 0xF];
    buffer[20] = g_hex[guid->data3[0] & 0xF];
    buffer[21] = g_hex[(guid->data3[1] >> 4) & 0xF];
    buffer[22] = g_hex[guid->data3[1] & 0xF];
    buffer[23] = '-';

    // last 6
    buffer[24] = g_hex[(guid->data3[2] >> 4) & 0xF];
    buffer[25] = g_hex[guid->data3[2] & 0xF];
    buffer[26] = g_hex[(guid->data3[3] >> 4) & 0xF];
    buffer[27] = g_hex[guid->data3[3] & 0xF];
    buffer[28] = g_hex[(guid->data3[4] >> 4) & 0xF];
    buffer[29] = g_hex[guid->data3[4] & 0xF];
    buffer[30] = g_hex[(guid->data3[5] >> 4) & 0xF];
    buffer[31] = g_hex[guid->data3[5] & 0xF];
    buffer[32] = g_hex[(guid->data3[6] >> 4) & 0xF];
    buffer[33] = g_hex[guid->data3[6] & 0xF];
    buffer[34] = g_hex[(guid->data3[7] >> 4) & 0xF];
    buffer[35] = g_hex[guid->data3[7] & 0xF];
    buffer[36] = 0;
}
