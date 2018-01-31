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
#include "codeobject.h"

/* Constructor 
 * Initialize and setup vars */
CodeObject::CodeObject(CodeType_t pType, const char *pIdentifier,
	const char *pPath, int pScopeId) {

	/* Store */
	m_eType = pType;
	m_pIdentifier = strdup(pIdentifier);
	m_pPath = pPath;
	m_iScopeId = pScopeId;

	/* Zero */
	m_iFunctionsDefined = 0;
	m_iVariablesDefined = 0;
	m_iOffset = 0;

	/* Clear out */
	m_lByteCode.clear();
}

/* Destructor 
 * Cleanup the name variables */
CodeObject::~CodeObject() {
	m_lByteCode.clear();
	if (m_pIdentifier != NULL)
		free(m_pIdentifier);
}

/* State Tracking
 * These allow for defining children
 * and their offsets in the object or function */
int CodeObject::AllocateFunctionOffset() {
	int RetVal = m_iFunctionsDefined;
	m_iFunctionsDefined++;
	return RetVal;
}

/* State Tracking
 * These allow for defining children
 * and their offsets in the object or function */
int CodeObject::AllocateVariableOffset() {
	int RetVal = m_iVariablesDefined;
	m_iVariablesDefined++;
	return RetVal;
}