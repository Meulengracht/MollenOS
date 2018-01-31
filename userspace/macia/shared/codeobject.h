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
#include <vector>

/* The code type
 * Identifies the type of the code object */
typedef enum {

	/* Build in objects */
	CTObject,
	CTFunction,
	CTVariable,
	CTString

} CodeType_t;

/* The code object
 * Represents everything that is serializable to IL */
class CodeObject
{
public:
	CodeObject(CodeType_t pType, const char *pIdentifier,
		const char *pPath, int pScopeId);
	~CodeObject();

	/* State Tracking
	 * These allow for defining children
	 * and their offsets in the object or function */
	int AllocateFunctionOffset();
	int AllocateVariableOffset();
	void SetOffset(int Offset) { m_iOffset = Offset; }

	/* Add Code */
	void AddCode(unsigned char Opcode) { m_lByteCode.push_back(Opcode); }

	/* Gets */
	std::vector<unsigned char> &GetCode() { return m_lByteCode; }
	CodeType_t GetType() { return m_eType; }
	const char *GetPath() { return m_pPath; }
	char *GetIdentifier() { return m_pIdentifier; }
	int GetScopeId() { return m_iScopeId; }
	int GetOffset() { return m_iOffset; }

private:
	/* Private - ByteCode */
	std::vector<unsigned char> m_lByteCode;

	/* Private - Data */
	CodeType_t m_eType;
	char *m_pIdentifier;
	const char *m_pPath;
	int m_iScopeId;

	/* Private - State Tracking */
	int m_iFunctionsDefined;
	int m_iVariablesDefined;
	int m_iOffset;
};
