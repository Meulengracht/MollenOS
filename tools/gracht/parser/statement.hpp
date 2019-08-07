/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "expression.hpp"
#include <memory>
#include <string>

class Statement
{
public:
    enum class StatementType {
        Using,
        Namespace,
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

    std::shared_ptr<Statement> GetChildStatement() { return m_Child; }
	StatementType              GetType()           { return m_Type; }

private:
	StatementType              m_Type;
    std::shared_ptr<Statement> m_Child;
};

class UsingModule : public Statement
{
public:
	UsingModule(const std::string& Module) 
        : Statement(StatementType::Using), m_Module(Module) { }

	const std::string& GetModule() { return m_Module; }

private:
	std::string m_Module;
};

class Namespace : public Statement
{
public:
	Namespace(const std::string& Namespace) 
        : Statement(StatementType::Namespace), m_Namespace(Namespace) { }

	const std::string& GetNamespace() { return m_Namespace; }

private:
	std::string m_Namespace;
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

	const std::string& GetIdentifier() { return m_Identifier; }

private:
	std::string m_Identifier;
};

class Function : public Statement
{
public:
	Function(const std::string& Identifier, const std::string& ReturnType) 
        : Statement(StatementType::Function), 
          m_Identifier(Identifier), m_ReturnType(ReturnType) { }

	const std::string& GetIdentifier() { return m_Identifier; }
	const std::string& GetReturnType() { return m_ReturnType; }

private:
	std::string m_Identifier;
    std::string m_ReturnType;
};

class Sequence : public Statement
{
public:
	Sequence(std::shared_ptr<Statement> Stmt1, std::shared_ptr<Statement> Stmt2) 
		: Statement(StatementType::Sequence), m_Stmt1(Stmt1), m_Stmt2(Stmt2) {	}

	const std::shared_ptr<Statement>& GetStatement1() const { return m_Stmt1; }
	const std::shared_ptr<Statement>& GetStatement2() const { return m_Stmt2; }

private:
	std::shared_ptr<Statement> m_Stmt1;
	std::shared_ptr<Statement> m_Stmt2;
};
