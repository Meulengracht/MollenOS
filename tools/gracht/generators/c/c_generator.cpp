/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "c_generator.hpp"

GrachtGeneratorC::GrachtGeneratorC(std::unique_ptr<GrachtUnit> Unit)
    : m_Unit(std::move(Unit))
{
}

int GrachtGeneratorC::Generate(const std::string& CommonHeadersPath, const std::string& ClientSourcesPath, 
        const std::string& ServerSourcesPath)
{
    return 0;
}
