/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../generator.hpp"

class GrachtGeneratorC : public GrachtGenerator {
public:
    GrachtGeneratorC(std::unique_ptr<GrachtUnit> Unit);

    int Generate(const std::string& CommonHeadersPath, const std::string& ClientSourcesPath, 
        const std::string& ServerSourcesPath) override;

private:
    std::unique_ptr<GrachtUnit> m_Unit;
};
