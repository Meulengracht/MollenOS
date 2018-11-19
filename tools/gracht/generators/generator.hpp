/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "unit.hpp"

class GrachtGenerator {
protected:
    GrachtGenerator(std::unique_ptr<GrachtUnit> Unit);
    ~GrachtGenerator();

private:
    std::unique_ptr<GrachtUnit> m_Unit;
};
