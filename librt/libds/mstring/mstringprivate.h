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
* MollenOS MCore - String Format
*/

#ifndef _MSTRING_PRIV_H_
#define _MSTRING_PRIV_H_

/*
 UTF-8 encoding/decoding functions
 Return # of bytes in UTF-8 sequence; result < 0 if illegal sequence
 
 Also see below for UTF-16 encoding/decoding functions
 
 References :
 
 1) UCS Transformation Format 8 (UTF-8):
 ISO/IEC 10646-1:1996 Amendment 2 or ISO/IEC 10646-1:2000 Annex D
 <http://anubis.dkuug.dk/JTC1/SC2/WG2/docs/n1335>
 <http://www.cl.cam.ac.uk/~mgk25/ucs/ISO-10646-UTF-8.html>
 
 Table 4 - Mapping from UCS-4 to UTF-8
 
 2) Unicode standards:
 <http://www.unicode.org/unicode/standard/standard.html>
 
 3) Legal UTF-8 byte sequences:
 <http://www.unicode.org/unicode/uni2errata/UTF-8_Corrigendum.html>
 
 Code point          1st byte    2nd byte    3rd byte    4th byte
 ----------          --------    --------    --------    --------
 U+0000..U+007F      00..7F
 U+0080..U+07FF      C2..DF      80..BF
 U+0800..U+0FFF      E0          A0..BF      80..BF
 U+1000..U+FFFF      E1..EF      80..BF      80..BF
 U+10000..U+3FFFF    F0          90..BF      80..BF      80..BF
 U+40000..U+FFFFF    F1..F3      80..BF      80..BF      80..BF
 U+100000..U+10FFFF  F4          80..8F      80..BF      80..BF
 
 The definition of UTF-8 in Annex D of ISO/IEC 10646-1:2000 also
 allows for the use of five- and six-byte sequences to encode
 characters that are outside the range of the Unicode character
 set; those five- and six-byte sequences are illegal for the use
 of UTF-8 as a transformation of Unicode characters. ISO/IEC 10646
 does not allow mapping of unpaired surrogates, nor U+FFFE and U+FFFF
 (but it does allow other noncharacters).
 
 4) RFC 2279: UTF-8, a transformation format of ISO 10646:
 <http://www.ietf.org/rfc/rfc2279.txt>
 
 5) UTF-8 and Unicode FAQ:
 <http://www.cl.cam.ac.uk/~mgk25/unicode.html>
 
 6) Markus Kuhn's UTF-8 decoder stress test file:
 <http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt>
 
 7) UTF-8 Demo:
 <http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-demo.txt>
 
 8) UTF-8 Sampler:
 <http://www.columbia.edu/kermit/utf8.html>
 
 9) Transformation Format for 16 Planes of Group 00 (UTF-16):
 ISO/IEC 10646-1:1996 Amendment 1 or ISO/IEC 10646-1:2000 Annex C
 <http://anubis.dkuug.dk/JTC1/SC2/WG2/docs/n2005/n2005.pdf>
 <http://www.cl.cam.ac.uk/~mgk25/ucs/ISO-10646-UTF-16.html>
 
 10) RFC 2781: UTF-16, an encoding of ISO 10646:
 <http://www.ietf.org/rfc/rfc2781.txt>
 
 11) UTF-16 invalid surrogate pairs:
 <http://www.unicode.org/unicode/faq/utf_bom.html#16>
 
  UTF-16       UTF-8          UCS-4
  D83F DFF*    F0 9F BF B*    0001FFF*
  D87F DFF*    F0 AF BF B*    0002FFF*
  D8BF DFF*    F0 BF BF B*    0003FFF*
  D8FF DFF*    F1 8F BF B*    0004FFF*
  D93F DFF*    F1 9F BF B*    0005FFF*
  D97F DFF*    F1 AF BF B*    0006FFF*
                  ...
  DBBF DFF*    F3 BF BF B*    000FFFF*
  DBFF DFF*    F4 8F BF B*    0010FFF*
 
  * = E or F
 
  1010  A
  1011  B
  1100  C
  1101  D
  1110  E
  1111  F
 
  */

#include <os/osdefs.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <ds/ds.h>
#include <ds/mstring.h>

/* This is the block size that mstring allocates with
 * this can be tweaked by the user */
#define MSTRING_BLOCK_SIZE 64

typedef struct MString {
    void*  Data;
    size_t Length;
    size_t MaxLength;
} MString_t;

/**
 * @brief Converts an character to an UTF8 sequence.
 * 
 * @param character  The character that should be converted to utf8 encoding
 * @param utf8buffer The buffer where the utf8 sequence will be written to. The buffer should be 6 bytes wide atleast to support all sequences.
 * @param length     The length of the utf8 buffer.
 * @return int       Returns 0 on successful conversion.
 */
CRTDECL(int, Utf8ConvertCharacterToUtf8(mchar_t Character, void* oBuffer, size_t *Length));

/**
 * @brief Returns the number of bytes required to encode the given character
 * in UTF8
 * 
 * @param character The character that should be encoded in utf8
 * @return size_t   The number of bytes required
 */
CRTDECL(size_t, Utf8ByteSizeOfCharacterInUtf8(uint32_t Character));

/**
 * @brief Reads the next utf-8 sequence out of a string. Uses the index pointer
 * to keep track of where to parse the next character.
 * 
 * @param string   Zero terminated string
 * @param indexp   A pointer to an index variable. This will be modified upon parse exit.
 * @return mchar_t The character parsed. Returns MSTRING_EOS on errors.
 */
CRTDECL(mchar_t, Utf8GetNextCharacterInString(const char *Str, int *Index));

/**
 * @brief Retrieve the number of utf8 characters present in the given 0 terminated string
 * 
 * @param string  The zero-terminated utf8 string to count characters in
 * @return size_t Character count
 */
CRTDECL(size_t, Utf8CharacterCountInString(const char *Str));

/**
 * @brief Get the byte count of the utf8 string. This does not include the 0 termination byte.
 * 
 * @param string  The zero-terminated utf8 string
 * @return size_t The number of bytes it occupies.
 */
CRTDECL(size_t, Utf8ByteCountInString(const char *Str));

/* Helper for internal functions
 * to automatically resize the buffer 
 * of a string to be able to fit a certain size */
CRTDECL(void, MStringResize(MString_t * string, size_t size));

#endif //!_MSTRING_PRIV_H_
