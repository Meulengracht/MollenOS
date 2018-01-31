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

/* Includes */
#include "parser.h"
#include <cstdio>
#include <cstring>
#include <strings.h>

/* Constructor
 * Takes a elem list for parsing */
Parser::Parser(std::vector<Element*> &Elements) {
	m_lElements = Elements;
	m_pBase = NULL;
}

/* Destructor
 * Cleanup list */
Parser::~Parser() {
	m_lElements.clear();
	
	/* Cleanup AST */
	if (m_pBase != NULL) {
		delete m_pBase;
	}
}

/* This runs the actual parsing 
 * process, use GetProgram to retrieve
 * the results */
int Parser::Parse() 
{
	/* Variables */
	int Count = 0;

	/* Iterate tokens */
	for (Count = 0; Count < (int)m_lElements.size();)
	{
		/* Get a reference */
		Element *elem = m_lElements[Count];

		/* Remember we are in the outer world 
		 * which means we only accept outer-world identifiers */
		switch (elem->GetType()) {
			case Identifier: {

				/* Good, this we can expect 
				 * Which type of identifier? 
				 * We accept only VERY few */
				int Increase = ParseStatement(Count, &m_pBase);
				Count += Increase;

				/* Sanity */
				if (!Increase) {
					/* Print an error message */
					printf("Invalid identifier %s at line %i\n", elem->GetName(), elem->GetLineNumber());

					/* Bail out */
					return -1;
				}

			} break;

			/* Ignore comments */
			case CommentBlock:
			case CommentLine: {
				Count++;
			} break;

			default: {
				/* Print an error message */
				printf("Invalid element %s at line %i, expected Identifier, Index %i\n", 
					elem->GetName(), elem->GetLineNumber(), Count);

				/* Bail out */
				return -1;
			}
		}
	}

	/* Done ! */
	return 0;
}

