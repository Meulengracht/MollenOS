/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - SHA1 Support Definitions & Structures
 * - This header describes the base sha1-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SHA1_INTERFACE_H_
#define _SHA1_INTERFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>

/* SHA1 Definitions 
 * Fixed constants for calculating the SHA1 */
#define SHA1_DIGEST_SIZE 20

/* The SHA1 Context
 * Structure describing the needed
 * variables for calculating the SHA1 */
typedef struct {
	int					handsoff;
	uint32_t			state[5];
	uint32_t			count[2];
	uint8_t				buffer[64];
} Sha1Context_t;

/* Start one of these before function prototypes */
_CODE_BEGIN

/* Sha1Init
 * Initializes a new SHA1 context 
 * using either an internal buffer for 
 * hashing by setting handsoff to 1, otherwise
 * it will destroy the given data buffers */
MOSAPI 
OsStatus_t
Sha1Init(
	_In_ Sha1Context_t *Context, 
	_In_ int Handsoff);

/* Sha1Add
 * Add data to the given SHA1 context,
 * this is the function for using the context */
MOSAPI 
OsStatus_t
Sha1Add(
	_In_ Sha1Context_t *Context, 
	_In_ __CONST uint8_t *Data,
	_In_ __CONST size_t Length);

/* Sha1Finalize
 * Finalizes the Sha1 context and outputs the
 * result to a digest buffer the user must provide */
MOSAPI 
OsStatus_t
Sha1Finalize(
	_In_ Sha1Context_t *Context, 
	_Out_ uint8_t Digest[SHA1_DIGEST_SIZE]);

/* Sha1DigestToHex
 * Converts the digest buffer to a hex-string 
 * by calling this function */
MOSAPI 
OsStatus_t
Sha1DigestToHex(
	_In_ uint8_t Digest[SHA1_DIGEST_SIZE], 
	_Out_ char *Output);

_CODE_END

#endif //!_SHA1_INTERFACE_H_
