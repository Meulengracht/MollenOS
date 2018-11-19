/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "unit.hpp"

class GrachtGenerator {
protected:
    GrachtGenerator(std::unique_ptr<GrachtUnit> Unit);
    virtual ~GrachtGenerator() { }

    virtual int Generate(const std::string& CommonHeadersPath, const std::string& ClientSourcesPath, 
        const std::string& ServerSourcesPath) = 0;

protected:
    std::unique_ptr<GrachtUnit> m_Unit;
};