/* Parse elements into an AST statement */
int Parser::ParseStatement(int Index, Statement **Parent)
{
	/* Keep track of elements consumed */
	Statement *Stmt = NULL;
	int Consumed = 0;
	int ModIndex = Index;
	int Modifiers = 0;

	/* Start out by passing modifiers */
	ModIndex += Consumed = ParseModifiers(ModIndex, &Modifiers);

	/* Filter out comments */
	if (m_lElements[ModIndex]->GetType() == CommentLine
		|| m_lElements[ModIndex]->GetType() == CommentBlock) {
		Consumed++;
	}
	/* Now let's see what we can do with this 
	 * Is it a declaration?? */
	else if ((m_lElements[ModIndex]->GetType() == Identifier
			&& m_lElements[ModIndex + 1]->GetType() == Identifier
			&& m_lElements[ModIndex + 2]->GetType() == OperatorAssign)
		|| (m_lElements[ModIndex]->GetType() == Identifier
			&& m_lElements[ModIndex + 1]->GetType() == Identifier
			&& m_lElements[ModIndex + 2]->GetType() == OperatorSemiColon)) {
		
		/* Create a new statement */
		Declaration *Decl = new Declaration(m_lElements[ModIndex]->GetData(), 
			m_lElements[ModIndex + 1]->GetData());
		int HasExpr = (m_lElements[ModIndex + 2]->GetType() == OperatorSemiColon) ? 0 : 1;

		/* Modify index + consumed */
		Consumed += 3;
		ModIndex += 3;

		/* Parse expression */
		if (HasExpr) {
			Expression *Expr = NULL;
			int Used = ParseExpression(ModIndex, &Expr);
			Decl->SetExpression(Expr);

			/* Skip ';' */
			Used++;

			/* Increase by consumed elements */
			Consumed += Used;
			ModIndex += Used;
		}

		/* Set it  */
		Stmt = Decl;
	}
	/* Assign statement */
	else if ((m_lElements[ModIndex]->GetType() == Identifier
		&& m_lElements[ModIndex + 1]->GetType() == OperatorAssign)) {

		/* Create a new statement */
		Assignment *Ass = new Assignment(m_lElements[ModIndex]->GetData());
		Expression *Expr = NULL;
		int Used = 0;

		/* Modify index + consumed */
		Consumed += 2;
		ModIndex += 2;

		/* Parse expression */
		Used = ParseExpression(ModIndex, &Expr);
		Ass->SetExpression(Expr);

		/* Skip ';' */
		Used++;

		/* Increase by consumed elements */
		Consumed += Used;
		ModIndex += Used;

		/* Set it  */
		Stmt = Ass;
	}
	else if (m_lElements[ModIndex]->GetType() == Identifier
		&& m_lElements[ModIndex + 1]->GetType() == Identifier
		&& (m_lElements[ModIndex + 2]->GetType() == LeftFuncBracket
			|| m_lElements[ModIndex + 2]->GetType() == LeftParenthesis)) {
		
		/* Function declaration, object declaration */
		if (!strcasecmp("object", m_lElements[ModIndex]->GetData())) {

			/* Create a new Object and parse it's body */
			Object *Obj = new Object(m_lElements[ModIndex + 1]->GetData());
			Statement *Body = NULL;
			int Used = 0;

			/* Consume */
			ModIndex += 3;
			Used = 3;

			/* Keep parsing statements till end of body */
			while (m_lElements[ModIndex]->GetType() != RightFuncBracket) {
				/* Parse */
				int StmtLength = ParseStatement(ModIndex, &Body);

				/* Update */
				ModIndex += StmtLength;
				Used += StmtLength;
			}

			/* Skip the end of body */
			Used++;

			/* Update body */
			Obj->SetBody(Body);

			/* Increase */
			Consumed += Used;

			/* Set it  */
			Stmt = Obj;
		}
		else if (!strcasecmp("func", m_lElements[ModIndex]->GetData())) {

			/* Create a new Object and parse it's body */
			Function *Func = new Function(m_lElements[ModIndex + 1]->GetData());
			Statement *Body = NULL;
			int Used = 0;

			/* Consume */
			ModIndex += 3;
			Used = 3;

			/* Keep parsing arguments */
			while (m_lElements[ModIndex]->GetType() != RightParenthesis) {
				/* Update */
				ModIndex += 1;
				Used += 1;
			}

			/* Skip the end of arugments */
			ModIndex++;
			Used++;

			/* Validate */
			if (m_lElements[ModIndex]->GetType() != LeftFuncBracket) {
				/* ERROR */
				printf("Unsupported start of function: <%s>, line %u. Expected '{' \n",
					m_lElements[ModIndex]->GetName(), m_lElements[ModIndex]->GetLineNumber());
			}
			
			/* Skip this too */
			ModIndex++;
			Used++;

			/* Keep parsing statements till end of body */
			while (m_lElements[ModIndex]->GetType() != RightFuncBracket) {
				/* Parse */
				int StmtLength = ParseStatement(ModIndex, &Body);

				/* Update */
				ModIndex += StmtLength;
				Used += StmtLength;
			}

			/* Skip this too */
			Used++;

			/* Update body */
			Func->SetBody(Body);

			/* Increase */
			Consumed += Used;

			/* Set it  */
			Stmt = Func;
		}
	}
	else {
		/* Invalid - ERROR - ERRROR */
		printf("Unsupported start of statement <%s: %s>, line %u\n",
			m_lElements[ModIndex]->GetName(), m_lElements[ModIndex]->GetData(), m_lElements[ModIndex]->GetLineNumber());
		Consumed++;
	}

	/* Add it? */
	if (Stmt != NULL) {
		if (*Parent == NULL) {
			*Parent = Stmt;
		}
		else {
			/* Create a sequence and add us in */
			Sequence *Seq = new Sequence(*Parent, Stmt);
			*Parent = Seq;
		}
	}
 
	/* Done! */
	return Consumed;
}

/* Parse statement modifers */
int Parser::ParseModifiers(int Index, int *Modifiers)
{
	/* Keep track of elements consumed */
	int Consumed = 0;
	int ModIndex = Index;

	/* Determine what kind of statement this is
	 * Start out by checking decl */
	while (m_lElements[ModIndex]->GetType() == Identifier) {
		if (!strcasecmp("const", m_lElements[ModIndex]->GetData())) {
			ModIndex++;
			Consumed++;
		}
		else if (!strcasecmp("locked", m_lElements[ModIndex]->GetData())) {
			ModIndex++;
			Consumed++;
		}
		else
			break;
	}

	/* Done! */
	return Consumed;
}

/* Parse an AST expression from the given index 
 * returns how many elements were consumed in the process */
