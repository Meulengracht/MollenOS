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

/* Includes */
#include "element.h"

/* Element Type Names */
const char *__ElementTypeNames[] = {
	"UNKNOWN",

	"Operator - ADD",
	"Operator - SUB",
	"Operator - MUL",
	"Operator - DIV",

	"Left Parenthesis",
	"Right Parenthesis",
	"Left Bracket",
	"Right Bracket",
	"Left Function Bracket",
	"Right Function Bracket",

	"Operator - ASSIGN",

	"Operator - SEMICOLON",
	"Identifier",
	"StringLiteral",
	"DigitLiteral",

	"Comment Line",
	"Comment Block"
};

/* Constructor */
Element::Element(ElementType_t Type, int Line, long Character) {
	m_eType = Type;
	m_iLinePosition = Line;
	m_iCharPosition = Character;
}

/* Destructor
 * Cleanup data as well */
Element::~Element() {
	if (m_pData != NULL) {
		free((void*)m_pData);
	}
}

/* Converts the type into
 * a printable name */
const char *Element::GetName() {
	return __ElementTypeNames[(int)m_eType];
}