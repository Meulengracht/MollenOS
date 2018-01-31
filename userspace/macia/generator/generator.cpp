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
#include "generator.h"
#include <cstdio>

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

/* Generates the entry point for the program 
 * this includes instantiating a new object of 'Program' */
void Generator::GenerateEntry() {

	/* Variables */
	int TemporaryRegister = -1;
	int ObjectId = 0;
	int ConstructorId = 0;
	int MainId = 0;
	int VarId = 0;
	int Id = 0;

	/* Lookup program object & functions */
	ObjectId = m_pPool->LookupSymbol("Program", -1);
	ConstructorId = m_pPool->LookupSymbol("Program", ObjectId);
	MainId = m_pPool->LookupSymbol("Main", ObjectId);

	/* Sanity */
	if (ObjectId == -1
		|| MainId == -1) {
		printf("Missing main function 'Program.Main', you need the Program Object and the Main function to run anything\n");
		return;
	}

	/* Generate the entry point function */
	Id = m_pPool->CreateFunction("__maciaentry", -1);

	/* Define the needed variables */
	VarId = m_pPool->DefineVariable("__entry", Id);

	/* Allocate a register */
	TemporaryRegister = AllocateRegister();

	/* Add code */
	m_pPool->AddOpcode(Id, OpNew);
	m_pPool->AddCode8(Id, TemporaryRegister);
	m_pPool->AddCode32(Id, ObjectId);

	m_pPool->AddOpcode(Id, OpStoreAR);
	m_pPool->AddCode32(Id, VarId);
	m_pPool->AddCode8(Id, TemporaryRegister);

	m_pPool->AddOpcode(Id, OpInvoke);
	m_pPool->AddCode8(Id, TemporaryRegister);
	m_pPool->AddCode32(Id, ConstructorId);

	m_pPool->AddOpcode(Id, OpInvoke);
	m_pPool->AddCode8(Id, TemporaryRegister);
	m_pPool->AddCode32(Id, MainId);

	/* Cleanup */
	DeallocateRegister(TemporaryRegister);
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

	/* Generate an entry point */
	GenerateEntry();

	/* Step 2 is now compiling everything together */
	for (std::map<int, CodeObject*>::iterator Itr = m_pPool->GetTable().begin(); 
		Itr != m_pPool->GetTable().end(); Itr++) {
		
		/* Get index */
		CodeObject *Obj = Itr->second;
		size_t DataLength = strlen(Obj->GetPath());
		const char *DataPtr = Obj->GetPath();

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
			printf("return\n");
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
			State.ActiveReference = Id;

			/* Parse the expression */
			if (ParseExpressions(Decl->GetExpression(), &State)) {
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
			State.ActiveReference = Id;

			/* Parse the expression */
			if (ParseExpressions(Ass->GetExpression(), &State)) {
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

/* Expression Parser */
int Generator::ParseExpressions(Expression *pExpr, GenState_t *State) {

	/* Setup initial registers */
	State->ActiveRegister = -1;
	State->IntermediateRegister = -1;
	State->GenerateCleanUp = 0;

	/* Now generate the code */
	for (int i = 0; i < (int)OperatorGroupCount; i++) {
		if (ParseExpression(pExpr, State, (OperatorGroup_t)i))
			return -1;
	}

	/* Generate cleanup statement? */
	if (State->GenerateCleanUp) {
		m_pPool->AddOpcode(State->CodeScopeId, OpStoreAR);
		m_pPool->AddCode32(State->CodeScopeId, State->ActiveReference);
		m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);

#ifdef DIAGNOSE
		printf("storear #%i, $%i\n", State->ActiveReference, State->ActiveRegister);
#endif

		/* Deallocate */
		DeallocateRegister(State->ActiveRegister);
	}

	return 0;
}

/* This is a recursive expression parser 
 * used for turning AST expressions into bytecode */
int Generator::ParseExpression(Expression *pExpr, GenState_t *State, OperatorGroup_t Group) {

	/* Sanity */
	if (pExpr == NULL
		|| pExpr->IsSolved()) {
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

			/* Sanity, don't parse us 
			 * unless we are asked for non operators */
			if (Group != OpGroupSingles) {
				return 0;
			}

			/* Generate some code */
			if ((State->IntermediateRegister != -1
				|| State->ActiveRegister != -1)) {

				/* Select register */
				int Register = State->IntermediateRegister;

				/* Sanity */
				if (Register == -1)
					Register = State->ActiveRegister;

				/* We are working in a temporary register
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadRA);
				m_pPool->AddCode8(State->CodeScopeId, Register);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loadra $%i, #%i\n", Register, Id);
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

			/* Set us to solved */
			pExpr->SetSolved();

		} break;

		/* String Literal? */
		case ExprString: {

			/* Cast to correct expression type */
			StringValue *String = (StringValue*)pExpr;

			/* Define it in our data-pool */
			int Id = m_pPool->DefineString(String->GetValue());

			/* Sanity, don't parse us 
			 * unless we are asked for non operators */
			if (Group != OpGroupSingles) {
				return 0;
			}

			/* Generate some code */
			if ((State->IntermediateRegister != -1
				|| State->ActiveRegister != -1)) {

				/* Select register */
				int Register = State->IntermediateRegister;

				/* Sanity */
				if (Register == -1)
					Register = State->ActiveRegister;

				/* We are working in a temporary register
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpLoadRA);
				m_pPool->AddCode8(State->CodeScopeId, Register);
				m_pPool->AddCode32(State->CodeScopeId, Id);

#ifdef DIAGNOSE
				printf("loadra $%i, #%i\n", Register, Id);
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

			/* Set us to solved */
			pExpr->SetSolved();

		} break;

		/* Int Literal? */
		case ExprInteger: {

			/* Cast to correct expression type */
			IntValue *Int = (IntValue*)pExpr;

			/* Sanity, don't parse us 
			 * unless we are asked for non operators */
			if (Group != OpGroupSingles) {
				return 0;
			}

			/* Generate some code */
			if ((State->IntermediateRegister != -1
				|| State->ActiveRegister != -1)) {

				/* Select register */
				int Register = State->IntermediateRegister;

				/* Sanity */
				if (Register == -1)
					Register = State->ActiveRegister;

				/* We are working in a temporary register
				* use appropriate instructions */
				m_pPool->AddOpcode(State->CodeScopeId, OpStoreRI);
				m_pPool->AddCode8(State->CodeScopeId, Register);
				m_pPool->AddCode32(State->CodeScopeId, Int->GetValue());

#ifdef DIAGNOSE
				printf("storeri $%i, [%i]\n", Register, Int->GetValue());
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

			/* Set us to solved */
			pExpr->SetSolved();

		} break;

		/* Binary Expression?? */
		case ExprBinary: {

			/* Cast to correct expression type */
			BinaryExpression *BinExpr = (BinaryExpression*)pExpr;

			/* Are we in the correct group? */
			if (Group == OpGroup3
				&& (BinExpr->GetOperator() == ExprOperatorDivide
				|| BinExpr->GetOperator() == ExprOperatorMultiply)) {
				goto ParseUs;
			}
			else if (Group == OpGroup4
				&& (BinExpr->GetOperator() == ExprOperatorAdd
				|| BinExpr->GetOperator() == ExprOperatorSubtract)) {
				goto ParseUs;
			}
			else {
				return ParseExpression(BinExpr->GetExpression2(), State, Group);
			}

		ParseUs:
			/* Use previous state if there is any */
			if (State->ActiveRegister == -1) {

				/* Allocate a temporary register 
				 * for calculations */
				State->ActiveRegister = AllocateRegister();

				/*************************************
				 ***** LEFT - HAND - EVALUATION ******
				 *************************************/

				/* First of all, if our left hand is a binary expression
				 * this means we should calculate our left hand tree before
				 * proceeding. For that we need a new intermediate state */
				if (BinExpr->GetExpression1()->GetType() == ExprBinary) {

					/* Create a new intermediate state */
					GenState_t TempEnvironment;

					/* Instantiate it */
					TempEnvironment.CodeScopeId = State->CodeScopeId;
					TempEnvironment.IntermediateRegister = -1;
					TempEnvironment.ActiveReference = -1;
					TempEnvironment.ActiveRegister = -1;
					TempEnvironment.GenerateCleanUp = 0;

					/* Parse the left hand */
					for (int i = 0; i < (int)OperatorGroupCount; i++) {
						if (ParseExpression(BinExpr->GetExpression1(), &TempEnvironment, (OperatorGroup_t)i))
							return -1;
					}

					/* Generate cleanup statement? */
					if (TempEnvironment.GenerateCleanUp) {
						m_pPool->AddOpcode(State->CodeScopeId, OpStore);
						m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
						m_pPool->AddCode8(State->CodeScopeId, TempEnvironment.ActiveRegister);

#ifdef DIAGNOSE
						printf("store #%i, $%i\n", State->ActiveRegister, TempEnvironment.ActiveRegister);
#endif

						/* Deallocate */
						DeallocateRegister(TempEnvironment.ActiveRegister);
					}

					/* Mark expression as solved */
					BinExpr->GetExpression1()->SetSolved();
				}
				else {
					/* Simple stuff actually
					 * this means we can directly load the left hand value in */
					if (ParseExpression(BinExpr->GetExpression1(), State, OpGroupSingles)) {
						return -1;
					}
				}

				/* Remember to cleanup */
				State->GenerateCleanUp = 1;
			}

			/* Allocate an intermediate register */
			State->IntermediateRegister = AllocateRegister();

			/*************************************
			 ***** RIGHT - HAND - EVALUATION *****
			 *************************************/

			/* First of all, if right hand is a binary expression
			 * we have two cases that can happen, however if it is not
			 * a binary expression, we have reached end of expression list
			 * and such it's pretty easy to handle */
			if (BinExpr->GetExpression2()->GetType() == ExprBinary) {
				
				/* For convienance, we need a handle to lower expression
				 * Cast to correct expression type */
				BinaryExpression *LowerBinExpr = (BinaryExpression*)BinExpr->GetExpression2();

				/* Now we have two possible cases, either the left hand of
				 * this expression is actually ALSO a binary expression,
				 * in which case we actually need to parse it */
				if (LowerBinExpr->GetExpression1()->GetType() == ExprBinary) {
					
					/* Create a temporary environment */
					GenState_t TempEnvironment;

					/* Deallocate the intermediate register */
					DeallocateRegister(State->IntermediateRegister);

					/* Instantiate it */
					TempEnvironment.CodeScopeId = State->CodeScopeId;
					TempEnvironment.IntermediateRegister = -1;
					TempEnvironment.ActiveReference = -1;
					TempEnvironment.ActiveRegister = -1;
					TempEnvironment.GenerateCleanUp = 0;

					/* Parse the left hand */
					for (int i = 0; i < (int)OperatorGroupCount; i++) {
						if (ParseExpression(LowerBinExpr->GetExpression1(), &TempEnvironment, (OperatorGroup_t)i))
							return -1;
					}

					/* Update the new intermediate */
					State->IntermediateRegister = TempEnvironment.ActiveRegister;

					/* Mark expression as solved, skip it forever */
					LowerBinExpr->GetExpression1()->SetSolved();
				}
				else {
					/* Or, it's simply a single expression in which case it's pretty easy */
					if (ParseExpression(LowerBinExpr->GetExpression1(), State, OpGroupSingles)) {
						return -1;
					}
				}
			}
			else {
				/* Ok, end of chain, load second into intermediate 
				 * generate the last statements */
				if (ParseExpression(BinExpr->GetExpression2(), State, OpGroupSingles)) {
					return -1;
				}
			}

			/*************************************
			 ****** OPERATOR CODE APPENDUM *******
			 *************************************/

			/* Generate code for both active 
			 * intermediate code */
			if (BinExpr->GetOperator() == ExprOperatorAdd) {
				m_pPool->AddOpcode(State->CodeScopeId, OpAdd);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode8(State->CodeScopeId, State->IntermediateRegister);

#ifdef DIAGNOSE
				printf("add $%i, $%i\n", State->ActiveRegister, State->IntermediateRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorSubtract) {
				m_pPool->AddOpcode(State->CodeScopeId, OpSub);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode8(State->CodeScopeId, State->IntermediateRegister);

#ifdef DIAGNOSE
				printf("sub $%i, $%i\n", State->ActiveRegister, State->IntermediateRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorMultiply) {
				m_pPool->AddOpcode(State->CodeScopeId, OpMul);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode8(State->CodeScopeId, State->IntermediateRegister);

#ifdef DIAGNOSE
				printf("mul $%i, $%i\n", State->ActiveRegister, State->IntermediateRegister);
#endif
			}
			else if (BinExpr->GetOperator() == ExprOperatorDivide) {
				m_pPool->AddOpcode(State->CodeScopeId, OpDiv);
				m_pPool->AddCode8(State->CodeScopeId, State->ActiveRegister);
				m_pPool->AddCode8(State->CodeScopeId, State->IntermediateRegister);

#ifdef DIAGNOSE
				printf("div $%i, $%i\n", State->ActiveRegister, State->IntermediateRegister);
#endif
			}

			/* Free the intermediate */
			DeallocateRegister(State->IntermediateRegister);
			State->IntermediateRegister = -1;

			/* Recursive down */
			if (BinExpr->GetExpression2()->GetType() == ExprBinary) {
				if (ParseExpression(BinExpr->GetExpression2(), State, Group)) {
					return -1;
				}
			}

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
