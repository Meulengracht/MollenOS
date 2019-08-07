/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "common/file.hpp"
#include "parser/parser.hpp"
#include "parser/lexer.hpp"
#include "generators/unit.hpp"
#include "generators/gracht/c/c_generator.hpp"
#include "parser/gracht/gracht_language.hpp"
#include <memory>
#include <queue>
#include <string.h>

struct CompilerArguments {
    std::string CommonHeadersPath;
    std::string ClientImplementationPath;
    std::string ServerImplementationPath;
    std::queue<std::string> Files;
};

void GetArguments(CompilerArguments& Args, int argc, char** argv)
{
    // Skip the first argument as that is simply path to ourself
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--headers=", 10)) {
            Args.CommonHeadersPath = (argv[i] + 10);
        }
        else if (!strncmp(argv[i], "--server-impl=", 14)) {
            Args.ServerImplementationPath = (argv[i] + 14);
        }
        else {
            Args.Files.push(std::string(argv[i]));
        }
    }
}

int main(int argc, char** argv)
{
    CompilerArguments Arguments;
    GetArguments(Arguments, argc, argv);
    if (Arguments.Files.size() == 0) {
        printf("gracht: no arguments are provided\n");
    }
    
    std::shared_ptr<Language>         Lang(new GrachtLanguage());
    std::unique_ptr<GrachtGeneratorC> Generator(new GrachtGeneratorC());

    while (Arguments.Files.size()) {
        std::shared_ptr<GrachtUnit> Code(new GrachtUnit(Arguments.Files.front(), Lang));
        if (Code->IsValid()) {
            if (Generator->Generate(Code, ".", ".") != 0) {
                break;
            }
        }
        Arguments.Files.pop();
    }
    return -1;
}
