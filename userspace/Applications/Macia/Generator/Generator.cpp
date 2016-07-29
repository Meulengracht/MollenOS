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

	/* Store */
	m_pAST = AST;
}

/* Destructor 
 * Does nothing for now */
Generator::~Generator() {

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


	/* Just return the result of Parser */
	return ParseStatement(m_pAST, -1);
}

/* The actual statement parser
 * This is the recursive function */
int Generator::ParseStatement(Statement *pStmt, int ScopeId) {

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

			/* Parse the expression */
			if (ParseExpression(Decl->GetExpression(), Id)) {
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

			/* Parse the expression */
			if (ParseExpression(Ass->GetExpression(), Id)) {
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
int Generator::ParseExpression(Expression *pExpr, int ScopeId) {

	/* Sanity */
	if (pExpr == NULL) {
		return 0;
	}

	/* Determine what kind of expression .. */
	switch (pExpr->GetType()) {

		/* Variable ? */
		case ExprVariable: {

		} break;

		/* String Literal? */
		case ExprString: {

		} break;

		/* Int Literal? */
		case ExprInteger: {

		} break;

		/* Binary Expression?? */
		case ExprBinary: {

			/* Cast to correct expression type */
			BinaryExpression *BinExpr = (BinaryExpression*)pExpr;

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