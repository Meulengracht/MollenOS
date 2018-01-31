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

/* Includes */
#include "interpreter.h"
#include <cstdio>

/* Constructor 
 * Save the data given for execution 
 * and setup vm */
Interpreter::Interpreter(DataPool *pPool) {
	m_pPool = pPool;
}

/* Destructor 
 * Cleanup Vm */
Interpreter::~Interpreter() {

}

/* The execution, it returns 
 * when code runs out */
int Interpreter::Execute() {

	/* Lookup main method */
	ObjectInstance *Instance = NULL;
	CodeObject *EntryObj = m_pPool->LookupObject("__maciaentry");

	/* Sanity -> We need entry */
	if (EntryObj == NULL) {
		/* Error Message */
		printf("Failed to locate program entry point\n");
		
		/* abort */
		return -1;
	}

	/* Allocate variables and objects */
	//Instance = new ObjectInstance(m_pPool->CalculateObjectSize(0), NULL);

	/* Execute code */
	return ExecuteCode(Instance, EntryObj->GetCode());
}

/* Executes the given code 
 * this function may be called recursive */
int Interpreter::ExecuteCode(ObjectInstance *Instance, std::vector<unsigned char> &Code) {
	
	/* Iterator */
	size_t Iterator = 0;

	while (Iterator < Code.size()) {

		/* Get opcode */
		Opcode_t Opcode = (Opcode_t)Code[Iterator];

		/* Increament */
		Iterator++;

		/* Handle opcode */
		switch (Opcode) {

			/* Specials */
			case OpLabel:
			case OpNew:
			case OpInvoke:
			case OpReturn: {

			} break;

			/* Store Opcodes */
			case OpStore:
			case OpStoreAR:
			case OpStoreRI:
			case OpStoreI: {

			} break;

			/* Load Opcodes */
			case OpLoadA:
			case OpLoadRA: {

			} break;

			/* Error on stupid opcodes */
			default: {
				/* Error Message */
				printf("Unhandled opcode 0x%x\n", Opcode);

				/* Bail */
				return -1;
			}
		}
	}

	/* Done! */
	return 0;
}