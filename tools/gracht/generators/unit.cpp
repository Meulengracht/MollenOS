/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "unit.hpp"

GrachtUnit::GrachtUnit(const std::string& Path, std::shared_ptr<Language> CodeLanguage)
    : m_AST(Path, CodeLanguage)
{
}

bool GrachtUnit::IsValid()
{
    if (m_AST.IsValid()) {
        return ParseAST();
    }
    return false;
}

bool GrachtUnit::ParseAST()
{
    return false;
}

bool GrachtUnit::ResolveUsing()
{
    return false;
}
