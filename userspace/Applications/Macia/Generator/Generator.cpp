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

/* Includes */
#include "Generator.h"

/* Constructor 
 * Takes an AST for a program */
Generator::Generator(Statement *AST) {

	/* Initialize */
	m_pPool = new DataPool();

	/* Initialize lists */
	m_sRegisters.clear();
	m_lByteCode.clear();
	m_lByteData.clear();

	/* Add registers */
	for (int i = 0; i < MACIA_REGISTER_COUNT; i++) {
		m_sRegisters[i] = 0;
	}

	/* Store */
	m_pAST = AST;
}

/* Destructor 
 * Does nothing for now */
Generator::~Generator() {

	/* Clear out lists */
	m_sRegisters.clear();
	m_lByteCode.clear();
	m_lByteData.clear();

	/* Clear out data pool */
	delete m_pPool;

	/* Null */
	m_pAST = NULL;
}

/* Generate the bytecode from the AST,
 * can be assembled or interpreted afterwards */
int Generator::Generate() {

	/* We want to do some recursive visitation steps
	 * here, unfortunately I can't use this function
	 * for the recursion as it takes no params */

	/* Step 1 will be parsing the AST */
	if (ParseStatement(m_pAST, -1)) {
		return -1;
	}

	/* Step 2 is now compiling everything together */
	for (std::map<int, CodeObject*>::iterator Itr = m_pPool->GetTable().begin(); 
		Itr != m_pPool->GetTable().end(); Itr++) {
		
		/* Get index */
		CodeObject *Obj = Itr->second;
		size_t DataLength = strlen(Obj->GetPath());
		char *DataPtr = Obj->GetPath();

		/* Serialize code object */

		/* Write Id */
		m_lByteData.push_back(Itr->first & 0xFF);
		m_lByteData.push_back((Itr->first >> 8) & 0xFF);
		m_lByteData.push_back((Itr->first >> 16) & 0xFF);
		m_lByteData.push_back((Itr->first >> 24) & 0xFF);

		/* Write Type */
		m_lByteData.push_back(Obj->GetType() & 0xFF);

		/* Write length */
		m_lByteData.push_back(DataLength & 0xFF);
		m_lByteData.push_back((DataLength >> 8) & 0xFF);
		m_lByteData.push_back((DataLength >> 16) & 0xFF);
		m_lByteData.push_back((DataLength >> 24) & 0xFF);

		/* Write Data */
		while (*DataPtr) {
			m_lByteData.push_back(*DataPtr);
			DataPtr++;
		}

		/* Function? */
		if (Obj->GetType() == CTFunction) {
			
			/* Write prologue */
			m_lByteCode.push_back(OpLabel);
			m_lByteCode.push_back(Itr->first & 0xFF);
			m_lByteCode.push_back((Itr->first >> 8) & 0xFF);
			m_lByteCode.push_back((Itr->first >> 16) & 0xFF);
			m_lByteCode.push_back((Itr->first >> 24) & 0xFF);

			/* Write code */
			for (int i = 0; i < (int)Obj->GetCode().size(); i++) {
				m_lByteCode.push_back(Obj->GetCode().at(i));
			}

			/* Write epilogue */
			m_lByteCode.push_back(OpReturn);
		}
	}
 
	/* Just return the result of Parser */
	return 0;
}

/* Save the code and data to a object file
* this can then be compiled into native code
* or run by the interpreter */
int Generator::SaveAs(const char *Path) {

	/* Variables */
	FILE *dest = NULL;
	char Header[16];

	/* Open the file handle */
	dest = fopen(Path, "w+b");

	/* Sanity */
	if (dest == NULL) {
		return -1;
	}

	/* Setup header */
	memset(&Header[0], 0, sizeof(Header));
	
	/* Version */
	Header[0] = 0x01;

	/* Size of code */
	*((size_t*)&Header[4]) = m_lByteCode.size();

	/* Size of data */
	*((size_t*)&Header[8]) = m_lByteData.size();

	/* Write the header */
	fwrite(&Header[0], 1, sizeof(Header), dest);

	/* Write code bytes */
	fwrite(&m_lByteCode[0], 1, m_lByteCode.size(), dest);

	/* Write data bytes */
	fwrite(&m_lByteData[0], 1, m_lByteData.size(), dest);

	/* Cleanup */
	fclose(dest);

	/* Done! */
	return 0;
}

/* Allocates a register or 
 * prints an error on register failure */
