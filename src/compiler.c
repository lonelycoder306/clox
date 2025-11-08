#include "../include/compiler.h"
#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/natives.h"
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
    bool isCaptured;
} Local;

typedef struct {
    Local* vars;
    int count;
    int capacity;
} LocalArray;

typedef struct {
    uint8_t index; // Stack slot of captured variable.
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    LocalArray locals;
    Upvalue upvalues[UINT8_COUNT]; // Fixed size for simplicity.
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
int continueJump = -1; // End of innermost loop (before loop instruction).
int breakJump = -1; // End of innermost loop (after loop instruction).
int loopDepth = 0; // Depth of innermost loop.

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static Chunk* currentChunk()
{
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message)
{
    // If we are in panic mode, suppress error reporting.
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "Compile Error");

    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (token->type == TOKEN_ERROR);
    else
        fprintf(stderr, " at '%.*s'", token->length, token->start);

    fprintf(stderr, " [line %d]: %s\n", token->line, message);
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
            case TOKEN_MATCH:
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

// Temporarily to exit execution.
// Done at end of chunk.
static void emitReturn()
{
    emitBytes(OP_NIL, OP_RETURN);
}

static void emitConstant(Value value)
{
    // Different code to accommodate our changes.
    writeConstant(currentChunk(), value, parser.previous.line);
}

static int emitJump(uint8_t instruction)
{
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    // Return position of first operand byte.
    return currentChunk()->count - 2;
}

static void patchJump(int offset)
{
    // -1 since the count is 1 more than the index
    // of the last instruction, and another -1 to
    // skip the second operand byte as well.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX)
        error("Too much code to jump over.");
    
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int loopStart)
{
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// Splits an opcode operand into 3 single-byte chunks.
// Operand is usually some variable index.
static void splitOperand(int index)
{
    emitByte((uint8_t) ((index >> 16) & 0xff));
    emitByte((uint8_t) ((index >> 8) & 0xff));
    emitByte((uint8_t) (index & 0xff));
}

static void emitOperand(int operand)
{
    if (operand < 256)
        emitBytes(OP_SHORT, (uint8_t) operand);
    else
    {
        emitByte(OP_LONG);
        splitOperand(operand);
    }
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

static void initCompiler(Compiler* compiler, FunctionType type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->scopeDepth = 0;
    // Null the function then assign in case of
    // GC being triggered.
    compiler->function = newFunction();
    current = compiler;
    if (type != TYPE_SCRIPT)
        // Name string can outlive source string.
        current->function->name = copyString(parser.previous.start,
                                            parser.previous.length);

    initLocalArray(&current->locals);
    int oldCapacity = current->locals.capacity;
    current->locals.capacity = GROW_CAPACITY(oldCapacity);
    current->locals.vars = GROW_ARRAY(Local, current->locals.vars, oldCapacity,
            current->locals.capacity);

    // Slot 0 will hold the function being called.
    Local* local = &current->locals.vars[current->locals.count++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = ""; // Cannot be accessed by user with any identifier.
    local->name.length = 0;
}

static ObjFunction* endCompiler()
{
    emitReturn();
    ObjFunction* function = current->function;
    freeLocalArray(&current->locals);
    #ifdef DEBUG_PRINT_CODE
    // Only show chunk code if compiling was
    // successful.
    if (!parser.hadError)
        disassembleChunk(currentChunk(), function->name == NULL ?
                    "<script>" : function->name->chars);
    #endif

    current = current->enclosing;
    return function;
}

static void beginScope()
{
    current->scopeDepth++;
}

static void endScope()
{
    current->scopeDepth--;
    LocalArray* locals = &current->locals;
    // int numPop = 0;

    while (locals->count > 0 &&
            locals->vars[locals->count - 1].depth >
                current->scopeDepth)
    {
        // numPop++;
        if (locals->vars[locals->count - 1].isCaptured)
            emitByte(OP_CLOSE_UPVALUE);
        else
            emitByte(OP_POP);
        locals->count--;
    }

    // if (numPop == 1)
    // {
    //     emitByte(OP_POP);
    //     return;
    // }

    // emitByte(OP_POPN);
    // if (numPop < 256)
    //     emitBytes(OP_SHORT, (uint8_t) numPop);
    // else
    // {
    //     emitByte(OP_LONG);
    //     splitOperand(numPop);
    // }
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
    local->isCaptured = false;
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

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal)
{
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++)
    {
        Upvalue* upvalue = &compiler->upvalues[i];
        // Variables are only inherited from enclosing functions,
        // and they will thus all still be on the stack (with
        // increasing stack indices) when the current function
        // is defined.
        if ((upvalue->index == index) && (upvalue->isLocal == isLocal))
            return i;
    }

    if (upvalueCount == UINT8_COUNT)
    {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name)
{
    // Global scope.
    if (compiler->enclosing == NULL) return -1;

    // Look for variable in enclosing function.
    int local = resolveLocal(&compiler->enclosing->locals, name);
    if (local != -1)
    {
        // Mark local variable as captured by closure.
        compiler->enclosing->locals.vars[local].isCaptured = true;
        // Found -> capture local as upvalue.
        return addUpvalue(compiler, (uint8_t) local, true);
    }
    
    // Not in enclosing function -> recurse through functions.
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1)
        // Found -> capture upvalue as upvalue.
        return addUpvalue(compiler, (uint8_t) upvalue, false);
    
    // Not found at all -> assumed global.
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
    if (current->scopeDepth == 0) return;
    
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
    emitOperand(global);
    
    tableSet(&vm.globalAccess, NUMBER_VAL((double) global), 
                                NUMBER_VAL((double) accessType));
}

// Emits byte-code for variable access or assignment.
static void namedVariable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    Table* accessTable = NULL; // Dummy initialization.
    bool isUpvalue = false;

    int arg = resolveLocal(&current->locals, &name);
    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        accessTable = &vm.localAccess;
    }
    // Variable is not in current compiler/function's scope.
    else if ((arg = resolveUpvalue(current, &name)) != -1)
    {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
        isUpvalue = true;
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
        if (accessTable != NULL)
        {
           int index = isUpvalue ? current->upvalues[arg].index : arg;
           
            if (tableGet(accessTable, NUMBER_VAL((double) index), &value) &&
                (int) AS_NUMBER(value) == ACCESS_FIX)
                    error("Fixed variable cannot be reassigned.");
        }
        
        expression();
        emitByte(setOp);
    }
    else
        emitByte(getOp);
    
    emitOperand(arg);
}

static uint8_t argumentList()
{
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
                error("Can't have more than 255 arguments.");
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
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

static void call(bool canAssign)
{
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
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

static void and_(bool canAssign)
{
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign)
{
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);

    patchJump(endJump);
}

static void conditional(bool canAssign)
{
    int falseJump = emitJump(OP_JUMP_IF_FALSE);
    // Expression is not falsey -> pop its value.
    emitByte(OP_POP);
    expression();
    int trueJump = emitJump(OP_JUMP);
    patchJump(falseJump);

    consume(TOKEN_COLON, "Expect ':' separator between ternary branches.");
    // Expression is falsey -> pop its value.
    emitByte(OP_POP);
    parsePrecedence(PREC_CONDITIONAL);
    patchJump(trueJump);
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

static void function(FunctionType type)
{
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            current->function->arity++;
            if (current->function->arity > 255)
                errorAtCurrent("Can't have more than 255 parameters.");
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant, ACCESS_VAR);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    emitByte(OP_CLOSURE);
    emitConstant(OBJ_VAL(function));

    for (int i = 0; i < function->upvalueCount; i++)
    {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
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

static void funDeclaration()
{
    int global = parseVariable("Expect function name");
    markInitialized(ACCESS_VAR);
    function(TYPE_FUNCTION);
    defineVariable(global, ACCESS_VAR);
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

static void printStatement()
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void ifStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    // We do this before patching so our jump-if-false
    // instruction can skip this instruction too if needed.
    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void whileStatement()
{
    int surroundBreakJump = breakJump;
    int surroundContinueJump = continueJump;

    loopDepth++;
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    if (continueJump != -1)
        patchJump(continueJump);
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);

    if (breakJump != -1)
        patchJump(breakJump);

    breakJump = surroundBreakJump;
    continueJump = surroundContinueJump;
    loopDepth--;
}

static void forStatement()
{   
    int surroundBreakJump = breakJump;
    int surroundContinueJump = continueJump;

    // Grab the name and slot of the loop variable
    // so we can refer to it later.
    int loopVariable = -1;
    Token loopVariableName;
    loopVariableName.start = NULL;
    
    beginScope();
    loopDepth++;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {}
    else if (match(TOKEN_VAR))
    {
        // Grab the name of the loop variable.
        loopVariableName = parser.current;
        varDeclaration(ACCESS_VAR);
        // Grab its slot as well.
        loopVariable = current->locals.count - 1;
    }
    else
        expressionStatement();
    
    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // Condition.
    }

    if (!match(TOKEN_RIGHT_PAREN))
    {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP); // Increment is only evaluated for side-effects.
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    int innerVariable = -1;
    // If the loop declares a variable.
    if (loopVariable != -1)
    {
        // Create a scope for the copy.
        beginScope();
        // Define a new variable initialized with the
        // current value of the loop variable.
        emitByte(OP_GET_LOCAL);
        emitOperand(loopVariable);
        addLocal(loopVariableName, &current->locals);
        markInitialized(ACCESS_VAR);
        // Keep track of its slot.
        innerVariable = current->locals.count - 1;
    }

    statement();

    if (loopVariable != -1)
    {
        // Store the inner variable back in the loop
        // variable.
        emitByte(OP_GET_LOCAL);
        emitOperand(innerVariable);

        emitByte(OP_SET_LOCAL);
        emitOperand(loopVariable);
        emitByte(OP_POP);

        // Close the temporary scope for the 
        // copy of the loop variable.
        endScope();
    }

    if (continueJump != -1)
        patchJump(continueJump);

    emitLoop(loopStart);

    if (breakJump != -1)
        patchJump(breakJump);

    if (exitJump != -1)
    {
        patchJump(exitJump);
        // Only pop if there is a condition expression
        // in the first place.
        emitByte(OP_POP);
    }

    loopDepth--;
    endScope();

    breakJump = surroundBreakJump;
    continueJump = surroundContinueJump;
}

static void matchStruct()
{
    #define MAX_CASES 100
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'match'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after match value.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before cases.");

    int cases[MAX_CASES];
    int caseNum = 0;

    while (match(TOKEN_IS))
    {
        if (caseNum == 100)
            error("Too many cases in structure.");
        
        if (match(TOKEN_Q_MARK))
        {
            consume(TOKEN_COLON, "Expect ':' after default case.");
            // Pop the match value.
            emitByte(OP_POP);
            statement();
            // Small check.
            if (match(TOKEN_IS))
                error("Cannot have a case after the default case.");
            break;
        }

        // Duplicate the match value so we don't 
        // pop it with OP_EQUAL.
        emitByte(OP_DUP);
        // Compile the case value.
        expression();
        consume(TOKEN_COLON, "Expect ':' after case value.");
        emitByte(OP_EQUAL); // Pops the duplicate.

        int falseJump = emitJump(OP_JUMP_IF_FALSE);
        // If we have a match, we pop the result of
        // the comparison and the match value.
        emitBytes(OP_POPN, OP_SHORT);
        emitByte((uint8_t) 2);
        statement();
        
        cases[caseNum++] = emitJump(OP_JUMP);
        patchJump(falseJump);
        // Pop the result of the comparison if OP_JUMP
        // didn't run.
        emitByte(OP_POP);
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after cases.");

    // Pop the match value if OP_JUMP didn't run.
    emitByte(OP_POP);
    for (int i = 0; i < caseNum; i++)
        patchJump(cases[i]);

    #undef MAX_CASES
}

static void breakStatement()
{
    if (loopDepth == 0)
        error("Cannot use 'break' outside of a loop.");
    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
    breakJump = emitJump(OP_JUMP);
}

static void continueStatement()
{
    if (loopDepth == 0)
        error("Cannot use 'continue' outside of a loop.");
    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
    continueJump = emitJump(OP_JUMP);
}

static void returnStatement()
{
    if (current->type == TYPE_SCRIPT)
        error("Can't return from top-level code.");
    
    if (match(TOKEN_SEMICOLON))
        emitReturn();
    else
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void statement()
{
    if (match(TOKEN_PRINT))
        printStatement();
    else if (match(TOKEN_IF))
        ifStatement();
    else if (match(TOKEN_WHILE))
        whileStatement();
    else if (match(TOKEN_FOR))
        forStatement();
    else if (match(TOKEN_MATCH))
        matchStruct();
    else if (match(TOKEN_BREAK))
        breakStatement();
    else if (match(TOKEN_CONTINUE))
        continueStatement();
    else if (match(TOKEN_RETURN))
        returnStatement();
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
        expressionStatement();
}

static void declaration()
{
    if (match(TOKEN_VAR))
        varDeclaration(ACCESS_VAR);
    else if (match(TOKEN_FIX))
        varDeclaration(ACCESS_FIX);
    else if (match(TOKEN_FUN))
        funDeclaration();
    else
        statement();

    if (parser.panicMode) synchronize();
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]      = {grouping,    call,           PREC_CALL},
    [TOKEN_RIGHT_PAREN]     = {grouping,    NULL,           PREC_NONE},
    [TOKEN_LEFT_BRACE]      = {NULL,        NULL,           PREC_NONE},
    [TOKEN_RIGHT_BRACE]     = {NULL,        NULL,           PREC_NONE},
    [TOKEN_COMMA]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_DOT]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_MINUS]           = {unary,       binary,         PREC_TERM},
    [TOKEN_PLUS]            = {NULL,        binary,         PREC_TERM},
    [TOKEN_SEMICOLON]       = {NULL,        NULL,           PREC_NONE},
    [TOKEN_Q_MARK]          = {NULL,        conditional,    PREC_CONDITIONAL},
    [TOKEN_COLON]           = {NULL,        NULL,           PREC_NONE},
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
    [TOKEN_AND]             = {NULL,        and_,           PREC_AND},
    [TOKEN_CLASS]           = {NULL,        NULL,           PREC_NONE},
    [TOKEN_ELSE]            = {NULL,        NULL,           PREC_NONE},
    [TOKEN_FALSE]           = {literal,     NULL,           PREC_NONE},
    [TOKEN_FOR]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_FUN]             = {NULL,        NULL,           PREC_NONE},
    [TOKEN_IF]              = {NULL,        NULL,           PREC_NONE},
    [TOKEN_NIL]             = {literal,     NULL,           PREC_NONE},
    [TOKEN_OR]              = {NULL,        or_,            PREC_OR},
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

ObjFunction* compile(const char* source)
{
    // Set up scanner.
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    defineNatives();

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF))
        declaration();

    ObjFunction* function = endCompiler();
    return (parser.hadError ? NULL : function);
}