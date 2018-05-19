/* File2C Utility 
 * Author: Philip Meulengracht
 * Date: 17-05-18
 * Used as a utility for MollenOS to convert file data to c-arrays */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    // Variables
    unsigned int i, FileSize, UseComma;
    char *Buffer = NULL, *ArrayName = NULL;
    FILE *Input = NULL, *Output = NULL;

    // Sanitize parameters
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input> <output> <array_name>\n", argv[0]);
        return -1;
    }

    // Open input file
    Input = fopen(argv[1], "rb");
    if (Input == NULL) {
        fprintf(stderr, "%s: can't open %s for reading\n", argv[0], argv[1]);
        return -1;
    }

    // Open output file
    Output = fopen(argv[2], "w");
    if (Output == NULL) {
        fprintf(stderr, "%s: can't open %s for writing\n", argv[0], argv[1]);
        fclose(Input);
        return -1;
    }

    // Get the file length
    fseek(Input, 0, SEEK_END);
    FileSize = ftell(Input);
    fseek(Input, 0, SEEK_SET);

    // Read the file and cleanup
    Buffer = (char*)malloc(FileSize);
    fread(Buffer, FileSize, 1, Input);
    fclose(Input);

    // Write the output file
    ArrayName   = argv[3];
    UseComma    = 0;
	fprintf(Output, "/* Automatically generated file, do not change contents.\n");
	fprintf(Output, " * The below file data is from %s. */\n\n", argv[1]);
    fprintf(Output, "const char %s[%i] = {", ArrayName, FileSize);
    for (i = 0; i < FileSize; ++i) {
        if (UseComma) {
            fprintf(Output, ", ");
        }
        else {
            UseComma = 1;
        }
        if ((i % 11) == 0) {
            fprintf(Output, "\n    ");
        }
        fprintf(Output, "0x%.2x", Buffer[i] & 0xff);
    }

    // Write last and finish
    fprintf(Output, "\n};\n\n");
    fprintf(Output, "const int %s_length = %i;\n", ArrayName, FileSize);
    return fclose(Output);
}
