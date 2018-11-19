/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../generator.hpp"

class GrachtGeneratorC {
public:
    GrachtGeneratorC(std::unique_ptr<GrachtUnit> Unit);
    ~GrachtGeneratorC();
};
