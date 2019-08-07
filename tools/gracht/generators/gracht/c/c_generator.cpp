/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "c_generator.hpp"

static bool IsTyped(const std::string& Typename)
{
    // signed values: byte, word, int, long
    if (Typename == "byte" || Typename == "word" || Typename == "int" || Typename == "long" ||
        Typename == "ubyte" || Typename == "uword" || Typename == "uint" || Typename == "ulong" ||
        Typename == "bool" || Typename == "enum") {
        return true;
    }
    return false;
}

// Convert builtin types to language types
static std::string GetCTypename(std::shared_ptr<GrachtUnit> CodeUnit,
    const std::string& Typename)
{
    // signed values: byte, word, int, long
    if (Typename == "byte") {
        return "char";
    }
    else if (Typename == "word") {
        return "short";
    }

    // unsigned values: ubyte, uword, uint, ulong
    else if (Typename == "ubyte") {
        return "unsigned char";
    }
    else if (Typename == "uword") {
        return "unsigned short";
    }
    else if (Typename == "uint") {
        return "unsigned int";
    }
    else if (Typename == "ulong") {
        return "unsigned long long";
    }

    // types: bool, float, double, string
    if (Typename == "string") {
        return "const char*";
    }

    // objects
    auto CustomTypename = CodeUnit->GetGlobalSymbol(Typename);
    if (CustomTypename != nullptr) {
        return "struct " + Typename;
    }
    return Typename;
}

int GrachtGeneratorC::Generate(std::shared_ptr<GrachtUnit> CodeUnit, 
        const std::string& CommonHeadersPath,
        const std::string& ServerSourcesPath)
{
    GenerateCommonHeaders(CodeUnit, CommonHeadersPath);
    return 0;
}

void GrachtGeneratorC::GenerateCommonHeaders(
    std::shared_ptr<GrachtUnit> CodeUnit, const std::string& Path)
{
    auto Objects = CodeUnit->GetSymbolsOfType(GrachtSymbol::SymbolType::Object);
    auto Functions = CodeUnit->GetSymbolsOfType(GrachtSymbol::SymbolType::Function);

    std::string Name = CodeUnit->GetName();
    std::transform(Name.begin(), Name.end(), Name.begin(),
        [](unsigned char c){ return std::tolower(c); });
    std::string FullPath = Path + "/" + Name + ".h";
    
    std::ofstream Header(FullPath);
    if (Header.is_open()) {
        GenerateHeaderStart(Header, CodeUnit->GetName());
        for (auto Obj : Objects) {
            auto CastedObj = std::static_pointer_cast<GrachtObject>(Obj);
            Header << "struct " << CastedObj->GetName() << " {" << std::endl;
            for (auto Member : CastedObj->GetSymbolTable()->GetSymbols()) {
                auto CastedMember = std::static_pointer_cast<GrachtDeclaration>(Member);
                Header << "    " << GetCTypename(CodeUnit, CastedMember->GetTypename()) << " " << CastedMember->GetName() << ";" << std::endl;
            }
            Header << "};" << std::endl << std::endl;
        }

        for (auto Func : Functions) {
            auto CastedFunc = std::static_pointer_cast<GrachtFunction>(Func);
            Header << "static inline " << GetCTypename(CodeUnit, CastedFunc->GetReturnType()) << std::endl;
            Header << CastedFunc->GetName() << "(";

            if (CastedFunc->GetSymbolTable()->GetSymbols().size() > 0) {
                Header << std::endl;

                auto it = CastedFunc->GetSymbolTable()->GetSymbols().begin();
                int  i;
                for (i = 0; i < CastedFunc->GetSymbolTable()->GetSymbols().size(); i++, it++) {
                    auto CastedParam = std::static_pointer_cast<GrachtDeclaration>(*it);
                    Header << "    _In_ " << GetCTypename(CodeUnit, CastedParam->GetTypename()) << " " << CastedParam->GetName();
                    if (i + 1 != CastedFunc->GetSymbolTable()->GetSymbols().size()) {
                        Header << "," << std::endl;
                    }
                }
            }
            else {
                Header << "void";
            }

            Header << ")" << std::endl;
            Header << "{" << std::endl;

            // Generate variables
            auto ResultString = "NULL";
            Header << "    " << "IpcMessage_t Message;" << std::endl;
            if (CastedFunc->GetReturnType() != "void") {
                Header << "    " << GetCTypename(CodeUnit, CastedFunc->GetReturnType()) << " Result;" << std::endl;
                ResultString = "&Result";
            }
            Header << std::endl;
            Header << "    " << "IpcInitialize(&Message);" << std::endl;

            // Generate parameters
            if (CastedFunc->GetSymbolTable()->GetSymbols().size() > 0) {
                auto it = CastedFunc->GetSymbolTable()->GetSymbols().begin();
                int  i;
                int  TypedIndex = 0;
                int  UntypedIndex = 0;
                for (i = 0; i < CastedFunc->GetSymbolTable()->GetSymbols().size(); i++, it++) {
                    auto CastedParam = std::static_pointer_cast<GrachtDeclaration>(*it);
                    if (IsTyped(CastedParam->GetTypename())) {
                        Header << "    " << "IpcSetTypedArgument(&Message, " << TypedIndex++ << ", " << 
                            CastedParam->GetName() << ");" << std::endl;
                    }
                    else {
                        Header << "    " << "IpcSetUntypedArgumnet(&Message, " << UntypedIndex++ << ", " << 
                            CastedParam->GetName() << ", ";
                        if (CastedParam->GetTypename() == "string") {
                            Header << "strlen(" << 
                                GetCTypename(CodeUnit, CastedParam->GetTypename()) << 
                                ") + 1" << ");" << std::endl;
                        }
                        else {
                            Header << "sizeof(" << 
                                GetCTypename(CodeUnit, CastedParam->GetTypename()) << 
                                ")" << ");" << std::endl;
                        }
                    }
                }
            }

            // Handle invocation and return
            Header << "    " << "IpcInvoke(Target, &Message, 0, 0, " << ResultString << ");" << std::endl;
            if (CastedFunc->GetReturnType() != "void") {
                Header << "    " << "return Result;" << std::endl;
            }
            Header << "}" << std::endl << std::endl;
        }
        GenerateHeaderEnd(Header, CodeUnit->GetName());
        Header.close();
    }
}

void GrachtGeneratorC::GenerateHeaderStart(std::ofstream& Stream, const std::string& Name)
{
    std::string UppercaseName = Name;
    std::transform(UppercaseName.begin(), UppercaseName.end(), UppercaseName.begin(),
        [](unsigned char c){ return std::toupper(c); });

    Stream << "/**" << std::endl;
    Stream << " * This header is automatically generated by the Gracht code generator" << std::endl;
    Stream << " * Any changes to this file will be overwritten, change the .gc files instead" << std::endl;
    Stream << " */" << std::endl << std::endl;

    Stream << "#ifndef __GC_CONTRACT_" + UppercaseName + "_H__" << std::endl;
    Stream << "#define __GC_CONTRACT_" + UppercaseName + "_H__";
    Stream << std::endl << std::endl;

    Stream << "#include <os/ipc.h>" << std::endl << std::endl;
}

void GrachtGeneratorC::GenerateHeaderEnd(std::ofstream& Stream, const std::string& Name)
{
    std::string UppercaseName = Name;
    std::transform(UppercaseName.begin(), UppercaseName.end(), UppercaseName.begin(),
        [](unsigned char c){ return std::toupper(c); });

    Stream << "#endif //!__GC_CONTRACT_" + UppercaseName + "_H__"<< std::endl;
}

