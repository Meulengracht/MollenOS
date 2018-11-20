/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

class GrachtToken {
public:
    enum class TokenType {
        UNKNOWN,
        SemiColon,
        Colon,
        Comma,
        LeftParenthesis,
        RightParenthesis,
        LeftBracket,
        RightBracket,
        OperatorAssign,
        Identifier,
        StringLiteral,
        DigitLiteral,
        CommentLine
    };

public:
    GrachtToken(TokenType Type, const std::string& Value, const std::string& File, int Line, int LineIndex)
        : m_Type(Type), m_Value(Value), m_File(File), m_Line(Line), m_LineIndex(LineIndex) { }

    std::string ToString() { return "in file " + m_File + ": at line " + std::to_string(m_Line); }

private:
    TokenType   m_Type;
    std::string m_Value;
    
    std::string m_File;
    int         m_Line;
    int         m_LineIndex;
};