int Generator::AllocateRegister() {

	/* Look for a free register */
	for (int i = 0; i < MACIA_REGISTER_COUNT; i++) {
		if (m_sRegisters[i] == 0) {
			m_sRegisters[i] = 1;
			return i;
		}
	}

	/* Fail - Abort */
	printf("OUT OF REGISTERS FOR ALLOCATION! ALERT! ABORT!\n");
	return -1;
}

/* Deallocates a register and 
 * marks it free for usage */
void Generator::DeallocateRegister(int Register) {

	/* Just mark it free */
	m_sRegisters[Register] = 0;
}

/* The actual statement parser
 * This is the recursive function */
int Generator::ParseStatement(Statement *pStmt, int ScopeId) {

	/* Variables */
	GenState_t State;

	/* Sanity */
	if (pStmt == NULL)
		return 0;

	/* Detect which kind of statement
	 * this is */
	switch (pStmt->GetType()) {

		/* The statement glue */
		case StmtSequence: {

			/* Cast to correct type */
			Sequence *Seq = (Sequence*)pStmt;

			/* Generate code (left-hand first?) */
			if (ParseStatement(Seq->GetStatement1(), ScopeId)
				|| ParseStatement(Seq->GetStatement2(), ScopeId)) {
				return -1;
			}

		} break;

		/* The object declaration */
		case StmtObject: {

			/* Cast to correct type */
			Object *Obj = (Object*)pStmt;

			/* Write the Object definition */
			int Id = m_pPool->CreateObject(Obj->GetIdentifier());

			/* Sanity
			* Was it created correctly? */
			if (Id == -1) {

				/* Error message */
				printf("Unable to define object %s, check for dublicates...\n", Obj->GetIdentifier());

				/* Return error - bail! */
				return -1;
			}

			/* Parse the body */
			if (ParseStatement(Obj->GetBody(), Id)) {
				return -1;
			}

		} break;

		/* The function declaration */
		case StmtFunction: {

			/* Cast to correct type */
			Function *Func = (Function*)pStmt;

			/* Write the Function definition */
			int Id = m_pPool->CreateFunction(Func->GetIdentifier(), ScopeId);

#ifdef DIAGNOSE
			printf("Function %s()\n", Func->GetIdentifier());
#endif

			/* Sanity
			* Was it created correctly? */
			if (Id == -1) {

				/* Error message */
				printf("Unable to define function %s, check for dublicates...\n", Func->GetIdentifier());

				/* Return error - bail! */
				return -1;
			}

			/* Parse the body */
			if (ParseStatement(Func->GetBody(), Id)) {
				return -1;
			}

#ifdef DIAGNOSE
			printf("return\n", Func->GetIdentifier());
#endif

		} break;

		/* The variable declaration */
		case StmtDeclaration: {

			/* Cast to correct type */
			Declaration *Decl = (Declaration*)pStmt;

			/* Write the variable definition */
			int Id = m_pPool->DefineVariable(Decl->GetIdentifier(), ScopeId);

			/* Sanity
			 * Was it created correctly? */
			if (Id == -1) {

				/* Error message */
				printf("Unable to define variable %s, check for dublicates...\n", Decl->GetIdentifier());

				/* Return error - bail! */
				return -1;
			}

			/* Setup the GenState */
			State.CodeScopeId = ScopeId;
			State.ActiveRegister = -1;
			State.ActiveReference = Id;

			/* Parse the expression */
			if (ParseExpression(Decl->GetExpression(), &State)) {
				return -1;
			}

		} break;

		/* The assignment statement */
		case StmtAssign: {

			/* Cast to correct type */
			Assignment *Ass = (Assignment*)pStmt;

			/* Lookup the given symbol and get the id */
			int Id = m_pPool->LookupSymbol(Ass->GetIdentifier(), ScopeId);

			/* Sanity 
			 * We must find the id */
			if (Id == -1) {

				/* Error message */
				printf("Unable to find variable with name %s...\n", Ass->GetIdentifier());

				/* Return error - bail! */
				return -1;
			}

			/* Setup the GenState */
			State.CodeScopeId = ScopeId;
			State.ActiveRegister = -1;
			State.ActiveReference = Id;

			/* Parse the expression */
			if (ParseExpression(Ass->GetExpression(), &State)) {
				return -1;
			}

		} break;

		default: {
			/* Error message */
			printf("Unsupported statement for bytecode generation...\n");

			/* Return error - bail! */
			return -1;
		}
	}

	/* No Error */
	return 0;
}

/* This is a recursive expression parser 
 * used for turning AST expressions into bytecode */
