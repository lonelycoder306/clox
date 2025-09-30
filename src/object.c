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

// Creates an ObjString object with enough size.
// No initial char string stored in the object.
ObjString* makeString(int length)
{
    ObjString* string = (ObjString *) allocateObject(
            sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    return string;
}

// Creates the full ObjString object with its char
// array taken from the given string.
ObjString* copyString(const char* chars, int length)
{
    ObjString* string = makeString(length);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return string;
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value))
    {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}