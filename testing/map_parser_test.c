
#define __TEST

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "../services/processmanager/debugger/map_parser.c"

// ./map_parser_test /home/philip/Source/vali-userspace/mesa/build/vali-amd64/gallium-osmesa.map
int main(int argc, char **argv)
{
    struct symbol_context* symbolContext;
    FILE* file;
    long  fileSize;
    void* fileBuffer;
    int   status;

    if (argc < 2) {
        fprintf(stderr, "map_parser_test: invalid number of arguments, %i\n", argc);
        return -1;
    }

    printf("opening file %s\n", argv[1]);
    file = fopen(argv[1], "r");
    if (!file) {
        // map did not exist
        fprintf(stderr, "map file not found at %s\n", argv[1]);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    fileBuffer = malloc(fileSize);
    if (!fileBuffer) {
        fclose(file);
        return -1;
    }

    fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    printf("creating context\n");
    symbolContext = (struct symbol_context*)malloc(sizeof(struct symbol_context));
    if (!symbolContext) {
        free(fileBuffer);
        return -1;
    }

    printf("parsing map file data\n");
    status = SymbolParseMapFile(symbolContext, fileBuffer, fileSize);

    printf("status of parse: %i\n", status);

    free(fileBuffer);
    return status;
}
