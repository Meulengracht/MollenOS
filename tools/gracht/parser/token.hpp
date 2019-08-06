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

    std::string ToString() {
        std::string StringRep = GetTypeString();
        if (m_Value != "") {
            StringRep += " (";
            StringRep += m_Value;
            StringRep += ")";
        }
        StringRep += " in file ";
        StringRep += m_File;
        StringRep += ": at line ";
        StringRep += std::to_string(m_Line);
        return StringRep;
    }

    const std::string& GetValue() { return m_Value; }

public:
    bool Is(TokenType Type) {
        return m_Type == Type;
    }

    bool Matches(TokenType Type, std::string Value) {
        return m_Type == Type && m_Value == Value;
    }

private:
    const std::string GetTypeString() {
        switch (m_Type) {
            case TokenType::SemiColon: return "SemiColon";
            case TokenType::Colon: return "Colon";
            case TokenType::Comma: return "Comma";
            case TokenType::LeftParenthesis: return "LeftParenthesis";
            case TokenType::RightParenthesis: return "RightParenthesis";
            case TokenType::LeftBracket: return "LeftBracket";
            case TokenType::RightBracket: return "RightBracket";
            case TokenType::OperatorAssign: return "OperatorAssign";
            case TokenType::Identifier: return "Identifier";
            case TokenType::StringLiteral: return "StringLiteral";
            case TokenType::DigitLiteral: return "DigitLiteral";
            case TokenType::CommentLine: return "CommentLine";
            default: return "UnknownToken";
        }
    }

private:
    TokenType   m_Type;
    std::string m_Value;
    
    std::string m_File;
    int         m_Line;
    int         m_LineIndex;
};
