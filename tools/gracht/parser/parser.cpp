/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "parser.hpp"

GrachtParser::GrachtParser(std::unique_ptr<GrachtFile> Input)
    : m_Input(std::move(Input))
{
}
    
TokenList& const GrachtParser::GetTokens()
{
    return m_Tokens;
}

bool GrachtParser::ParseGrachtFile()
{
    int Line      = 0;
    int LineIndex = 0;
    while (true) {
        int Character = m_Input->ReadCharacter(); LineIndex++;
        if (Character == EOF) {
            break;
        }

        // Ignore spaces, newlines, carriage returns and tabs
        if (isspace(Character) || Character == '\n' || Character == '\r' || Character == '\t') {
            if (Character == '\n') {
                Line++;
                LineIndex = 0;
            }
            continue;
        }

        // Ignore comment lines
        if (Character == '#') {
            while (m_Input->ReadCharacter() != '\n');
            Line++;
            LineIndex = 0;
            continue;
        }

        // Digits
        if (isdigit(Character)) {
            std::string Identifier = "";
            while (isdigit(Character) || ispunct(Character)) {
                Identifier += static_cast<char>(Character & 0xFF);
                Character   = m_Input->ReadCharacter(); LineIndex++;
            }
            m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::DigitLiteral, Identifier, 
                m_Input->GetPath(), Line, LineIndex));
        }

        // Strings
        if (Character == '\"') {
            std::string String = "";
            Character          = m_Input->ReadCharacter(); LineIndex++;
            while (Character != '\"') {
                String    += static_cast<char>(Character & 0xFF);
                Character  = m_Input->ReadCharacter(); LineIndex++;
            }
            m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::StringLiteral, String, 
                m_Input->GetPath(), Line, LineIndex));
        }

        // Identifiers
        if (isalpha(Character)) {
            std::string Identifier = "";
            while (isalpha(Character) || isdigit(Character) || Character == '_') {
                Identifier += static_cast<char>(Character & 0xFF);
                Character   = m_Input->ReadCharacter(); LineIndex++;
            }
            m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::Identifier, Identifier, 
                m_Input->GetPath(), Line, LineIndex));
        }

        // Match rest against known fixed cases
        switch (Character) {
            case '(': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::LeftParenthesis, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;
            case ')': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::RightParenthesis, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;
            case '{': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::LeftBracket, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;
            case '}': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::RightBracket, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;
            case ';': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::OperatorSemiColon, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;
            case '=': {
                m_Tokens.push_back(GrachtToken(GrachtToken::TokenType::OperatorAssign, "", 
                    m_Input->GetPath(), Line, LineIndex));
            } break;

            default: {
                printf("invalid token %c at line %i, %i in file %s\n",
                    Character, Line, LineIndex, m_Input->GetPath().c_str());
                return false;
            } break;
        }
    }
    return true;
}
