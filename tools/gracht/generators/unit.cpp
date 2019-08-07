/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "unit.hpp"
#include <algorithm>

GrachtUnit::GrachtUnit(const std::string& Path, std::shared_ptr<Language> CodeLanguage)
    : m_Path(Path), m_Language(CodeLanguage), m_AST(Path, CodeLanguage)
{
    size_t LastDelimiter = Path.find_last_of("\\/");
    size_t LastDot       = Path.find_last_of(".");

    m_UnitName = Path.substr(LastDelimiter + 1, LastDot - LastDelimiter - 1);
    m_Symbols.reset(new SymbolTable());
}

bool GrachtUnit::IsValid()
{
    if (m_AST.IsValid()) {
        return ParseAST();
    }
    return false;
}

bool GrachtUnit::ParseAST()
{
    ParseState State;
    State.GlobalScope = 1;
    State.m_Symbols = m_Symbols;

    return ParseStatement(State, m_AST.GetRootStatement());
}

bool GrachtUnit::ParseStatement(ParseState& State, std::shared_ptr<Statement> Stmt)
{
    if (Stmt == nullptr) {
        return true;
    }

    switch (Stmt->GetType()) {
        case Statement::StatementType::Using: {
            if (!ResolveUsing(State, std::static_pointer_cast<UsingModule>(Stmt))) {
                return false;
            }
        } break;
        case Statement::StatementType::Namespace: {
            // Resolve?
        } break;
        case Statement::StatementType::Declaration: {
            if (!ResolveDeclaration(State, std::static_pointer_cast<Declaration>(Stmt))) {
                return false;
            }
        } break;
        case Statement::StatementType::Object: {
            if (!ResolveObject(State, std::static_pointer_cast<Object>(Stmt))) {
                return false;
            }
        } break;
        case Statement::StatementType::Function: {
            if (!ResolveFunction(State, std::static_pointer_cast<Function>(Stmt))) {
                return false;
            }
        } break;
        case Statement::StatementType::Sequence: {
            // Always go left first
            auto Seq = std::static_pointer_cast<Sequence>(Stmt);
            if (!ParseStatement(State, Seq->GetStatement1())) {
                return false;
            }
            if (!ParseStatement(State, Seq->GetStatement2())) {
                return false;
            }
        } break;
    }
    return true;
}

bool GrachtUnit::ResolveDeclaration(ParseState& State, std::shared_ptr<Declaration> DeclStatement)
{
    // Resolve types
    if (!ResolveType(State, DeclStatement->GetOfType())) {
        fprintf(stderr, "Failed to resolve type %s in file %s\n", 
            DeclStatement->GetOfType().c_str(), m_Path.c_str());
        return false;
    }

    // Check for redeclarations
    if (HasSymbol(State, DeclStatement->GetIdentifier())) {
        fprintf(stderr, "Redeclaration of symbol %s in file %s\n", 
            DeclStatement->GetIdentifier().c_str(), m_Path.c_str());
    }

    // Add symbol to list
    State.m_Symbols->AddSymbol(std::shared_ptr<GrachtSymbol>(
        new GrachtDeclaration(DeclStatement->GetOfType(), DeclStatement->GetIdentifier())));
    return true;
}

bool GrachtUnit::ResolveObject(ParseState& State, std::shared_ptr<Object> ObjectStatement)
{
    // Check for redeclaration
    if (HasSymbol(State, ObjectStatement->GetIdentifier())) {
        fprintf(stderr, "Redeclaration of symbol %s in file %s\n", 
            ObjectStatement->GetIdentifier().c_str(), m_Path.c_str());
    }

    // Update scope to object
    std::shared_ptr<SymbolTable> Symbols(new SymbolTable());
    auto PreviousSymbols = State.m_Symbols;
    auto PreviousScope   = State.GlobalScope;

    State.GlobalScope = 0;
    State.m_Symbols   = Symbols;

    // Iterate members and verify types & check for name dublicates
    if (!ParseStatement(State, ObjectStatement->GetChildStatement())) {
        return false;
    }

    State.GlobalScope = PreviousScope;
    State.m_Symbols   = PreviousSymbols;

    State.m_Symbols->AddSymbol(std::shared_ptr<GrachtSymbol>(
        new GrachtObject(ObjectStatement->GetIdentifier(), Symbols)));

    return true;
}

bool GrachtUnit::ResolveFunction(ParseState& State, std::shared_ptr<Function> FunctionStatement)
{
    // Check for redeclaration
    if (HasSymbol(State, FunctionStatement->GetIdentifier())) {
        fprintf(stderr, "Redeclaration of symbol %s in file %s\n", 
            FunctionStatement->GetIdentifier().c_str(), m_Path.c_str());
        return false;
    }

    // Resolve return type of function
    if (!ResolveType(State, FunctionStatement->GetReturnType())) {
        fprintf(stderr, "Return type for function %s: %s is unknown in file %s\n", 
            FunctionStatement->GetIdentifier().c_str(),
            FunctionStatement->GetReturnType().c_str(), m_Path.c_str());
        return false;
    }

    // Update scope to function
    std::shared_ptr<SymbolTable> Symbols(new SymbolTable());
    auto PreviousSymbols = State.m_Symbols;
    auto PreviousScope   = State.GlobalScope;

    State.GlobalScope = 0;
    State.m_Symbols   = Symbols;

    // Iterate parameters and verify types & check for name dublicates
    if (!ParseStatement(State, FunctionStatement->GetChildStatement())) {
        return false;
    }

    State.GlobalScope = PreviousScope;
    State.m_Symbols   = PreviousSymbols;

    State.m_Symbols->AddSymbol(std::shared_ptr<GrachtSymbol>(
        new GrachtFunction(FunctionStatement->GetIdentifier(), 
            FunctionStatement->GetReturnType(), Symbols)));
    return true;
}

bool GrachtUnit::ResolveUsing(ParseState& State, std::shared_ptr<UsingModule> UsingStatement)
{
    std::string Path = m_Path.substr(0, m_Path.find_last_of("\\/"));
    std::string ModulePath = Path + "/" + UsingStatement->GetModule() + ".gc";
    printf("loading module %s\n", ModulePath.c_str());

    auto Module = std::shared_ptr<GrachtUnit>(new GrachtUnit(ModulePath, m_Language));
    if (!Module->IsValid()) {
        fprintf(stderr, "Failed to resolve module %s in file %s\n", 
            UsingStatement->GetModule().c_str(), m_Path.c_str());
        return false;
    }
    
    m_SupportUnits.push_back(Module);
    return true;
}

bool GrachtUnit::ResolveType(ParseState& State, const std::string& Type)
{
    // Check builtins
    if (Type == "string" || Type == "int" || 
        Type == "uint" || Type == "bool") {
        return true;
    }

    // Check symbols
    auto Symbol = GetSymbol(State, Type);
    if (Symbol == nullptr) {
        return false;
    }
    
    // Check the type of symbol
    return true;
}

std::list<std::shared_ptr<GrachtSymbol>> GrachtUnit::GetSymbolsOfType(GrachtSymbol::SymbolType Type)
{
    std::list<std::shared_ptr<GrachtSymbol>> Symbols = m_Symbols->GetSymbolsOfType(Type);
    for (auto Unit : m_SupportUnits) {
        auto UnitSymbols = Unit->GetSymbolsOfType(Type);
        Symbols.insert(Symbols.end(), UnitSymbols.begin(), UnitSymbols.end());
    }
    return Symbols;
}

std::shared_ptr<GrachtSymbol> GrachtUnit::GetGlobalSymbol(const std::string& Symbol)
{
    auto global = m_Symbols->GetSymbol(Symbol);
    if (global != nullptr) {
        return global;
    }
    
    for (auto Unit : m_SupportUnits) {
        global = Unit->GetGlobalSymbol(Symbol);
        if (global != nullptr) {
            return global;
        }
    }
    return std::shared_ptr<GrachtSymbol>(nullptr);
}

bool GrachtUnit::HasSymbol(ParseState& State, const std::string& Symbol)
{
    return GetSymbol(State, Symbol) != nullptr;
}

std::shared_ptr<GrachtSymbol> GrachtUnit::GetSymbol(ParseState& State, const std::string& Symbol)
{
    // Check local scope (obj or func)
    auto local = State.m_Symbols->GetSymbol(Symbol);
    if (local != nullptr) {
        return local;
    }

    // Check global scope
    if (!State.GlobalScope) {
        auto global = GetGlobalSymbol(Symbol);
        if (global != nullptr) {
            return global;
        }
    }
    return std::shared_ptr<GrachtSymbol>(nullptr);
}
