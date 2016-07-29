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
* Macia - IL Generation
* - This is the IL generation system
* - It converts AST into Macia bytecode
*/
#pragma once

/* Includes */
#include <cstring>
#include <cstdlib>

/* System Includes */
#include "../Parser/Parser.h"
#include "../Shared/DataPool.h"

/* The generation-class
 * Converts AST into IL Bytecode */
class Generator
{
public:
	Generator(Statement *AST);
	~Generator();

	/* Generate the bytecode from the AST,
	 * can be assembled or interpreted afterwards */
	int Generate();

private:
	/* Private - Functions */
	int ParseStatement(Statement *pStmt, int ScopeId);
	int ParseExpression(Expression *pExpr, int ScopeId);

	/* Private - Data */
	Statement *m_pAST;
	DataPool *m_pPool;
};