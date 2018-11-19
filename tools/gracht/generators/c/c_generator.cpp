/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "c_generator.hpp"

GrachtGeneratorC::GrachtGeneratorC(std::unique_ptr<GrachtUnit> Unit)
    : GrachtGenerator(std::move(Unit))
{
}

GrachtGeneratorC::~GrachtGeneratorC()
{
}

int GrachtGeneratorC::Generate(const std::string& CommonHeadersPath, const std::string& ClientSourcesPath, 
        const std::string& ServerSourcesPath)
{
    auto Units = m_Unit->GetUnits();
    if (Units.size() == 0) {
        return -1;
    }
    return 0;
}