int Generator::ParseExpression(Expression *pExpr, GenState_t *State) {

	/* Sanity */
	if (pExpr == NULL) {
		return 0;
	}

	/* Determine what kind of expression .. */
	switch (pExpr->GetType()) {

		/* Variable ? */
		case ExprVariable: {

			/* Cast to correct expression type */
			Variable *Var = (Variable*)pExpr;

			/* Lookup Id */
			int Id = m_pPool->LookupSymbol(Var->GetIdentifier(), State->CodeScopeId);

			/* Generate some code */
			if (State->ActiveRegister != -1) {

				/* We are working in a temporary register 
				 * use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadRA);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loadra $%i, #%i\n", State->ActiveRegister, Id);
#endif
			}
			else {

				/* We are working with a variable reference
				 * use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loada #%i, #%i\n", State->ActiveReference, Id);
#endif
			}

		} break;

		/* String Literal? */
		case ExprString: {

			/* Cast to correct expression type */
			StringValue *String = (StringValue*)pExpr;

			/* Define it in our data-pool */
			int Id = m_pPool->DefineString(String->GetValue());

			/* Generate some code */
			if (State->ActiveRegister != -1) {

				/* We are working in a temporary register
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadRA);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loadra $%i, #%i\n", State->ActiveRegister, Id);
#endif
			}
			else {

				/* We are working with a variable reference
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loada #%i, #%i\n", State->ActiveReference, Id);
#endif
			}

		} break;

		/* Int Literal? */
		case ExprInteger: {

			/* Cast to correct expression type */
			IntValue *Int = (IntValue*)pExpr;

			/* Generate some code */
			if (State->ActiveRegister != -1) {

				/* We are working in a temporary register
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpStoreRI);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode32(State->CodeScopeId, Int->GetValue());

#ifdef DIAGNOSE
				printf("storeri $%i, [%i]\n", State->ActiveRegister, Int->GetValue());
#endif
			}
			else {

				/* We are working with a variable reference
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpStoreI);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode32(State->CodeScopeId, Int->GetValue());

#ifdef DIAGNOSE
				printf("storei #%i, [%i]\n", State->ActiveReference, Int->GetValue());
#endif
			}

		} break;

		/* Binary Expression?? */
		case ExprBinary: {

			/* We need a new genstate for this */
			GenState_t TempState;

			/* Cast to correct expression type */
			BinaryExpression *BinExpr = (BinaryExpression*)pExpr;

			/* First of all, allocate a register */
			TempState.CodeScopeId = State->CodeScopeId;
			TempState.ActiveRegister = AllocateRegister();
			TempState.ActiveReference = State->ActiveReference;

			/* Now, we want to do left hand first 
			 * and make sure we store the value in main first 
			 * Then we solve right hand, but store it into the temporary 
			 * state with the temp register we have allocated */
			if (TempState.ActiveRegister == -1
				|| ParseExpression(BinExpr->GetExpression1(), State)
				|| ParseExpression(BinExpr->GetExpression2(), &TempState)) {
				return -1;
			}

			/* Now we handle the operator, we have right hand in register
			 * and left hand in id */
			if (BinExpr->GetOperator() == ExprOperatorAdd) {
				m_pPool->AddOpcode(State->CodeScopeId, OpAddRA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode8(State->CodeScopeId, TempState.ActiveRegister);

#ifdef DIAGNOSE
				printf("addra #%i, $%i\n", State->ActiveReference, TempState.ActiveRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorSubtract) {
				m_pPool->AddOpcode(State->CodeScopeId, OpSubRA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode8(State->CodeScopeId, TempState.ActiveRegister);

#ifdef DIAGNOSE
				printf("subra #%i, $%i\n", State->ActiveReference, TempState.ActiveRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorMultiply) {
				m_pPool->AddOpcode(State->CodeScopeId, OpMulRA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode8(State->CodeScopeId, TempState.ActiveRegister);

#ifdef DIAGNOSE
				printf("mulra #%i, $%i\n", State->ActiveReference, TempState.ActiveRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorDivide) {
				m_pPool->AddOpcode(State->CodeScopeId, OpDivRA);
				m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
				m_pPool->AddCode8(State->CodeScopeId, TempState.ActiveRegister);

#ifdef DIAGNOSE
				printf("divra #%i, $%i\n", State->ActiveReference, TempState.ActiveRegister);
#endif
			}

			/* Deallocate register */
			DeallocateRegister(TempState.ActiveRegister);

		} break;

		default: {

			/* Error message */
			printf("Unsupported expression for bytecode generation...\n");

			/* Error - bail out */
			return -1;
		}
	}

	/* No Error */
	return 0;
}