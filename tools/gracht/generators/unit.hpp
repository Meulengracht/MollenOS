/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../parser/lexer.hpp"
#include <memory>
#include <list>

class GrachtUnit {
    using UnitList = std::list<GrachtUnit>;
public:
    GrachtUnit(std::unique_ptr<GrachtAST> AST);

    UnitList& const GetSupportUnits();

private:
    bool VerifyAST();

private:
    std::unique_ptr<GrachtAST> m_AST;
    UnitList                   m_SupportUnits;
};
