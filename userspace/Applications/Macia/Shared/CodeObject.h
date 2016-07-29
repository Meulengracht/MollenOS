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
#pragma once

/* Includes */
#include <cstring>
#include <cstdlib>

/* The code type
* Identifies the type of the code object */
typedef enum {

	/* Build in objects */
	CTObject,
	CTFunction,
	CTVariable

} CodeType_t;

/* The code object
* Represents everything that is serializable to IL */
class CodeObject
{
public:
	CodeObject(CodeType_t pType, char *pIdentifier,
		char *pPath, int pScopeId);
	~CodeObject();

	/* Gets */
	char *GetPath() { return m_pPath; }
	char *GetIdentifier() { return m_pIdentifier; }

private:
	/* Private - Data */
	CodeType_t m_eType;
	char *m_pIdentifier;
	char *m_pPath;
	int m_iScopeId;
};