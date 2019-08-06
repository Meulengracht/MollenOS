/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */
#pragma once

#include "../parser/lexer.hpp"
#include <memory>
#include <list>

class GrachtScope {

};

class GrachtObject : public GrachtScope {

};

class GrachtFunction : public GrachtScope {

};

class GrachtEnum : public GrachtScope {

};

class GrachtUnit {
    using UnitList     = std::list<GrachtUnit>;
    using ObjectList   = std::list<GrachtObject>;
    using FunctionList = std::list<GrachtFunction>;
    using EnumList     = std::list<GrachtEnum>;
public:
    GrachtUnit(const std::string& Path, std::shared_ptr<Language> CodeLanguage);
    
    bool IsValid();

    const UnitList&     GetSupportUnits() { return m_SupportUnits; }
    const ObjectList&   GetObjects()      { return m_Objects; }
    const FunctionList& GetFunctions()    { return m_Functions; }
    const EnumList&     GetEnums()        { return m_Enums; }

private:
    bool ParseAST();
    bool ResolveUsing();

private:
    GrachtAST    m_AST;
    ObjectList   m_Objects;
    FunctionList m_Functions;
    EnumList     m_Enums;
    UnitList     m_SupportUnits;
};
