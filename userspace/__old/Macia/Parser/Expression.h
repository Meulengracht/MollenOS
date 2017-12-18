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
* Macia - Parser Expression
* - This class describes the base of an expression
* - An expression is a variable, a value, a string etc
*/
#pragma once

/* Includes */
#include <cstring>
#include <cstdlib>

/* Expression Types */
typedef enum
{
	ExprVariable,

	ExprString,
	ExprInteger,

	ExprBinary

} ExpressionType_t;

/* Expression Binary Operator Types */
typedef enum 
{
	ExprOperatorAdd,
	ExprOperatorSubtract,
	ExprOperatorMultiply,
	ExprOperatorDivide

} ExpressionBinaryOperator_t;

/* The base-class
 * An expression is the base class */
class Expression
{
public:
	Expression(ExpressionType_t Type) { m_eType = Type; m_iSolved = 0; }
	virtual ~Expression() {}

	/* Sets */
	void SetSolved() { m_iSolved = 1; }

	/* Gets */
	ExpressionType_t GetType() { return m_eType; }
	int IsSolved() { return m_iSolved; }

private:
	/* Private - Data */
	ExpressionType_t m_eType;
	int m_iSolved;
};

/* A variable, this can pretty much be a reference
 * to anything */
class Variable : public Expression
{
public:
	/* Variable Constructor 
	 * Set type and create a copy of the name */
	Variable(const char *pIdentifier) : Expression(ExprVariable) {
		m_pIdentifier = strdup(pIdentifier);
	}

	/* Variable Deconstructor
	 * Handle cleanup */
	~Variable() {
		free(m_pIdentifier);
	}
	
	/* Gets */
	char *GetIdentifier() { return m_pIdentifier; }

private:
	/* Private - Data*/
	char *m_pIdentifier;
};

/* A string literal, this is a string value */
class StringValue : public Expression
{
public:
	/* Variable Constructor 
	 * Set type and create a copy of the name */
	StringValue(const char *pValue) : Expression(ExprString) {
		m_pValue = strdup(pValue);
	}

	/* Variable Deconstructor
	 * Handle cleanup */
	~StringValue() {
		free(m_pValue);
	}
	
	/* Gets */
	char *GetValue() { return m_pValue; }

private:
	/* Private - Data*/
	char *m_pValue;
};

/* A int literal, this is a int value */
class IntValue : public Expression
{
public:
	/* Variable Constructor
	 * Set type and create a copy of the name */
	IntValue(const char *pValue) : Expression(ExprInteger) {
		m_iValue = atoi(pValue);
	}

	/* Variable Deconstructor
	 * Handle cleanup */
	~IntValue() { }

	/* Gets */
	int GetValue() { return m_iValue; }

private:
	/* Private - Data*/
	int m_iValue;
};

/* A binary expression, this is primarily used 
 * for expressions that contain operators! */
class BinaryExpression : public Expression
{
public:
	/* Variable Constructor
	* Set type and create a copy of the name */
	BinaryExpression(ExpressionBinaryOperator_t Type) 
		: Expression(ExprBinary) {
		m_eType = Type;
		m_pExpr1 = NULL;
		m_pExpr2 = NULL;
	}

	/* Variable Deconstructor
	* Handle cleanup */
	~BinaryExpression() {
		if (m_pExpr1 != NULL)
			delete m_pExpr1;
		if (m_pExpr2 != NULL)
			delete m_pExpr2;
	}

	/* Update expressions */
	void SetExpression1(Expression *Expr) {
		m_pExpr1 = Expr;
	}
	void SetExpression2(Expression *Expr) {
		m_pExpr2 = Expr;
	}

	/* Gets */
	ExpressionBinaryOperator_t GetOperator() { return m_eType; }
	Expression *GetExpression1() { return m_pExpr1; }
	Expression *GetExpression2() { return m_pExpr2; }

private:
	/* Private - Data*/
	ExpressionBinaryOperator_t m_eType;
	Expression *m_pExpr1;
	Expression *m_pExpr2;
};