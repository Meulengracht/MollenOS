/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "unit.hpp"

class GrachtGenerator {
public:
    virtual int Generate(const std::string& CommonHeadersPath, const std::string& ClientSourcesPath, 
        const std::string& ServerSourcesPath) = 0;
};
