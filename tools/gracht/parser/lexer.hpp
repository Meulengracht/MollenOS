/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "parser.hpp"
#include <string>

class GrachtAST {
public:
    GrachtAST(const std::string& Path);

    bool IsValid();

private:
    bool ParseTokens();
    bool VerifyAST();

private:
    GrachtParser m_Parser;
};
