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
* Macia - Virtual Machine
* - This virtual machine implementation is capable
* - of executing the Macia bytecode language
*/
#pragma once

/* Includes */
#include <cstring>
#include <cstdlib>
#include <vector>

/* System Includes */
#include "machinestate.h"
#include "../shared/datapool.h"

/* The object instance 
 * this is used for objects when new instances
 * of something is created */
class ObjectInstance
{
public:
	ObjectInstance(size_t Size, CodeObject *Type) {
		m_pBase = malloc(Size);
		m_pType = Type;
		m_iSize = Size;
	}

	~ObjectInstance() {
		free(m_pBase);
	}

	/* Gets */
	CodeObject *GetType() { return m_pType; }
	size_t GetSize() { return m_iSize; }
	void *GetBase() { return m_pBase; }

private:
	/* Private - Data */
	CodeObject *m_pType;
	size_t m_iSize;
	void *m_pBase;
};

/* The interpreter class 
 * this contains all functionality needed
 * for executing Macia bytecode */
class Interpreter
{
public:
	Interpreter(DataPool *pPool);
	~Interpreter();

	/* Run the interpreter
	 * this returns when the code is at end */
	int Execute();

private:
	/* Private - Functions */
	int ExecuteCode(ObjectInstance *Instance, std::vector<unsigned char> &Code);

	/* Private - Data */
	DataPool *m_pPool;
};