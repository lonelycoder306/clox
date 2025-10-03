#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

typedef enum {
    ACCESS_FIX,
    ACCESS_VAR
} Access;

typedef struct {
    Chunk* chunk; // Chunk being executed in the VM.
    uint8_t* ip; // Pointer to the instruction about to be executed.

    Value* stack;
    int stackCount;
    int stackCapacity;

    Table strings; // To hold our interned strings.

    Table globalNames; // Table of name-(value index) pairs of global variables.
    ValueArray globalValues; // To hold values of global variables.

    Table globalAccess; // Tables of variable-accessibility pairs.
    Table localAccess;

    Obj* objects; // Linked list of (most) allocated objects.
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif