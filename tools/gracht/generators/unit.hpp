/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../parser/lexer.hpp"
#include <algorithm>
#include <memory>
#include <list>

class GrachtSymbol {
public:
    enum class SymbolType {
        Declaration,
        Object,
        Function
    };
protected:
    GrachtSymbol(SymbolType Type, const std::string& Name) 
        : m_Type(Type), m_Name(Name) { }

public:
    const std::string& GetName() const { return m_Name; }
    SymbolType         GetType() const { return m_Type; }

private:
    SymbolType  m_Type;
    std::string m_Name;
};

class SymbolTable {
    using SymbolList = std::list<std::shared_ptr<GrachtSymbol>>;
public:
    bool AddSymbol(std::shared_ptr<GrachtSymbol> Symbol) {
        if (HasSymbol(Symbol->GetName())) {
            return false;
        }
        m_Symbols.push_back(Symbol);
    }
    
    bool HasSymbol(const std::string& Symbol) {
        return GetSymbol(Symbol) != nullptr;
    }
    
    std::list<std::shared_ptr<GrachtSymbol>> GetSymbolsOfType(GrachtSymbol::SymbolType Type) {
        std::list<std::shared_ptr<GrachtSymbol>> Symbols;
        std::copy_if(m_Symbols.begin(), m_Symbols.end(), std::back_inserter(Symbols), 
            [Type](std::shared_ptr<GrachtSymbol> const& Symbol) {
                return Symbol->GetType() == Type;
            });
        return Symbols;
    }

    std::shared_ptr<GrachtSymbol> GetSymbol(const std::string& Symbol) {
        auto it = std::find_if(m_Symbols.begin(), m_Symbols.end(), 
            [Symbol](std::shared_ptr<GrachtSymbol> const& Rh){
                return Rh->GetName() == Symbol;
            });
        if (it != m_Symbols.end()) {
            return *it;
        }
        return std::shared_ptr<GrachtSymbol>(nullptr);
    }

    const SymbolList& GetSymbols() { return m_Symbols; }    

private:
    SymbolList m_Symbols;
};

class GrachtDeclaration : public GrachtSymbol {
public:
    GrachtDeclaration(const std::string& Typename, const std::string& Name) : 
        GrachtSymbol(SymbolType::Declaration, Name), m_Typename(Typename) { }

    const std::string& GetTypename() const { return m_Typename; }

private:
    std::string m_Typename;
};

class GrachtObject : public GrachtSymbol {
public:
    GrachtObject(const std::string& Name, std::shared_ptr<SymbolTable> Symbols) : 
        GrachtSymbol(SymbolType::Object, Name), m_Symbols(Symbols) { }

    const std::shared_ptr<SymbolTable>& GetSymbolTable() { return m_Symbols; }

private:
    std::shared_ptr<SymbolTable> m_Symbols;
};

class GrachtFunction : public GrachtSymbol {
public:
    GrachtFunction(const std::string& Name, const std::string& Typename, std::shared_ptr<SymbolTable> Symbols) : 
        GrachtSymbol(SymbolType::Function, Name), m_ReturnType(Typename), m_Symbols(Symbols) { }

    const std::string&                  GetReturnType() const  { return m_ReturnType; }
    const std::shared_ptr<SymbolTable>& GetSymbolTable() const { return m_Symbols; }

private:
    std::string                  m_ReturnType;
    std::shared_ptr<SymbolTable> m_Symbols;
};

class GrachtUnit {
    using UnitList = std::list<std::shared_ptr<GrachtUnit>>;
private:
    struct ParseState {
        int GlobalScope;
        std::shared_ptr<SymbolTable> m_Symbols;
    };

public:
    GrachtUnit(const std::string& Path, std::shared_ptr<Language> CodeLanguage);
    
    bool                          IsValid();
    std::shared_ptr<GrachtSymbol> GetGlobalSymbol(const std::string&);
    std::list<std::shared_ptr<GrachtSymbol>> GetSymbolsOfType(GrachtSymbol::SymbolType Type);
    const std::string&            GetName() const { return m_UnitName; }

private:
    bool ParseStatement(ParseState&, std::shared_ptr<Statement>);
    bool ParseAST();
    bool ResolveUsing(ParseState&, std::shared_ptr<UsingModule>);
    bool ResolveDeclaration(ParseState&, std::shared_ptr<Declaration>);
    bool ResolveObject(ParseState&, std::shared_ptr<Object>);
    bool ResolveFunction(ParseState&, std::shared_ptr<Function>);

    bool ResolveType(ParseState&, const std::string&);
    bool HasSymbol(ParseState&, const std::string&);
    std::shared_ptr<GrachtSymbol> GetSymbol(ParseState&, const std::string&);

private:
    std::string                  m_Path;
    std::shared_ptr<Language>    m_Language;
    std::string                  m_UnitName;
    GrachtAST                    m_AST;
    std::shared_ptr<SymbolTable> m_Symbols;
    UnitList                     m_SupportUnits;
};
