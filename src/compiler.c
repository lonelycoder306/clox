#include "../include/compiler.h"
#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/scanner.h"
#include "../include/table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
} Local;

typedef struct {
    Local* vars;
    int count;
    int capacity;
} LocalArray;

typedef struct {
    LocalArray locals;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

static void expression();
static void statement();
static void declaration();
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

static bool check(TokenType type)
{
    return (parser.current.type == type);
}

static bool match(TokenType type)
{
    if (!check(type)) return false;
    advance();
    return true;
}

static void synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF)
    {
        // Possible end of previous statement.
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        // Possible beginning of new statement.
        switch (parser.current.type)
        {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ; // Do nothing.
        }

        advance();
    }
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

// Splits an opcode operand into 3 single-byte chunks.
// Operand is usually some variable index.
static void splitOperand(int index)
{
    emitByte((uint8_t) ((index >> 16) & 0xff));
    emitByte((uint8_t) ((index >> 8) & 0xff));
    emitByte((uint8_t) (index & 0xff));
}

static void initLocalArray(LocalArray* locals)
{
    locals->count = 0;
    locals->capacity = 0;
    locals->vars = NULL;
}

static void freeLocalArray(LocalArray* locals)
{
    FREE_ARRAY(Local, locals->vars, locals->capacity);
    initLocalArray(locals);
}

static void initCompiler(Compiler* compiler)
{
    initLocalArray(&compiler->locals);
    compiler->scopeDepth = 0;
    current = compiler;
}

static void endCompiler()
{
    emitReturn();
    freeLocalArray(&current->locals);
    #ifdef DEBUG_PRINT_CODE
    // Only show chunk code if compiling was
    // successful.
    if (!parser.hadError)
        disassembleChunk(currentChunk(), "code");
    #endif
}

static void beginScope()
{
    current->scopeDepth++;
}

static void endScope()
{
    current->scopeDepth--;
    LocalArray* locals = &current->locals;
    int numPop = 0;

    while (locals->count > 0 &&
            locals->vars[locals->count - 1].depth >
                current->scopeDepth)
    {
        numPop++;
        locals->count--;
    }

    if (numPop == 1)
    {
        emitByte(OP_POP);
        return;
    }

    emitByte(OP_POPN);
    if (numPop < 256)
        emitBytes(OP_SHORT, (uint8_t) numPop);
    else
    {
        emitByte(OP_LONG);
        splitOperand(numPop);
    }
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

    bool canAssign = (precedence <= PREC_ASSIGNMENT);
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL))
        error("Invalid assignment target.");
}

static void number(bool canAssign)
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

static void string(bool canAssign)
{
    // +1 to skip leading ".
    // -2 to trim trailing " (full string would be -1).
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, 
                                    parser.previous.length -2)));
}

