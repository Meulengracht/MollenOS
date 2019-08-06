/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "lexer.hpp"
#include <queue>

template<class T>
class QueueAdapter
{
public:
    QueueAdapter(std::queue<T>& q) : _q(q) {}
    void push_back(const T& t) { _q.push(t); }

private:
    std::queue<T>& _q;
};

GrachtAST::GrachtAST(const std::string& Path, std::shared_ptr<Language>& ASTLanguage)
    : m_Parser(Path), m_Language(ASTLanguage), m_RootNode(nullptr), m_CurrentScope(ASTScope::Global)
{
}

bool GrachtAST::IsValid()
{
    if (m_Parser.IsValid()) {
        return VerifyAST();
    }
    return false;
}

bool GrachtAST::ParseTokens()
{
    std::queue<GrachtToken*> Tokens;
    for (auto Token : m_Parser.GetTokens()) {
        Tokens.push(&Token);
    }

    m_RootNode = m_Language->ParseTokens(Tokens);
    return m_RootNode != nullptr;
}

bool GrachtAST::VerifyAST()
{
    if (m_Parser.GetTokens().size() != 0 && !ParseTokens()) {
        return false;
    }
    return true;
}
