#include "../include/compiler.h"
#include "../include/common.h"
#include "../include/scanner.h"
#include <stdio.h>

void compile(const char* source)
{
    // Set up scanner.
    initScanner(source);

    // Start scanning from the compiler.
    int line = -1; // Invalid line value.
    while (true)
    {
        Token token = scanToken();
        if (token.line != line)
        {
            printf("%4d ", token.line);
            line = token.line;
        }
        else
            printf("   | ");
        
        // No terminator for the token lexeme.
        // So we use its length as a terminator.
        printf("%s, '%.*s'\n", types[token.type], token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }
}