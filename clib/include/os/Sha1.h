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
* MollenOS MCore - SHA1 Implementation
*/

#ifndef _MCORE_SHA1_H_
#define _MCORE_SHA1_H_

/* Cpp Guard */
#ifdef __cplusplus
extern "C"
{
#endif

/* Definitions */
#define SHA1_DIGEST_SIZE 20

/* Structure describing the 
	* sha context */
typedef struct {
	/* Handsoff? */
	int handsoff;

	/* Sha1 Context State */
	uint32_t state[5];

	/* Count buffer */
	uint32_t count[2];

	/* Data buffer */
	uint8_t buffer[64];

} Sha1Context_t;

/* Initializes a new SHA1 context 
 * using either an internal buffer for 
 * hashing by setting handsoff to 1, otherwise
 * it will destroy the given data buffers */
_CRT_EXTERN void Sha1Init(Sha1Context_t *Context, int Handsoff);

/* Add data to the given SHA1 context,
 * this is the function for using the context */
_CRT_EXTERN void Sha1Add(Sha1Context_t *Context, const uint8_t *Data, const size_t Length);

/* Finalizes the Sha1 context and outputs the
 * result to a digest buffer the user must provide */
_CRT_EXTERN void Sha1Finalize(Sha1Context_t *Context, uint8_t Digest[SHA1_DIGEST_SIZE]);

/* Converts the digest buffer to a hex-string 
 * by calling this function */
_CRT_EXTERN void Sha1DigestToHex(const uint8_t Digest[SHA1_DIGEST_SIZE], char *Output);

/* Cpp end Guard */
#ifdef __cplusplus
}
#endif

#endif //!_MCORE_SHA1_H_