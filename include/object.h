#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)

#define AS_STRING(value)    ((ObjString *) AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString *) AS_OBJ(value))->chars)
#define AS_FUNCTION(value)  ((ObjFunction *) AS_OBJ(value))
#define AS_NATIVE(value) \
        (((ObjNative *) AS_OBJ(value))->function)
#define AS_CLOSURE(value)   ((ObjClosure *) AS_OBJ(value))

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

// Value parameter points to the VM's stack.
typedef bool (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    const char* name; // Bare string name.
    NativeFn function;
    int arity;
} ObjNative;

typedef struct {
    Obj obj;
    Value* location; // Reference to variable itself.
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount; // In case GC needs it after function is freed.
} ObjClosure;

struct ObjString {
    Obj obj;
    int length;
    uint32_t hash;
    // Was:
    // char* chars;
    // Now:
    char chars[];
};

ObjFunction* newFunction();
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjString* makeString(int length);
ObjString* copyString(const char* chars, int length);
uint32_t hashString(const char* key, int length);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type)
{
    return (IS_OBJ(value) && AS_OBJ(value)->type == type);
}

#endif