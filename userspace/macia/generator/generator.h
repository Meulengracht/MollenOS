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

#include <cstring>
#include <cstdlib>

#define MACIA_REGISTER_COUNT	4
#define DIAGNOSE
#define VERSION "0.0.1-dev"
#define AUTHOR	"Philip Meulengracht"

/* System Includes */
#include "../parser/parser.h"
#include "../shared/datapool.h"

/* This is the generator state 
 * structure that holds information 
 * about target register etc */
typedef struct {

	/* Keeps track of the code-scope
	 * this is the scope we should append
	 * the code too */
	int CodeScopeId;

	/* This contains things 
	 * as active register */
	int ActiveRegister;
	int IntermediateRegister;
	int ActiveReference;

	/* If we need cleanup for this
	 * environment state */
	int GenerateCleanUp;

} GenState_t;

/* Expression precedence groups */
typedef enum {

	/* Group 1:
	 * ++ (suffix), -- (suffix), func(),
	 * arr[], '.' obj access */
	OpGroup1,

	/* Group 2:
	 * ++ (prefix), -- (prefix),
	 * + (unary), - (unary) */
	OpGroup2,

	/* Group 3:
	 * '*, '/', '%' */
	OpGroup3,

	/* Group 4:
	 * + (add), - (sub) */
	OpGroup4,

	/* Singles */
	OpGroupSingles,

	/* Used for iteration */
	OperatorGroupCount

} OperatorGroup_t;

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

	/* Save the code and data to a object file 
	 * this can then be compiled into native code
	 * or run by the interpreter */
	int SaveAs(const char *Path);

	/* Gets */
	std::vector<unsigned char> &GetCode() { return m_lByteCode; }
	std::vector<unsigned char> &GetData() { return m_lByteData; }
	DataPool *GetPool() { return m_pPool; }

private:
	/* Private - Functions */
	int ParseStatement(Statement *pStmt, int ScopeId);
	int ParseExpressions(Expression *pExpr, GenState_t *State);
	int ParseExpression(Expression *pExpr, GenState_t *State, OperatorGroup_t Group);
	int AllocateRegister();
	void DeallocateRegister(int Register);

	void GenerateEntry();

	/* Private - Data */
	std::vector<unsigned char> m_lByteCode;
	std::vector<unsigned char> m_lByteData;
	std::map<int, int> m_sRegisters;
	Statement *m_pAST;
	DataPool *m_pPool;
};