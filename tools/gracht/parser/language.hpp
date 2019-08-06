/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "token.hpp"
#include <queue>

class Language {
public:
    virtual std::shared_ptr<Statement> ParseTokens(std::queue<GrachtToken*>&) = 0;
};
