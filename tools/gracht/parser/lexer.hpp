/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "expression.hpp"
#include "statement.hpp"
#include "parser.hpp"
#include "language.hpp"
#include <memory>
#include <string>

class GrachtAST {
private:
    enum class ASTScope {
        Global,
        Object,
        Enum
    };

public:
    GrachtAST(const std::string& Path, std::shared_ptr<Language>& ASTLanguage);

    bool IsValid();

private:
    bool                       ParseTokens();
    std::shared_ptr<Statement> GetNextStatement();
    bool                       VerifyAST();

private:
    GrachtParser               m_Parser;
    std::shared_ptr<Language>  m_Language;
    std::shared_ptr<Statement> m_RootNode;
    ASTScope                   m_CurrentScope;
};
