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
#define SHA1HANDSOFF

#include <os/sha1.h>
#include <stdio.h>
#include <string.h>

void Sha1Transform(int handsoff, uint32_t state[5], const uint8_t buffer[64]);

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
/* FIXME: can we do this in an endian-proof way? */
#ifdef WORDS_BIGENDIAN
#define blk0(i) block->l[i]
#else
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

/* Hash a single 512-bit block. This is the core of the algorithm. */
void Sha1Transform(int handsoff, uint32_t state[5], const uint8_t buffer[64])
{
	static uint8_t workspace[64];
	uint32_t a, b, c, d, e;
	typedef union {
		uint8_t c[64];
		uint32_t l[16];
	} CHAR64LONG16;
	CHAR64LONG16 *block;

	if (handsoff) {
		block = (CHAR64LONG16 *)workspace;
		memcpy(block, buffer, 64);
	}
	else {
		block = (CHAR64LONG16 *)buffer;
	}

	/* Copy context->state[] to working vars */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	/* 4 rounds of 20 operations each. Loop unrolled. */
	R0(a, b, c, d, e, 0); R0(e, a, b, c, d, 1); R0(d, e, a, b, c, 2); R0(c, d, e, a, b, 3);
	R0(b, c, d, e, a, 4); R0(a, b, c, d, e, 5); R0(e, a, b, c, d, 6); R0(d, e, a, b, c, 7);
	R0(c, d, e, a, b, 8); R0(b, c, d, e, a, 9); R0(a, b, c, d, e, 10); R0(e, a, b, c, d, 11);
	R0(d, e, a, b, c, 12); R0(c, d, e, a, b, 13); R0(b, c, d, e, a, 14); R0(a, b, c, d, e, 15);
	R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
	R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
	R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
	R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
	R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
	R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
	R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
	R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
	R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
	R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
	R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
	R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
	R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
	R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
	R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
	R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);

	/* Add the working vars back into context.state[] */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;

	/* Wipe variables */
	a = b = c = d = e = 0;
}

/* Sha1Init
 * Initializes a new SHA1 context 
 * using either an internal buffer for 
 * hashing by setting handsoff to 1, otherwise
 * it will destroy the given data buffers */
oserr_t
Sha1Init(
	_In_ Sha1Context_t *Context, 
	_In_ int Handsoff)
{
	/* Save handsoff */
	Context->handsoff = Handsoff;

	/* SHA1 initialization constants */
	Context->state[0] = 0x67452301;
	Context->state[1] = 0xEFCDAB89;
	Context->state[2] = 0x98BADCFE;
	Context->state[3] = 0x10325476;
	Context->state[4] = 0xC3D2E1F0;
	Context->count[0] = Context->count[1] = 0;

	return OsOK;
}

/* Sha1Add
 * Add data to the given SHA1 context,
 * this is the function for using the context */
oserr_t
Sha1Add(
	_In_ Sha1Context_t *Context, 
	_In_ __CONST uint8_t *Data,
	_In_ __CONST size_t Length)
{
	/* Variables */
	size_t i, j;

	j = (Context->count[0] >> 3) & 63;
	if ((Context->count[0] += Length << 3) < (Length << 3))
		Context->count[1]++;
	Context->count[1] += (Length >> 29);
	if ((j + Length) > 63) {
		memcpy(&Context->buffer[j], Data, (i = 64 - j));
		Sha1Transform(Context->handsoff, Context->state, Context->buffer);
		for (; i + 63 < Length; i += 64) {
			Sha1Transform(Context->handsoff, Context->state, Data + i);
		}
		j = 0;
	}
	else
		i = 0;
	memcpy(&Context->buffer[j], &Data[i], Length - i);
	return OsOK;
}

/* Sha1Finalize
 * Finalizes the Sha1 context and outputs the
 * result to a digest buffer the user must provide */
oserr_t
Sha1Finalize(
	_In_ Sha1Context_t *Context, 
	_Out_ uint8_t Digest[SHA1_DIGEST_SIZE])
{
	/* Variables */
	uint32_t i;
	uint8_t finalcount[8];

	for (i = 0; i < 8; i++) {
		finalcount[i] = (unsigned char)((Context->count[(i >= 4 ? 0 : 1)]
			>> ((3 - (i & 3)) * 8)) & 255);        /* Endian independent */
	}
	Sha1Add(Context, (uint8_t *) "\200", 1);
	while ((Context->count[0] & 504) != 448) {
		Sha1Add(Context, (uint8_t *) "\0", 1);
	}
	Sha1Add(Context, finalcount, 8);        /* Should cause a SHA1_Transform() */
	for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
		Digest[i] = (uint8_t)
			((Context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
	}

	/* Wipe variables */
	i = 0;
	memset(Context->buffer, 0, 64);
	memset(Context->state, 0, 20);
	memset(Context->count, 0, 8);
	memset(finalcount, 0, 8);   /* SWR */

	if (Context->handsoff) {
		/* make SHA1Transform overwrite its own static vars */
		Sha1Transform(Context->handsoff, Context->state, Context->buffer);
	}

	return OsOK;
}

/* Sha1DigestToHex
 * Converts the digest buffer to a hex-string 
 * by calling this function */
oserr_t
Sha1DigestToHex(
	_In_ uint8_t Digest[SHA1_DIGEST_SIZE], 
	_Out_ char *Output)
{
	/* Variables */
	int i, j;
	char *c = Output;

	/* Iterate digest data and convert
	 * each byte to hex code by using sprintf */
	for (i = 0; i < SHA1_DIGEST_SIZE / 4; i++) {
		for (j = 0; j < 4; j++) {
			sprintf(c, "%02X", Digest[i * 4 + j]);
			c += 2;
		}
		sprintf(c, " ");
		c += 1;
	}

	/* Null-terminate */
	*(c - 1) = '\0';
	return OsOK;
}
