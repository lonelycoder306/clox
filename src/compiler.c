#include "../include/compiler.h"
#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/scanner.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_CONDITIONAL,   // ?:
    PREC_OR,            // or
    PREC_AND,           // and
    PREC_EQUALITY,       // ==
    PREC_COMPARISON,    // < > <= >=
    PREC_TERM,          // + -
    PREC_FACTOR,        // * /
    PREC_UNARY,         // ! -
    PREC_CALL,          // . ()
    PREC_PRIMARY    
} Precedence;

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;

static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static Chunk* currentChunk()
{
    return compilingChunk;
}

static void errorAt(Token* token, const char* message)
{
    // If we are in panic mode, suppress error reporting.
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (token->type == TOKEN_ERROR);
    else
        fprintf(stderr, " at '%.*s'", token->length, token->start);

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// Error for token just consumed.
static void error(const char* message)
{
    errorAt(&parser.previous, message);
}

// Error for current token.
static void errorAtCurrent(const char* message)
{
    errorAt(&parser.current, message);
}

static void advance()
{
    // Stores current token for later.
    // Can retrieve lexeme of just-consumed token.
    parser.previous = parser.current;

    // Keep reading until regular Token or EOF.
    // Parser only sees valid tokens.
    while (true)
    {
        // Read and store token for later.
        parser.current = scanToken();
        // Break at non-error token or EOF.
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }

    errorAtCurrent(message);
}

// Byte is opcode or operand.
static void emitByte(uint8_t byte)
{
    writeChunk(currentChunk(), byte, parser.previous.line);
}

// One byte opcode followed by operand.
static void emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

// Temporarily to print out value of expression.
// Done at end of chunk.
static void emitReturn()
{
    emitByte(OP_RETURN);
}

static void emitConstant(Value value)
{
    // Different code to accommodate our changes.
    writeConstant(currentChunk(), value, parser.previous.line);
}

static void endCompiler()
{
    emitReturn();
    #ifdef DEBUG_PRINT_CODE
    // Only show chunk code if compiling was
    // successful.
    if (!parser.hadError)
        disassembleChunk(currentChunk(), "code");
    #endif
}

static void parsePrecedence(Precedence precedence)
{
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    // We cannot start with this token, so report error.
    if (prefixRule == NULL)
    {
        error("Expect expression.");
        return;
    }

    prefixRule();

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

static void number()
{
    double value = strtod(parser.previous.start, NULL);
    if (value == 0)
        emitByte(OP_ZERO);
    else if (value == 1)
        emitByte(OP_ONE);
    else if (value == 2)
        emitByte(OP_TWO);
    else if (value == -1)
        emitByte(OP_MINUSONE);
    else
        emitConstant(NUMBER_VAL(value));
}

static void literal()
{
    switch (parser.previous.type)
    {
        case TOKEN_FALSE:   emitByte(OP_FALSE); break;
        case TOKEN_NIL:     emitByte(OP_NIL); break;
        case TOKEN_TRUE:    emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

static void binary()
{
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    Chunk* chunk = currentChunk();
    bool zero = (*(chunk->code + chunk->count - 1) == OP_ZERO);

    switch (operatorType)
    {
        case TOKEN_PLUS:            emitByte(OP_ADD); break;
        case TOKEN_MINUS:           emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:            emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:           emitByte(OP_DIVIDE); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(zero ? OP_COMPZER0 : OP_EQUAL); break;
        case TOKEN_BANG_EQUAL:      emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_GREATER:         emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:            emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitBytes(OP_GREATER, OP_NOT); break;
        default: return; // Unreachable.
    }
}

static void unary()
{
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType)
    {
        case TOKEN_BANG:    emitByte(OP_NOT); break;
        case TOKEN_MINUS:   emitByte(OP_NEGATE); break;
        default: return; // Unreachable.
    }
}

static void grouping()
{
    // We've already consumed the (.
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]      = {grouping,    NULL,           PREC_NONE},
    [TOKEN_RIGHT_PAREN]     = {grouping,    NULL,           PREC_NONE},
    [TOKEN_LEFT_BRACE]      = {NULL,        NULL,           PREC_NONE},
    [TOKEN_RIGHT_BRACE]     = {NULL,        NULL,           PREC_NONE},
    [TOKEN_COMMA]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_DOT]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_MINUS]           = {unary,       binary,         PREC_TERM},
    [TOKEN_PLUS]            = {NULL,        binary,         PREC_TERM},
    [TOKEN_SEMICOLON]       = {NULL,        NULL,           PREC_NONE},
    [TOKEN_SLASH]           = {NULL,        binary,         PREC_FACTOR},
    [TOKEN_STAR]            = {NULL,        binary,         PREC_FACTOR},
    [TOKEN_BANG]            = {unary,       NULL,           PREC_NONE},
    [TOKEN_BANG_EQUAL]      = {NULL,        binary,         PREC_EQUALITY},
    [TOKEN_EQUAL]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_EQUAL_EQUAL]     = {NULL,        binary,         PREC_EQUALITY},
    [TOKEN_GREATER]         = {NULL,        binary,         PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]   = {NULL,        binary,         PREC_COMPARISON},
    [TOKEN_LESS]            = {NULL,        binary,         PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]      = {NULL,        binary,         PREC_COMPARISON},
    [TOKEN_IDENTIFIER]      = {NULL,        NULL,           PREC_NONE},
    [TOKEN_STRING]          = {NULL,        NULL,           PREC_NONE},
    [TOKEN_NUMBER]          = {number,      NULL,           PREC_NONE},
    [TOKEN_AND]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_CLASS]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_ELSE]            = {NULL,        NULL,           PREC_NONE},
    [TOKEN_FALSE]           = {literal,     NULL,           PREC_NONE},
    [TOKEN_FOR]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_FUN]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_IF]              = {NULL,        NULL,           PREC_NONE},
    [TOKEN_NIL]             = {literal,     NULL,           PREC_NONE},
    [TOKEN_OR]              = {NULL,        NULL,           PREC_NONE},
    [TOKEN_PRINT]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_RETURN]          = {NULL,        NULL,           PREC_NONE},
    [TOKEN_SUPER]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_THIS]            = {NULL,        NULL,           PREC_NONE},
    [TOKEN_TRUE]            = {literal,     NULL,           PREC_NONE},
    [TOKEN_VAR]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_WHILE]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_ERROR]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_EOF]             = {NULL,        NULL,           PREC_NONE},
};

static ParseRule* getRule(TokenType type)
{
    return &rules[type];
}

bool compile(const char* source, Chunk* chunk)
{
    // Set up scanner.
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    expression(); // Parse a single expression.
    // Check for sentinel token.
    consume(TOKEN_EOF, "Expect end of expression.");
    endCompiler(); // Done compiling chunk.
    return !parser.hadError;
}