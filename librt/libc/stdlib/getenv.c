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
* MollenOS C Library - Get Environment Variable
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* A NULL environment, the day we support env
 * variables this should be in TLS and initialized */
static char *_GlbEnvironmentNull[] = { NULL };
char **_GlbEnviron = &_GlbEnvironmentNull[0];

/* Get environmental var 
 * Returns the settings for a given key */
char *getenv(const char *name)
{
	/* Get a pointer to the environment 
	 * first entry  */
	char ***Env = &_GlbEnviron;
	register int len;
	register char **p;
	const char *c;

	/* Sanitize the env variable, the first entry
	 * may not be null actually */
	if (!*Env) {
		return NULL;
	}

	/* Set inital state */
	c = name;
	while (*c && *c != '=')  c++;

	/* Identifiers may not contain an '=', so cannot match if does */
	if (*c != '=') {
		len = c - name;
		for (p = *Env; *p; ++p) {
			if (!strncmp(*p, name, len)) {
				if (*(c = *p + len) == '=') {
					return (char *)(++c);
				}
			}
		}
	}

	/* Not found */
	return NULL;
}
