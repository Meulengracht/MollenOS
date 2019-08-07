/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "unit.hpp"

class GrachtGenerator {
public:
    virtual int Generate(std::shared_ptr<GrachtUnit>, 
        const std::string& CommonHeadersPath,
        const std::string& ServerSourcesPath) = 0;
};
