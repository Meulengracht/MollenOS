/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../common/file.hpp"
#include "token.hpp"
#include <list>

class GrachtParser {
    using TokenList = std::list<GrachtToken>;
public:
    GrachtParser(const std::string& Path);

    bool IsValid();

    const TokenList& GetTokens() { return m_Tokens; }

private:
    bool ParseGrachtFile();

private:
    GrachtFile m_Input;
    TokenList  m_Tokens;
};
