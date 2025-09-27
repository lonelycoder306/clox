#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
// #include "../include/heap.h"

static void repl()
{
    char line[1024];

    while (true)
    {
        printf(">>> ");
        if (*fgets(line, sizeof(line), stdin) == '\n')
            break;

        interpret(line);
    }
}

static char* readFile(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char *) malloc(fileSize + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read file \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void runFile(const char* path)
{
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, const char* argv[])
{
    clock_t start, end;
    double cpu_time_used;
    (void) cpu_time_used;
    start = clock();
    initVM();
    
    if (argc == 1)
        repl();
    else if (argc == 2)
        runFile(argv[1]);
    else
    {
        fprintf(stderr, "Usage: clox [script]");
        exit(64);
    }

    freeVM();
    //freeHeap(&heap);

    #ifdef TIME_RUN
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Time taken: %f seconds\n", cpu_time_used);
    #endif

    return 0;
}