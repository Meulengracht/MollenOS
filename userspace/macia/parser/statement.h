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
* Macia - Parser Statement
* - This class describes the base of an statement
* - A statement represents an action, sometimes a line of actions
*/
#pragma once

/* Includes */
#include "expression.h"
#include <cstring>
#include <cstdlib>

/* Statement Types */
typedef enum
{
	StmtDeclaration,

	StmtObject,
	StmtFunction,

	StmtAssign,
	StmtCall,

	/* This is the sequence
	 * it chains together actions */
	StmtSequence

} StatementType_t;

/* The base-class
 * A statement is the base class */
class Statement
{
public:
	Statement(StatementType_t Type) { m_eType = Type; }
	virtual ~Statement() {}

	/* Type of expression */
	StatementType_t GetType() { return m_eType; }

private:
	/* Private - Data */
	StatementType_t m_eType;
};

/* The declaration class 
 * This describes a variable declaration/instantiation */
class Declaration : public Statement
{
public:
	Declaration(const char *pOfType, const char *pIdentifier) : Statement(StmtDeclaration) {
		m_pIdentifier = strdup(pIdentifier);
		m_pOfType = strdup(pOfType);
		m_pExpression = NULL;
	}
	~Declaration() {
		free((void*)m_pIdentifier);
	}

	/* Update expression */
	void SetExpression(Expression *pExpression) {
		m_pExpression = pExpression;
	}

	/* Gets */
	const char *GetOfType() { return m_pOfType; }
	const char *GetIdentifier() { return m_pIdentifier; }
	Expression *GetExpression() { return m_pExpression; }

private:
	const char *m_pOfType;
	const char *m_pIdentifier;
	Expression *m_pExpression;
};

/* The assignment class
 * This describes a variable assignment */
class Assignment : public Statement
{
public:
	Assignment(const char *pIdentifier) : Statement(StmtAssign) {
		m_pIdentifier = strdup(pIdentifier);
		m_pExpression = NULL;
	}
	~Assignment() {
		free((void*)m_pIdentifier);
	}

	/* Update expression */
	void SetExpression(Expression *pExpression) {
		m_pExpression = pExpression;
	}

	/* Gets */
	const char *GetIdentifier() { return m_pIdentifier; }
	Expression *GetExpression() { return m_pExpression; }

private:
	const char *m_pIdentifier;
	Expression *m_pExpression;
};

/* The object class
 * This describes an Macia-Object */
class Object : public Statement
{
public:
	Object(const char *pIdentifier) : Statement(StmtObject) {
		m_pIdentifier = strdup(pIdentifier);
		m_pBody = NULL;
	}
	~Object() {
		free((void*)m_pIdentifier);
		if (m_pBody != NULL) {
			delete m_pBody;
		}
	}

	/* Update body - statement */
	void SetBody(Statement *pStmt) {
		m_pBody = pStmt;
	}

	/* Gets */
	const char *GetIdentifier() { return m_pIdentifier; }
	Statement *GetBody() { return m_pBody; }

private:
	const char *m_pIdentifier;
	Statement *m_pBody;
};

/* The function class
 * This describes an Macia-Function */
class Function : public Statement
{
public:
	Function(const char *pIdentifier) : Statement(StmtFunction) {
		m_pIdentifier = strdup(pIdentifier);
		m_pBody = NULL;
	}
	~Function() {
		free((void*)m_pIdentifier);
		if (m_pBody != NULL) {
			delete m_pBody;
		}
	}

	/* Update arguments, variable list */

	/* Update body - statement */
	void SetBody(Statement *pStmt) {
		m_pBody = pStmt;
	}

	/* Gets */
	const char *GetIdentifier() { return m_pIdentifier; }
	Statement *GetBody() { return m_pBody; }

private:
	const char *m_pIdentifier;
	Statement *m_pBody;
};

/* The sequence class
 * This describes an statement sequence */
class Sequence : public Statement
{
public:
	Sequence(Statement *Stmt1, Statement *Stmt2) 
		: Statement(StmtSequence) {
		m_pStmt1 = Stmt1;
		m_pStmt2 = Stmt2;
	}
	~Sequence() {
		if (m_pStmt1 != NULL) {
			delete m_pStmt1;
		}
		if (m_pStmt2 != NULL) {
			delete m_pStmt2;
		}
	}

	/* Gets */
	Statement *GetStatement1() { return m_pStmt1; }
	Statement *GetStatement2() { return m_pStmt2; }

private:
	Statement *m_pStmt1;
	Statement *m_pStmt2;
};