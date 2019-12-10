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
#include <ds/mstring.h>

/* This is the block size that mstring allocates with
 * this can be tweaked by the user */
#define MSTRING_BLOCK_SIZE 64

typedef struct MString {
    void*  Data;
    size_t Length;
    size_t MaxLength;
} MString_t;

/* Converts a single char (ASCII, UTF16, UTF32) to UTF8 
 * and returns the number of bytes the new utf8 
 * 'string' takes up. Returns 0 if conversion was good */
CRTDECL(int, Utf8ConvertCharacterToUtf8(mchar_t Character, void* oBuffer, size_t *Length));

/* Bytes used by given (ASCII, UTF16, UTF32) character in UTF-8 Encoding
 * If 0 is returned the character was invalid */
CRTDECL(size_t, Utf8ByteSizeOfCharacterInUtf8(mchar_t Character));

/* Reads the next utf-8 sequence out of a string, updating an index 
 * the index keeps track of how many characters into the string
 * we are. Returns MSTRING_EOS on errors */
CRTDECL(mchar_t, Utf8GetNextCharacterInString(const char *Str, int *Index));

/* Character Count of UTF8-String 
 * Returns the size of an UTF8 string in char-count 
 * this is used to tell how long strings are */
CRTDECL(size_t, Utf8CharacterCountInString(const char *Str));

/* Byte Count of UTF8-String 
 * Returns the size of an UTF8 string in bytes 
 * this is used to tell how long strings are */
CRTDECL(size_t, Utf8ByteCountInString(const char *Str));

/* Helper for internal functions
 * to automatically resize the buffer 
 * of a string to be able to fit a certain size */
CRTDECL(void, MStringResize(MString_t *String, size_t Length));

#endif //!_MSTRING_PRIV_H_
