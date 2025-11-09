#ifndef clox_vm_h
#define clox_vm_h

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

typedef enum {
    ACCESS_FIX,
    ACCESS_VAR
} Access;

// Single ongoing function call.
typedef struct {
    ObjClosure* closure; // Pointer to callee's closure.
    uint8_t* ip; // Caller's ip to resume from after return.
    Value* slots; // Pointer to first slot function can use.
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value* stack;
    int stackCount;
    int stackCapacity;

    Table strings; // To hold our interned strings.

    Table globalNames; // Table of name-(value index) pairs of global variables.
    ValueArray globalValues; // To hold values of global variables.

    Table globalAccess; // Tables of variable-accessibility pairs.
    Table localAccess;

    ObjUpvalue* openUpvalues;

    size_t bytesAllocated; // Number of bytes VM has allocated.
    size_t nextGC; // Threshold for next collection.
    Obj* objects; // Linked list of (most) allocated objects.

    // For GC tri-color traversal.
    int grayCount;
    int grayCapacity;
    Obj** grayStack;
} VM;

extern VM vm;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif