/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../parser/lexer.hpp"
#include <memory>
#include <list>

class GrachtUnit {
public:
    GrachtUnit(std::unique_ptr<GrachtAST> AST);
    ~GrachtUnit();

private:
    std::unique_ptr<GrachtAST> m_AST;
    std::list<GrachtUnit>      m_SupportUnits;
};
