/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "expression.h"
#include <string>

class Statement
{
public:
    enum class StatementType {
        Declaration,
        Object,
        Function,
        Sequence
    };
public:
	Statement(StatementType Type) 
        : m_Type(Type) { }
	virtual ~Statement() {}

	StatementType GetType() { return m_Type; }

private:
	StatementType m_Type;
};

class Declaration : public Statement
{
public:
	Declaration(const char *pOfType, const char *pIdentifier) : Statement(StmtDeclaration) {
		m_pIdentifier = strdup(pIdentifier);
		m_pOfType = strdup(pOfType);
		m_pExpression = NULL;
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