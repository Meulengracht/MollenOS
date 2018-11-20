/* Gracht Communication Protocol
 * Copyright 2018 (C) Philip Meulengracht */

#include "common/file.hpp"
#include "parser/parser.hpp"
#include "parser/lexer.hpp"
#include "generators/unit.hpp"
#include "generators/c/c_generator.hpp"
#include <memory>
#include <queue>

std::queue<std::string> GetArguments(int argc, char** argv)
{
    std::queue<std::string> Arguments;

    // Skip the first argument as that is simply path to ourself
    for (int i = 1; i < argc; i++) {
        Arguments.push(std::string(argv[i]));
    }
    return Arguments;
}

int main(int argc, char** argv)
{
    auto Arguments = GetArguments(argc, argv);
    if (Arguments.size() == 0) {
        printf("no arguments are provided\n");
    }
    std::unique_ptr<GrachtUnit> Code(new GrachtUnit(Arguments.front()));
    if (Code->IsValid()) {
        std::unique_ptr<GrachtGeneratorC> Generator(new GrachtGeneratorC(std::move(Code)));
        return Generator->Generate("", "", "");
    }
    return -1;
}
