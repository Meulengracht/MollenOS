/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

static const char* TokenNames[] = {
    "UnknownToken",
    "SemiColon",
    "Colon",
    "Comma",
    "LeftParenthesis",
    "RightParenthesis",
    "LeftBracket",
    "RightBracket",
    "OperatorAssign",
    "Identifier",
    "StringLiteral",
    "DigitLiteral",
    "CommentLine"
};

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

    const std::string& GetValue() { return m_Value; }

    std::string ToString() {
        std::string Value(TokenNames[static_cast<int>(m_Type)]);
        return Value + " in file " + m_File + ": at line " + std::to_string(m_Line);
    }

public:
    bool Is(TokenType Type) {
        return m_Type == Type;
    }

    bool Matches(TokenType Type, std::string Value) {
        return m_Type == Type && m_Value == Value;
    }

private:
    TokenType   m_Type;
    std::string m_Value;
    
    std::string m_File;
    int         m_Line;
    int         m_LineIndex;
};
