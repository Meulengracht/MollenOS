/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../../generator.hpp"
#include <fstream>

class GrachtGeneratorC : public GrachtGenerator {
public:
    int Generate(std::shared_ptr<GrachtUnit>, 
        const std::string& CommonHeadersPath,  
        const std::string& ServerSourcesPath) override;

private:
    void GenerateCommonHeaders(std::shared_ptr<GrachtUnit>, const std::string& Path);

    void GenerateHeaderStart(std::ofstream&, const std::string&);
    void GenerateHeaderEnd(std::ofstream&, const std::string&);
};
