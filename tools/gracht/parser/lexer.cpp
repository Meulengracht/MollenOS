/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "lexer.hpp"


GrachtAST::GrachtAST(const std::string& Path)
    : m_Parser(Path)
{
}

bool GrachtAST::IsValid()
{
    if (m_Parser.IsValid()) {
        return VerifyAST();
    }
    return false;
}

bool GrachtAST::ParseTokens()
{
    auto Tokens = m_Parser.GetTokens();
    
}

bool GrachtAST::VerifyAST()
{
    return false;
}
