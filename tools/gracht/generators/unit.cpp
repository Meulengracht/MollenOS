/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "unit.hpp"

GrachtUnit::GrachtUnit(const std::string& Path, std::shared_ptr<Language> CodeLanguage)
    : m_AST(Path, CodeLanguage)
{
}

bool GrachtUnit::IsValid()
{
    return m_AST.IsValid();
}

bool GrachtUnit::ParseAST()
{
    auto RootAST = m_AST.GetRootStatement();

    return false;
}

bool GrachtUnit::ResolveUsing()
{
    return false;
}
