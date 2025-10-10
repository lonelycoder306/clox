#include "../include/object.h"
#include "../include/memory.h"
#include "../include/value.h"
#include "../include/vm.h"
#include <stdio.h>
#include <string.h>

#define ALLOCATE_OBJ(type, objectType) \
        (type *) allocateObject(sizeof(type), objectType)

// Allocates pure Obj object with specified type.
// The size allocated is for the specific object type,
// not for Obj (so the size fits the specific object needed).
static Obj* allocateObject(size_t size, ObjType type)
{
    Obj* object = (Obj*) reallocate(NULL, 0, size);
    object->type = type;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

// Makes a completely bare function object.
ObjFunction* newFunction()
{
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative* newNative(NativeFn function)
{
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

// Function to hash a string.
uint32_t hashString(const char* key, int length)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++)
    {
        hash ^= (uint8_t) key[i];
        hash *= 16777619;
    }
    return hash;
}

// Creates an ObjString object with enough size.
// No initial char string stored in the object.
ObjString* makeString(int length)
{
    ObjString* string = (ObjString *) allocateObject(
            sizeof(ObjString) + length + 1, OBJ_STRING);
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
    string->length = length;
    return string;
}

// Creates the full ObjString object with its char
// array taken from the given string.
ObjString* copyString(const char* chars, int length)
{
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
                                            hash);
    if (interned != NULL) return interned;
    
    ObjString* string = makeString(length);

    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    string->hash = hash;

    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);

    return string;
}

static void printFunction(ObjFunction* function)
{
    if (function->name == NULL)
    {
        printf("<script>");
        return;
    }
    
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value))
    {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
    }
}