#define _CRT_SECURE_NO_WARNINGS

#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
// #include "../include/heap.h"

typedef struct {
    char* string;
    int length;
    int capacity;
} inputLine;

static void growLine(inputLine* line, const char* temp, int shift)
{
    if (line->capacity < line->length + (int) strlen(temp) + shift)
    {
        int oldCapacity = line->capacity;
        line->capacity = GROW_CAPACITY(line->capacity);
        line->string = GROW_ARRAY(char, line->string, oldCapacity,
                            line->capacity);
    }
}

static void repl()
{
    inputLine line = { .string = NULL, .length = 1024, .capacity = 1024 };
    line.string = GROW_ARRAY(char, line.string, 0, line.capacity);
    char temp[256];

    while (true)
    {
        // Clear line and temp each iteration.
        memset(line.string, '\0', line.capacity);
        // memset(line, '\0', line.capacity);
        memset(temp, '\0', sizeof(temp));
        printf(">>> ");

        do
        {
            if (strlen(line.string) != 0) // Beginning of input.
                printf("... ");
            memset(temp, '\0', sizeof(temp));
            fgets(temp, sizeof(temp), stdin);
            // strlen - 2 since the last character is \n (when Enter is pressed),
            // not \.
            if ((strlen(temp) > 1) && (temp[strlen(temp) - 2] == '\\'))
            {
                growLine(&line, temp, -1);
                strncat(line.string, temp, strlen(temp) - 2);
                strcat(line.string, "\n");
                line.length += (int) strlen(temp) - 1;
            }
            else
            {
                growLine(&line, temp, 1);
                strcat(line.string, temp);
                strcat(line.string, "\n");
                line.length += (int) strlen(temp) + 1;
            }
        } while ((strlen(temp) > 1) && (temp[strlen(temp) - 2] == '\\'));

        if (line.string[0] == '\n')
            break;

        interpret(line.string);
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