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
* Macia - Scanner Elements (Lexer)
*/
#pragma once

/* Includes */
#include <cstdlib>

typedef enum
{
	/* Wtf? */
	UNKNOWN,

	/* Operators */
	OperatorAdd,
	OperatorSubtract,
	OperatorMultiply,
	OperatorDivide,

	/* Brackets */
	LeftParenthesis,
	RightParenthesis,
	LeftBracket,
	RightBracket,
	LeftFuncBracket,
	RightFuncBracket,

	/* Assignment */
	OperatorAssign,

	/* Special */
	OperatorSemiColon,
	Identifier,
	StringLiteral,
	DigitLiteral,

	/* Comments */
	CommentLine,
	CommentBlock

} ElementType_t;

/* This class represents an element 
 * in a file, this could be string, 
 * digit, operator etc any of the above */
class Element
{
public:
	/* Store information */
	Element(ElementType_t Type, int Line, long Character);

	/* Destructor 
	 * Cleanup data as well */
	~Element();

	/* Modify data for element */
	void SetData(const char *Data) { m_pData = Data; }

	/* Get(s) */
	ElementType_t GetType() { return m_eType; }
	const char *GetData() { return m_pData; }
	int GetLineNumber() { return m_iLinePosition; }
	long GetCharacterPosition() { return m_iCharPosition; }
	const char *GetName();

private:
	/* Private - Information */
	ElementType_t m_eType;
	const char *m_pData;

	long m_iCharPosition;
	int m_iLinePosition;
};