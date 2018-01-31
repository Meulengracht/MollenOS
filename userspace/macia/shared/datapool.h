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
* Macia - IL Data Pool
* - This keeps track of all data for a program
* - Allows us to serialize it into bytecode easier
*/
#pragma once

/* Includes */
#include <cstring>
#include <cstdlib>
#include <map>

/* System Includes */
#include "../generator/opcodes.h"
#include "../parser/parser.h"
#include "codeobject.h"

/* The data pool 
 * Contains all the static data for a program */
class DataPool
{
public:
	DataPool();
	~DataPool();

	/* Create a new object and return
	 * the id for the current scope */
	int CreateObject(const char *pIdentifier);

	/* Create a new function for the given scope 
	 * and return the id for the current scope */
	int CreateFunction(const char *pIdentifier, int ScopeId);

	/* Create a new variable for the given scope
	 * and return the id of the variable */
	int DefineVariable(const char *pIdentifier, int ScopeId);

	/* Creates a new string in the string pool
	 * and returns the id given to it */
	int DefineString(const char *pString);

	/* Retrieve a code object Id from the given 
	 * identifier and scope */
	int LookupSymbol(const char *pIdentifier, int ScopeId);

	/* Retrieves a code object from the given 
	 * identifier path */
	CodeObject *LookupObject(const char *pPath);

	/* Calculates the memory requirement of 
	 * an object, returned as bytes */
	int CalculateObjectSize(int ObjectId);

	/* Appends bytecode to a code-object 
	 * this redirects the bytecode to the id given */
	int AddOpcode(int ScopeId, Opcode_t Opcode);
	int AddCode8(int ScopeId, char Value);
	int AddCode16(int ScopeId, short Value);
	int AddCode32(int ScopeId, int Value);
	int AddCode64(int ScopeId, long long Value);

	/* Gets */
	std::map<int, CodeObject*> &GetTable() { return m_sTable; }

private:
	/* Private - Functions */
	int CheckDublicate(const char *pIdentifier, const char *pPath);
	char *CreatePath(int ScopeId, const char *Identifier);

	/* Private - Data */
	std::map<int, CodeObject*> m_sTable;
	int m_iIdGen;
};