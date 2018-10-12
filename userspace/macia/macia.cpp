/* The Macia Language (MACIA)
 *
 * Copyright 2016, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Macia - Compiler Suite
 */

#include <iostream>
#include <fstream>
#include "lexer/scanner.h"
#include "parser/parser.h"
#include "generator/generator.h"
#include "interpreter/interpreter.h"

// Supported arguments
// -o        outfile 
// -r        run / interpret
// [ files ] the files to be compiled

int main(int argc, char* argv[])
{
    std::ifstream fs_in;
	Interpreter *vm = NULL;
	Generator *ilgen = NULL;
	Scanner *scrambler = NULL;
	Parser *parser = NULL;

#ifdef DIAGNOSE
	printf("macia-lang compiler %s - 2018 oct 12\n", VERSION);
	printf(" > author: %s\n\n", AUTHOR);
#endif

    // Sanitize input parameters
    if (argc == 1) {
        printf(" > invalid number of arguments\n");
        return -1;
    }

	scrambler = new Scanner();

#ifdef DIAGNOSE
	printf(" - Scanning (flength = %u)\n", 0);
#endif
	if (scrambler->Scan(NULL, 0)) {
		printf("Failed to scramble file\n");
		goto Cleanup;
	}

#ifdef DIAGNOSE
	printf(" - Parsing (elements = %u)\n", (unsigned)scrambler->GetElements().size());
#endif

	parser = new Parser(scrambler->GetElements());
	if (parser->Parse()) {
		printf("Failed to parse file\n");
		goto Cleanup;
	}

#ifdef DIAGNOSE
	printf(" - Generating IL (Bytecode)\n");
#endif

	ilgen = new Generator(parser->GetProgram());
	if (ilgen->Generate()) {
		printf("Failed to create bytecode from the AST\n");
		goto Cleanup;
	}
	ilgen->SaveAs("test.mo");

#ifdef DIAGNOSE
	printf(" - Executing the code\n");
#endif

	vm = new Interpreter(ilgen->GetPool());
	printf("The interpreter finished with result %i\n", vm->Execute());
	delete vm;

Cleanup:
#ifdef DIAGNOSE
	printf(" - Cleaning up & exitting\n");
#endif

	if (ilgen != NULL) {
		delete ilgen;
	}

	if (parser != NULL) {
		delete parser;
	}

	if (scrambler != NULL) {
		delete scrambler;
	}
	return 0;
}

