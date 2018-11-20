/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include <fstream>
#include <string>

class GrachtFile {
public:
    GrachtFile(const std::string& Path);

    int ReadCharacter();
    
    unsigned int       GetHashCode() { return m_Hash; }
    const std::string& GetPath() { return m_Path; }

private:
    std::ifstream m_Stream;
    std::string   m_Path;
    unsigned int  m_Hash;
};
