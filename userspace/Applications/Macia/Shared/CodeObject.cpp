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
* Macia - IL Code Object
* - This keeps track of all data for a program
* - Allows us to serialize it into bytecode easier
*/

/* Includes */
#include "CodeObject.h"

/* Constructor 
 * Initialize and setup vars */
CodeObject::CodeObject(CodeType_t pType, char *pIdentifier,
	char *pPath, int pScopeId) {

	/* Store */
	m_eType = pType;
	m_pIdentifier = strdup(pIdentifier);
	m_pPath = pPath;
	m_iScopeId = pScopeId;
}

/* Destructor 
 * Cleanup the name variables */
CodeObject::~CodeObject() {
	free(m_pIdentifier);
}