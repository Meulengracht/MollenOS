/*
 * Library: libcrc
 * File:    src/crc16.c
 * Author:  Lammert Bies
 *
 * This file is licensed under the MIT License as stated below
 *
 * Copyright (c) 1999-2016 Lammert Bies
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Description
 * -----------
 * The source file src/crc16.c contains routines which calculate the common
 * CRC16 cyclic redundancy check values for an incomming byte string.
 */

#include "include/gracht/crc.h"

#define    CRC_POLY_16      0xA001
#define    CRC_START_16     0x0000
#define    CRC_START_MODBUS 0xFFFF

#define    CRC_POLY_32      0xEDB88320ul
#define    CRC_START_32     0xFFFFFFFFul

static void crc16_initialize(void);

static void crc32_initialize(void);

static int      crc_tab16_init = 0;
static uint16_t crc_tab16[256];

static int      crc_tab32_init = 0;
static uint32_t crc_tab32[256];

/*
 * The function crc_16() calculates the 16 bits CRC16 in one pass for a byte
 * string of which the beginning has been passed to the function. The number of
 * bytes to check is also a parameter. The number of the bytes in the string is
 * limited by the constant SIZE_MAX.
 */
uint16_t crc16_generate(const unsigned char *data, size_t length)
{
    const unsigned char *ptr;
    uint16_t            crc;
    size_t              a;

    if (!crc_tab16_init) {
        crc16_initialize();
    }

    crc = CRC_START_16;
    ptr = data;

    if (ptr != NULL) {
        for (a = 0; a < length; a++) {
            crc = (crc >> 8) ^ crc_tab16[(crc ^ (uint16_t) *ptr++) & 0x00FF];
        }
    }
    return crc;
}

/*
 * The function crc_modbus() calculates the 16 bits Modbus CRC in one pass for
 * a byte string of which the beginning has been passed to the function. The
 * number of bytes to check is also a parameter.
 */
uint16_t crc_modbus(const unsigned char *data, size_t length)
{
    const unsigned char *ptr;
    uint16_t            crc;
    size_t              a;

    if (!crc_tab16_init) {
        crc16_initialize();
    }

    crc = CRC_START_MODBUS;
    ptr = data;

    if (ptr != NULL) {
        for (a = 0; a < length; a++) {
            crc = (crc >> 8) ^ crc_tab16[(crc ^ (uint16_t) *ptr++) & 0x00FF];
        }
    }
    return crc;
}

/*
 * The function update_crc_16() calculates a new CRC-16 value based on the
 * previous value of the CRC and the next byte of data to be checked.
 */
uint16_t crc16_update(uint16_t crc, unsigned char c)
{
    if (!crc_tab16_init) {
        crc16_initialize();
    }
    return (crc >> 8) ^ crc_tab16[(crc ^ (uint16_t) c) & 0x00FF];
}

/*
 * For optimal performance uses the CRC16 routine a lookup table with values
 * that can be used directly in the XOR arithmetic in the algorithm. This
 * lookup table is calculated by the crc16_initialize() routine, the first time
 * the CRC function is called.
 */
static void crc16_initialize(void)
{
    uint16_t i;
    uint16_t j;
    uint16_t crc;
    uint16_t c;

    for (i = 0; i < 256; i++) {
        crc = 0;
        c   = i;

        for (j = 0; j < 8; j++) {
            if ((crc ^ c) & 0x0001) { crc = (crc >> 1) ^ CRC_POLY_16; }
            else { crc = crc >> 1; }
            c = c >> 1;
        }
        crc_tab16[i] = crc;
    }
    crc_tab16_init = 1;
}

/*
 * uint32_t crc32_generate(const unsigned char *input_str, size_t num_bytes);
 *
 * The function crc_32() calculates in one pass the common 32 bit CRC value for
 * a byte string that is passed to the function together with a parameter
 * indicating the length.
 */
uint32_t crc32_generate(const unsigned char *input_str, size_t num_bytes)
{
    uint32_t            crc;
    const unsigned char *ptr;
    size_t              a;

    if (!crc_tab32_init) {
        crc32_initialize();
    }

    crc = CRC_START_32;
    ptr = input_str;

    if (ptr != NULL) {
        for (a = 0; a < num_bytes; a++) {
            crc = (crc >> 8) ^ crc_tab32[(crc ^ (uint32_t) *ptr++) & 0x000000FFul];
        }
    }

    return (crc ^ 0xFFFFFFFFul);

}

/*
 * uint32_t update_crc_32( uint32_t crc, unsigned char c );
 *
 * The function update_crc_32() calculates a new CRC-32 value based on the
 * previous value of the CRC and the next byte of the data to be checked.
 */
uint32_t crc32_update(uint32_t crc, unsigned char c)
{
    if (!crc_tab32_init) {
        crc32_initialize();
    }
    return (crc >> 8) ^ crc_tab32[(crc ^ (uint32_t) c) & 0x000000FFul];
}

/*
 * void crc32_initialize(void);
 *
 * For optimal speed, the CRC32 calculation uses a table with pre-calculated
 * bit patterns which are used in the XOR operations in the program.
 */
void crc32_initialize(void)
{
    uint32_t i;
    uint32_t j;
    uint32_t crc;

    for (i = 0; i < 256; i++) {

        crc = i;

        for (j = 0; j < 8; j++) {

            if (crc & 0x00000001L) { crc = (crc >> 1) ^ CRC_POLY_32; }
            else { crc = crc >> 1; }
        }

        crc_tab32[i] = crc;
    }
    crc_tab32_init = 1;
}
