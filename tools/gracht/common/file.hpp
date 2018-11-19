/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include <fstream>
#include <string>

class GrachtFile {
public:
    GrachtFile(const std::string& Path);
    ~GrachtFile();

    int ReadCharacter();
    
    unsigned int       GetHashCode();
    const std::string& GetPath(); 

private:
    std::ifstream m_Stream;
};
