/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "file.hpp"

GrachtFile::GrachtFile(const std::string& Path)
    : m_Stream(Path), m_Path(Path), m_Hash(0)
{
}

int GrachtFile::ReadCharacter()
{
    if (m_Stream.is_open()) {
        int Character = m_Stream.get();
        if (m_Stream) {
            return Character;
        }
    }
    return EOF;
}
