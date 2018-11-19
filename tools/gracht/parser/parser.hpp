/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../common/file.hpp"
#include "token.hpp"
#include <memory>
#include <list>

class GrachtParser {
    using TokenList = std::list<GrachtToken>;
public:
    GrachtParser(std::unique_ptr<GrachtFile> Input);

    TokenList& const GetTokens();

private:
    bool ParseGrachtFile();

private:
    std::unique_ptr<GrachtFile> m_Input;
    TokenList                   m_Tokens;
};
