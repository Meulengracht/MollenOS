/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../token.hpp"
#include "../language.hpp"
#include "../statement.hpp"
#include <memory>
#include <queue>

class GrachtLanguage : public Language {
private:
    using TokenList = std::queue<GrachtToken*>&;
public:
    std::shared_ptr<Statement> ParseTokens(TokenList Tokens) {
        std::shared_ptr<Statement> RootStatement(
            ParseGlobalStatement(GetNextToken(Tokens), Tokens));
        std::shared_ptr<Statement> Iterator = RootStatement;
        
        while (Iterator != nullptr && Tokens.size()) {
            Iterator->SetChildStatement(
                std::shared_ptr<Statement>(
                    ParseGlobalStatement(GetNextToken(Tokens), Tokens)));
            Iterator = Iterator->GetChildStatement();
        }
        return RootStatement;
    }

private:
    Statement* ParseGlobalStatement(GrachtToken* Token, TokenList Tokens) {
        if (Token == nullptr) {
            return nullptr;
        }

        // Using statements are accepted in global context
        if (Token->Matches(GrachtToken::TokenType::Identifier, "using")) {
            return ParseUsingStatement(Token, Tokens);
        }

        // Namespace statements are accepted in global context
        else if (Token->Matches(GrachtToken::TokenType::Identifier, "namespace")) {
            return ParseNamespaceStatement(Token, Tokens);
        }

        // Object statements are accepted in global context
        else if (Token->Matches(GrachtToken::TokenType::Identifier, "object")) {
            return ParseObjectStatement(Token, Tokens);
        }

        // Function statements are accepted in global context
        else if (Token->Matches(GrachtToken::TokenType::Identifier, "func")) {
            return ParseFunctionStatement(Token, Tokens);
        }
        else {
            fprintf(stderr, "Unexpected identifier %s\n", Token->ToString());
            return nullptr;
        }
    }

    // Identifier Identifier SemiColon
    Statement* ParseUsingStatement(GrachtToken* Token, TokenList Tokens) {
        auto ModuleValue = GetNextToken(Tokens);
        auto EndOfLine   = GetNextToken(Tokens);

        if (ModuleValue == nullptr || !ModuleValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected module name after %s\n", Token->ToString());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", ModuleValue->ToString());
            return nullptr;
        }

        return new UsingModule(ModuleValue->GetValue());
    }

    // Identifier Identifier SemiColon
    Statement* ParseNamespaceStatement(GrachtToken* Token, TokenList Tokens) {
        auto NamespaceValue = GetNextToken(Tokens);
        auto EndOfLine      = GetNextToken(Tokens);

        if (NamespaceValue == nullptr || !NamespaceValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected namespace after %s\n", Token->ToString());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", NamespaceValue->ToString());
            return nullptr;
        }

        return new Namespace(NamespaceValue->GetValue());
    }

    // Identifier Identifier LeftBracket
    // 0..n Identifier Identifier SemiColon
    // RightBracket
    Statement* ParseObjectStatement(GrachtToken* Token, TokenList Tokens) {
        auto ObjectValue = GetNextToken(Tokens);
        if (ObjectValue == nullptr || !ObjectValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", Token->ToString());
            return nullptr;
        }

        auto Opener = GetNextToken(Tokens);
        if (Opener == nullptr || !Opener->Is(GrachtToken::TokenType::LeftBracket)) {
            fprintf(stderr, "expected left bracket after object %s\n", ObjectValue->ToString());
            return nullptr;
        }

        auto Obj = new Object(ObjectValue->GetValue());

        auto Iterator = GetNextToken(Tokens);
        while (Iterator && !Iterator->Is(GrachtToken::TokenType::RightBracket)) {
            if (!Iterator->Is(GrachtToken::TokenType::Identifier)) {
                fprintf(stderr, "expected declaration instead of %s\n", ObjectValue->ToString());
                return nullptr;
            }

            auto Child = std::shared_ptr<Statement>(
                ParseDeclarationStatement(Iterator, Tokens));
            if (Obj->GetChildStatement() != nullptr) {
                Child = std::shared_ptr<Statement>(
                    new Sequence(Obj->GetChildStatement(), Child));
            }

            Obj->SetChildStatement(Child);
            Iterator = GetNextToken(Tokens);
        }

        if (Iterator == nullptr || !Iterator->Is(GrachtToken::TokenType::RightBracket)) {
            fprintf(stderr, "expected closing bracket after object %s\n", ObjectValue->ToString());
            return nullptr;
        }
        
        return Obj;
    }
    
    // Identifier Identifier LeftParenthesis
    // 0..n Identifier Identifier [Comma]
    // RightParenthesis [Colon Identifier] SemiColon
    Statement* ParseFunctionStatement(GrachtToken* Token, TokenList Tokens) {

    }

    // Identifier Identifier SemiColon
    Statement* ParseDeclarationStatement(GrachtToken* TypeToken, TokenList Tokens) {
        auto NameToken = GetNextToken(Tokens);
        auto EndOfLine = GetNextToken(Tokens);

        if (NameToken == nullptr || !NameToken->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", TypeToken->ToString());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", NameToken->ToString());
            return nullptr;
        }

        return new Declaration(TypeToken->GetValue(), NameToken->GetValue());
    }

    GrachtToken* GetNextToken(TokenList Tokens) {
        auto Token = Tokens.front();
        Tokens.pop();
        return Token;
    }
};
