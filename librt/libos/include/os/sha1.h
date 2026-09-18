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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
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

_CODE_BEGIN
/**
 * @brief Initializes a SHA-1 context.
 * @param Context Context to initialize.
 * @param Handsoff Whether the context should use an internal hashing buffer.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(
        oserr_t,
        Sha1Init(
	_In_ Sha1Context_t *Context, 
	_In_ int Handsoff));

/**
 * @brief Adds data to a SHA-1 context.
 * @param Context SHA-1 context to update.
 * @param Data Data to hash.
 * @param Length Number of bytes in Data.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(
        oserr_t,
        Sha1Add(
	_In_ Sha1Context_t *Context, 
	_In_ const uint8_t *Data,
	_In_ const size_t Length));

/**
 * @brief Finalizes a SHA-1 context and writes the digest.
 * @param Context SHA-1 context to finalize.
 * @param Digest Receives the SHA-1 digest.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(
        oserr_t,
        Sha1Finalize(
	_In_ Sha1Context_t *Context, 
	_Out_ uint8_t Digest[SHA1_DIGEST_SIZE]));

/**
 * @brief Converts a SHA-1 digest to a hexadecimal string.
 * @param Digest Digest to convert.
 * @param Output Receives the hexadecimal string.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(
        oserr_t,
        Sha1DigestToHex(
	_In_ uint8_t Digest[SHA1_DIGEST_SIZE], 
	_Out_ char *Output));
_CODE_END

#endif //!_SHA1_INTERFACE_H_