static void literal(bool canAssign)
{
    switch (parser.previous.type)
    {
        case TOKEN_FALSE:   emitByte(OP_FALSE); break;
        case TOKEN_NIL:     emitByte(OP_NIL); break;
        case TOKEN_TRUE:    emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

// Returns value slot in vm.globalValues
// associated with given variable identifier.
static int identifierIndex(Token* name)
{
    // See if we already have it.
    ObjString* identifier = copyString(name->start, name->length);
    Value indexValue;
    if (tableGet(&vm.globalNames, OBJ_VAL(identifier), &indexValue))
        // We do.
        return ((int) AS_NUMBER(indexValue));

    int newIndex = vm.globalValues.count;
    writeValueArray(&vm.globalValues, UNDEFINED_VAL);
    tableSet(&vm.globalNames, OBJ_VAL(identifier), NUMBER_VAL((double) newIndex));
    return newIndex;
}

static bool identifiersEqual(Token* a, Token* b)
{
    if (a->length != b->length) return false;
    return (memcmp(a->start, b->start, a->length) == 0);
}

// Adds new local variable to local array in current
// compiler.
static void addLocal(Token name, LocalArray* locals)
{
    if (locals->capacity < locals->count + 1)
    {
        int oldCapacity = locals->capacity;
        locals->capacity = GROW_CAPACITY(oldCapacity);
        locals->vars = GROW_ARRAY(Local, locals->vars, oldCapacity,
                                        locals->capacity);
    }
    
    // Get pointer to last slot in current->locals.
    Local* local = &locals->vars[locals->count++];
    local->name = name;
    local->depth = -1;
}

static int resolveLocal(LocalArray* locals, Token* name)
{
    for (int i = locals->count - 1; i >= 0; i--)
    {
        Local* local = &locals->vars[i];
        if (identifiersEqual(name, &local->name))
        {
            if (local->depth == -1)
                error("Can't read local variable in its own initializer.");
            return i;
        }
    }

    return -1;
}

// Declares local variables.
static void declareVariable()
{
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    for (int i = current->locals.count - 1; i >=0; i--)
    {
        Local* local = &current->locals.vars[i];
        // We check every variable in the current scope, and exit
        // once we're beyond that scope.
        if (local->depth != -1 && local->depth < current->scopeDepth)
            break;
        
        if (identifiersEqual(name, &local->name))
            error("Already a variable with this name in this scope.");
    }

    addLocal(*name, &current->locals);
}

// Consumes identifier tokens for variables.
static int parseVariable(const char* errorMessage)
{
    // Gets the identifier token then sends it to
    // identifierIndex.
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0; // Local variable.

    return identifierIndex(&parser.previous);
}

// Marks a local variable as initialized once its
// declaration is complete.
static void markInitialized(Access accessType)
{
    LocalArray* locals = &current->locals;
    locals->vars[locals->count - 1].depth =
        current->scopeDepth;
    tableSet(&vm.localAccess, NUMBER_VAL((double) (locals->count - 1)),
                                        NUMBER_VAL((double) accessType));
}

// Emits byte-code for global variable declaration
// and marks local variables as initialized.
static void defineVariable(int global, Access accessType)
{
    if (current->scopeDepth > 0)
    {
        markInitialized(accessType);
        return;
    }
    
    // global is the index of the variable's
    // value in vm.globalValues.
    emitByte(OP_DEFINE_GLOBAL);
    if (global > 255)
    {
        emitByte(OP_LONG);
        splitOperand(global);
    }
    else
        emitBytes(OP_SHORT, (uint8_t) global);
    
    tableSet(&vm.globalAccess, NUMBER_VAL((double) global), 
                                NUMBER_VAL((double) accessType));
}

// Emits byte-code for variable access or assignment.
static void namedVariable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    Table* accessTable;
    int arg = resolveLocal(&current->locals, &name);
    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        accessTable = &vm.localAccess;
    }
    else
    {
        arg = identifierIndex(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
        accessTable = &vm.globalAccess;
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        Value value;
        if (tableGet(accessTable, NUMBER_VAL((double) arg), &value) &&
            (int) AS_NUMBER(value) == ACCESS_FIX)
                error("Fixed variable cannot be reassigned.");
        
        expression();
        emitByte(setOp);
    }
    else
        emitByte(getOp);
    
    if (arg > 255)
    {
        emitByte(OP_LONG);
        splitOperand(arg);
    }
    else
        emitBytes(OP_SHORT, (uint8_t) arg);
}

// For variable access/assignment after 
// declaration.
static void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static void binary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // Chunk* chunk = currentChunk();
    // bool zero = (chunk->code[chunk->count - 1] == OP_ZERO);
    // bool one = (chunk->code[chunk->count - 1] == OP_ONE);

    switch (operatorType)
    {
        case TOKEN_PLUS:
        {
            /*if (one)
                chunk->code[chunk->count - 1] = OP_INCREMENT;
            else*/
                emitByte(OP_ADD);
            break;
        }
        case TOKEN_MINUS:
        {
            /*if (one)
                chunk->code[chunk->count - 1] = OP_DECREMENT;
            else*/
                emitByte(OP_SUBTRACT);
            break;
        }
        case TOKEN_STAR:            emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:           emitByte(OP_DIVIDE); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(/*zero ? OP_COMPZER0 : */OP_EQUAL); break;
        case TOKEN_BANG_EQUAL:      emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_GREATER:         emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:            emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitBytes(OP_GREATER, OP_NOT); break;
        default: return; // Unreachable.
    }
}

static void unary(bool canAssign)
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

static void grouping(bool canAssign)
{
    // We've already consumed the (.
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block()
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        declaration();
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration(Access accessType)
{
    int global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL))
        expression();
    else
        emitByte(OP_NIL);
    
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    // Only define if no compilation problem.
    defineVariable(global, accessType);
}

static void printStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void expressionStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    // Print out values of expression statements 
    // by default.
    // emitByte(OP_PRINT);
    emitByte(OP_POP);
}

static void statement()
{
    if (match(TOKEN_PRINT))
        printStatement();
    else
        expressionStatement();
}

static void declaration()
{
    if (match(TOKEN_VAR))
        varDeclaration(ACCESS_VAR);
    else if (match(TOKEN_FIX))
        varDeclaration(ACCESS_FIX);
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
        statement();

    if (parser.panicMode) synchronize();
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
    [TOKEN_IDENTIFIER]      = {variable,    NULL,           PREC_NONE},
    [TOKEN_STRING]          = {string,      NULL,           PREC_NONE},
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
    Compiler compiler;
    initCompiler(&compiler);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF))
        declaration();
    endCompiler(); // Done compiling chunk.
    return !parser.hadError;
}