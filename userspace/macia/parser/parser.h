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
* Macia - Parser
* - Turns an element list into a program structure
*/
#pragma once

/* Includes */
#include "../shared/element.h"
#include "statement.h"
#include <vector>

/* The class
 * Takes a element-list and parses it
 * into a program-structure list of expressions
 * and statements */
class Parser
{
public:
	Parser(std::vector<Element*> &Elements);
	~Parser();

	/* This runs the actual parsing 
	 * process, use GetProgram to retrieve
	 * the results */
	int Parse();

	/* Retrieve the program AST */
	Statement *GetProgram() { return m_pBase; }

private:
	/* Private - Functions */
	int ParseExpression(int Index, Expression **Parent);
	int ParseStatement(int Index, Statement **Parent);
	int ParseModifiers(int Index, int *Modifiers);

	/* Private - Data */
	std::vector<Element*> m_lElements;
	Statement *m_pBase;
};

