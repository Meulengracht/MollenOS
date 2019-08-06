/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include <string>

class Expression
{
public:
    enum class ExpressionType {
        Variable,
        String,
        Integer
    };
public:
	Expression(ExpressionType Type) 
        : m_Type(Type) { }
	virtual ~Expression() {}

	ExpressionType GetType() { return m_Type; }

private:
	ExpressionType m_Type;
};

class VariableExpression : public Expression
{
public:
	VariableExpression(const std::string& Identifier) 
        : Expression(ExpressionType::Variable), m_Identifier(Identifier) { }

	const std::string& GetIdentifier() { return m_Identifier; }

private:
	std::string m_Identifier;
};

class StringExpression : public Expression
{
public:
	StringExpression(const std::string& Value) 
        : Expression(ExpressionType::String), m_Value(Value) { }

	const std::string& GetValue() { return m_Value; }

private:
	std::string m_Value;
};

class IntegerExpression : public Expression
{
public:
	IntegerExpression(const std::string& Value) 
        : Expression(ExpressionType::String), m_Value(std::stoi(Value)) { }

	int GetValue() { return m_Value; }

private:
	int m_Value;
};
