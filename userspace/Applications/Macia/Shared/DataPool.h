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
#include "../Parser/Parser.h"
#include "CodeObject.h"

/* The data pool 
 * Contains all the static data for a program */
class DataPool
{
public:
	DataPool();
	~DataPool();

	/* Create a new object and return
	 * the id for the current scope */
	int CreateObject(char *pIdentifier);

	/* Create a new function for the given scope 
	 * and return the id for the current scope */
	int CreateFunction(char *pIdentifier, int ScopeId);

	/* Create a new variable for the given scope
	 * and return the id of the variable */
	int DefineVariable(char *pIdentifier, int ScopeId);

	/* Retrieve a code object Id from the given 
	 * scope and identifier */
	int LookupSymbol(char *pIdentifier, int ScopeId);

private:
	/* Private - Functions */
	int CheckDublicate(char *pIdentifier, char *pPath);
	char *CreatePath(int ScopeId, char *Identifier);

	/* Private - Data */
	std::map<int, CodeObject*> m_sTable;
	int m_iIdGen;
};