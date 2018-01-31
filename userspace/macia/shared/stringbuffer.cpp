/* The Macia Language (MACIA)
*
* Copyright 2016, Philip Meulengracht
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
* Macia - Stringbuffer (Shared)
*/

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "stringbuffer.h"

/* Constant determining initial pointer list length - is small so we can test growPtrList() */

#define STR_BUF_INIT_SIZE 32

/* Function declarations */
void error(const char *msg);
void Grow(StringBuffer_t *sb);
void Init(StringBuffer_t *sb);

/* Quasi-public functions exposed as function pointers in StringBuffer structure */

/* Wrapper around malloc() - allocate memory and initialize to all zeros */
void *AllocateMemory(int nBytes)
{
	void *ret = malloc(nBytes);

	/* Out of memory?! */
	if (!ret)
		error("Memory allocation failed!");

	/* Null it */
	memset(ret, 0, nBytes);
	return ret;
}

/* Factory-like StringBuffer instantiator */
StringBuffer_t *GetStringBuffer() {
	StringBuffer_t *sb = (StringBuffer_t*)AllocateMemory(sizeof(StringBuffer_t));
	Init(sb);
	return sb;
}

/* Append a string to the StringBuffer */
void Append(StringBuffer_t *Sb, char c) 
{
	/* Sanity character, no EOS */
	if (!c)
		error("Null character passed for argument 'c' in SB.append()!"); /* Abort */
	
	/* Size-check! */
	if (Sb->Count == Sb->Capacity)
		Grow(Sb);
	
	/* Append */
	Sb->Characters[Sb->Count++] = c;
}

/* Catenate all strings and return result */
const char *ToString(StringBuffer_t *Sb)
{
	/* Null-checks! */
	if (!Sb || !Sb->Count)
		return ""; /* TODO: Decide if error message or other action is preferable */

	/* Allocate a new string */
	return strdup(Sb->Characters);
}

/* Delete this StringBuffer object and free all memory */
/* Note: The argument 'sb' is the ADDRESS of the POINTER to a StringBuffer structure */
void Dispose(StringBuffer_t **Sb) 
{
	/* Null-checks! */
	if (!Sb || !*Sb || !(*Sb)->Characters)
		return; /* TODO: Decide if should abort here or take other action */

	/* Cleanup */
	free((*Sb)->Characters);
	free(*Sb);

	/* Invalidate pointer */
	*Sb = NULL;
}

/* Begin of quasi-private functions */

/* Print simple error message to stderr and call exit() */
void error(const char *msg) {
	fprintf(stderr, "%s\n", (msg) ? msg : "");
	exit(1);
}

/* Double length of the characters when append() needs to go past current limit */
void Grow(StringBuffer_t *Sb) {
	size_t nBytes = 2 * Sb->Capacity * sizeof(char);
	char *pTemp = (char*)AllocateMemory(nBytes);
	memcpy((void *)pTemp, Sb->Characters, Sb->Capacity * sizeof(char));
	Sb->Capacity *= 2;
	free(Sb->Characters);
	Sb->Characters = pTemp;
}

/* Initialize a new StringBuffer structure */
void Init(StringBuffer_t *Sb) 
{
	Sb->Count = 0;
	Sb->Capacity = STR_BUF_INIT_SIZE;
	Sb->Characters = (char*)AllocateMemory(STR_BUF_INIT_SIZE * sizeof(char));
	Sb->Append = Append;
	Sb->ToString = ToString;
	Sb->Dispose = Dispose;
}