int Parser::ParseExpression(int Index, Expression **Parent)
{
	/* Keep track of elements consumed */
	Expression *Expr = NULL;
	int Consumed = 0;
	int ModIndex = Index;

	/* Iterate untill end of expression */
	while (m_lElements[ModIndex]->GetType() != OperatorSemiColon
		&& m_lElements[ModIndex]->GetType() != RightParenthesis) {

		/* Used elements */
		int Used = 0;

		/* Now, let's check... */
		if (m_lElements[ModIndex]->GetType() == LeftParenthesis) {

			/* Consume the left paranthesis */
			Used++;

			/* Parse sub-expression in expression */
			Used += ParseExpression(ModIndex + 1, &Expr);

			/* Skip the right parenthesis */
			Used++;
		}
		else if (m_lElements[ModIndex]->GetType() == StringLiteral) {

			/* Create a new string value object */
			StringValue *StrVal = new StringValue(m_lElements[ModIndex]->GetData());

			/* Save it */
			Expr = StrVal;
			Used++;
		}
		else if (m_lElements[ModIndex]->GetType() == DigitLiteral) {

			/* Create a new digit value object */
			IntValue *IntVal = new IntValue(m_lElements[ModIndex]->GetData());

			/* Save it */
			Expr = IntVal;
			Used++;
		}
		else if (m_lElements[ModIndex]->GetType() == Identifier
			&& m_lElements[ModIndex + 1]->GetType() == LeftParenthesis) {
			/* Function calls in expressions 
			 * not really supported atm */
			printf("Functions calls in expressions are currently unsupported, line %u\n",
				m_lElements[ModIndex]->GetLineNumber());
		}
		else if (m_lElements[ModIndex]->GetType() == Identifier) {

			/* Create a new variable value object */
			Variable *Var = new Variable(m_lElements[ModIndex]->GetData());
			
			/* Save it */
			Expr = Var;
			Used++;
		}
		/* Now we check for operators!! 
		 * operators are allowed, we just need to be careful */
		else if (m_lElements[ModIndex]->GetType() == OperatorAdd
			|| m_lElements[ModIndex]->GetType() == OperatorSubtract
			|| m_lElements[ModIndex]->GetType() == OperatorDivide
			|| m_lElements[ModIndex]->GetType() == OperatorMultiply) {

			/* Do the sanity check, no + operators to start with */
			if (Expr == NULL
				&& m_lElements[ModIndex]->GetType() != OperatorSubtract) {
				printf("Expression cannot be started with element of type %s, line %u\n",
					m_lElements[ModIndex]->GetName(), m_lElements[ModIndex]->GetLineNumber());
			}

			/* Determine correct expr-operator */
			ExpressionBinaryOperator_t ExprOperator;
			BinaryExpression *Binary = NULL;
			Expression *Expr2 = NULL;

			/* Switch.. */
			if (m_lElements[ModIndex]->GetType() == OperatorAdd)
				ExprOperator = ExprOperatorAdd;
			else if (m_lElements[ModIndex]->GetType() == OperatorSubtract)
				ExprOperator = ExprOperatorSubtract;
			else if (m_lElements[ModIndex]->GetType() == OperatorDivide)
				ExprOperator = ExprOperatorDivide;
			else if (m_lElements[ModIndex]->GetType() == OperatorMultiply)
				ExprOperator = ExprOperatorMultiply;
            else {
                // @todo
                printf("Expressions");
                return 0;
            }

			/* Ok, create a new expression */
			Binary = new BinaryExpression(ExprOperator);

			/* Take the already existing expression, set it as left hand */
			Binary->SetExpression1(Expr);

			/* Skip this element */
			Used++;

			/* Parse the next expression */
			Used += ParseExpression(ModIndex + 1, &Expr2);

			/* Set second expression */
			Binary->SetExpression2(Expr2);

			/* Update */
			Expr = Binary;
		}
		else {
			/* ERROR - ERRROR - ABBOOOOORT */
			printf("Element of type %s in expressions are currently unsupported, line %u\n",
				m_lElements[ModIndex]->GetName(), m_lElements[ModIndex]->GetLineNumber());

			/* Skip */
			Used++;
		}

		/* Consumeeee */
		Consumed += Used;
		ModIndex += Used;
	}

	/* Update parent */
	*Parent = Expr;

	/* Done ! */
	return Consumed;
}