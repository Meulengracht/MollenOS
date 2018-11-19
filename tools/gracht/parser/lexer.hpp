/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "parser.hpp"
#include <memory>
#include <list>

class GrachtAST {
public:
    GrachtAST(std::unique_ptr<GrachtParser> Parser);
    ~GrachtAST();

private:
    std::unique_ptr<GrachtParser> m_Parser;
};
