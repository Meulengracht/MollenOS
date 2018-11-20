/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "expression.h"
#include <memory>
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
        : m_Type(Type), m_Child(nullptr) { }
	virtual ~Statement() {}

    void SetChildStatement(std::shared_ptr<Statement> Stmt) { m_Child = Stmt; }

    const std::shared_ptr<Statement>& GetChildStatement() { return m_Child; }
	StatementType                     GetType()           { return m_Type; }

private:
	StatementType              m_Type;
    std::shared_ptr<Statement> m_Child;
};

class Declaration : public Statement
{
public:
	Declaration(const std::string& Type, const std::string& Identifier) 
        : Statement(StatementType::Declaration), m_OfType(Type), m_Identifier(Identifier) { }

	const std::string& GetOfType()     { return m_OfType; }
	const std::string& GetIdentifier() { return m_Identifier; }

private:
	std::string m_OfType;
	std::string m_Identifier;
};

class Object : public Statement
{
public:
	Object(const std::string& Identifier) 
        : Statement(StatementType::Object), m_Identifier(Identifier) { }

	const std::string& GetIdentifier() { return m_pIdentifier; }

private:
	std::string& m_Identifier;
};

class Function : public Statement
{
public:
	Function(const std::string& Identifier) 
        : Statement(StatementType::Function), m_Identifier(Identifier) { }

	const std::string& GetIdentifier() { return m_pIdentifier; }

private:
	std::string& m_Identifier;
};

class Sequence : public Statement
{
public:
	Sequence(std::shared_ptr<Statement> Stmt1, std::shared_ptr<Statement> Stmt2) 
		: Statement(StatementType::Sequence), m_Stmt1(Stmt1), m_Stmt2(Stmt2) {	}

	const std::shared_ptr<Statement>& GetStatement1() { return m_pStmt1; }
	const std::shared_ptr<Statement>& GetStatement2() { return m_pStmt2; }

private:
	std::shared_ptr<Statement> m_Stmt1;
	std::shared_ptr<Statement> m_Stmt2;
};
