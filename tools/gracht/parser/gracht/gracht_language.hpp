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
    using TokenList = std::queue<std::shared_ptr<GrachtToken>>&;
public:
    std::shared_ptr<Statement> ParseTokens(TokenList Tokens) {
        std::shared_ptr<Statement> RootStatement(
            ParseGlobalStatement(GetNextToken(Tokens), Tokens));
        std::shared_ptr<Statement> Iterator = RootStatement;
        
        while (Iterator != nullptr && Tokens.size()) {
            auto Child = std::shared_ptr<Statement>(
                    ParseGlobalStatement(GetNextToken(Tokens), Tokens));
            RootStatement = std::shared_ptr<Statement>(
                new Sequence(RootStatement, Child));
        }
        return RootStatement;
    }

private:
    Statement* ParseGlobalStatement(std::shared_ptr<GrachtToken> Token, TokenList Tokens) {
        if (Token == nullptr) {
            return nullptr;
        }
        printf("%s\n", Token->ToString().c_str());

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
            fprintf(stderr, "Unexpected %s\n", Token->ToString().c_str());
            return nullptr;
        }
    }

    // Identifier Identifier SemiColon
    Statement* ParseUsingStatement(std::shared_ptr<GrachtToken> Token, TokenList Tokens) {
        auto ModuleValue = GetNextToken(Tokens);
        auto EndOfLine   = GetNextToken(Tokens);

        if (ModuleValue == nullptr || !ModuleValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected module name after %s\n", Token->ToString().c_str());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", ModuleValue->ToString().c_str());
            return nullptr;
        }

        return new UsingModule(ModuleValue->GetValue());
    }

    // Identifier Identifier SemiColon
    Statement* ParseNamespaceStatement(std::shared_ptr<GrachtToken> Token, TokenList Tokens) {
        auto NamespaceValue = GetNextToken(Tokens);
        auto EndOfLine      = GetNextToken(Tokens);

        if (NamespaceValue == nullptr || !NamespaceValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected namespace after %s\n", Token->ToString().c_str());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", NamespaceValue->ToString().c_str());
            return nullptr;
        }

        return new Namespace(NamespaceValue->GetValue());
    }

    // Identifier Identifier LeftBracket
    // 0..n Identifier Identifier SemiColon
    // RightBracket
    Statement* ParseObjectStatement(std::shared_ptr<GrachtToken> Token, TokenList Tokens) {
        auto ObjectValue = GetNextToken(Tokens);
        if (ObjectValue == nullptr || !ObjectValue->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", Token->ToString().c_str());
            return nullptr;
        }

        auto Opener = GetNextToken(Tokens);
        if (Opener == nullptr || !Opener->Is(GrachtToken::TokenType::LeftBracket)) {
            fprintf(stderr, "expected left bracket after object %s\n", ObjectValue->ToString().c_str());
            return nullptr;
        }

        auto Obj = new Object(ObjectValue->GetValue());

        auto Iterator = GetNextToken(Tokens);
        while (Iterator && !Iterator->Is(GrachtToken::TokenType::RightBracket)) {
            if (!Iterator->Is(GrachtToken::TokenType::Identifier)) {
                fprintf(stderr, "expected declaration instead of %s\n", Iterator->ToString().c_str());
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
            fprintf(stderr, "expected closing bracket after object %s\n", ObjectValue->ToString().c_str());
            return nullptr;
        }
        
        return Obj;
    }
    
    // Identifier Identifier LeftParenthesis
    // 0..n Identifier Identifier [Comma]
    // RightParenthesis [Colon Identifier] SemiColon
    Statement* ParseFunctionStatement(std::shared_ptr<GrachtToken> FuncToken, TokenList Tokens) {
        auto NameToken = GetNextToken(Tokens);

        if (NameToken == nullptr || !NameToken->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", FuncToken->ToString().c_str());
            return nullptr;
        }

        auto Opener = GetNextToken(Tokens);
        if (Opener == nullptr || !Opener->Is(GrachtToken::TokenType::LeftParenthesis)) {
            fprintf(stderr, "expected '(' after function name %s\n", NameToken->ToString().c_str());
            return nullptr;
        }

        auto Func = new Function(NameToken->GetValue());

        auto Iterator = GetNextToken(Tokens);
        bool Comma    = false;
        while (Iterator && !Iterator->Is(GrachtToken::TokenType::RightParenthesis)) {
            if (!Iterator->Is(GrachtToken::TokenType::Identifier)) {
                fprintf(stderr, "expected identifier instead of %s\n", Iterator->ToString().c_str());
                return nullptr;
            }

            auto Child = std::shared_ptr<Statement>(
                ParseParameterStatement(Iterator, Tokens, Comma));
            if (Func->GetChildStatement() != nullptr) {
                Child = std::shared_ptr<Statement>(
                    new Sequence(Func->GetChildStatement(), Child));
            }

            Func->SetChildStatement(Child);
            Iterator = GetNextToken(Tokens);
        }

        // Get close parenthesis, must exist at this point here
        if (Iterator == nullptr || !Iterator->Is(GrachtToken::TokenType::RightParenthesis)) {
            fprintf(stderr, "expected ')' after function %s\n", NameToken->ToString().c_str());
            return nullptr;
        }
        
        auto ColonToken = Tokens.front();
        if (ColonToken != nullptr && ColonToken->Is(GrachtToken::TokenType::Colon)) {
            ColonToken           = GetNextToken(Tokens);
            auto ReturnTypeToken = GetNextToken(Tokens);
            if (ReturnTypeToken == nullptr || !ReturnTypeToken->Is(GrachtToken::TokenType::Identifier)) {
                fprintf(stderr, "missing return type after ':' %s\n", ColonToken->ToString().c_str());
                return nullptr;
            }
        }

        auto EndOfLine = GetNextToken(Tokens);
        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", NameToken->ToString().c_str());
            return nullptr;
        }

        return Func;
    }

    // Identifier Identifier [Comma]
    Statement* ParseParameterStatement(std::shared_ptr<GrachtToken> TypeToken, TokenList Tokens, bool& Comma) {
        auto NameToken  = GetNextToken(Tokens);
        auto CommaToken = Tokens.front();

        if (NameToken == nullptr || !NameToken->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", TypeToken->ToString().c_str());
            return nullptr;
        }

        if (CommaToken == nullptr || !CommaToken->Is(GrachtToken::TokenType::Comma)) {
            Comma = false;
        }
        else {
            // Consume the comma
            CommaToken = GetNextToken(Tokens);
            Comma      = true;
        }

        return new Declaration(TypeToken->GetValue(), NameToken->GetValue());
    }

    // Identifier Identifier SemiColon
    Statement* ParseDeclarationStatement(std::shared_ptr<GrachtToken> TypeToken, TokenList Tokens) {
        auto NameToken = GetNextToken(Tokens);
        auto EndOfLine = GetNextToken(Tokens);

        if (NameToken == nullptr || !NameToken->Is(GrachtToken::TokenType::Identifier)) {
            fprintf(stderr, "expected name identifier after %s\n", TypeToken->ToString().c_str());
            return nullptr;
        }

        if (EndOfLine == nullptr || !EndOfLine->Is(GrachtToken::TokenType::SemiColon)) {
            fprintf(stderr, "missing ';' after %s\n", NameToken->ToString().c_str());
            return nullptr;
        }

        return new Declaration(TypeToken->GetValue(), NameToken->GetValue());
    }

    std::shared_ptr<GrachtToken> GetNextToken(TokenList Tokens) {
        auto Token = Tokens.front();
        Tokens.pop();
        return Token;
    }
};
