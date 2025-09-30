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

// Allocates Obj object with OBJ_STRING type,
// then completes the ObjString object for it.
static ObjString* allocateString(char* chars, int length)
{
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    return string;
}

// Creates an ObjString object around a string that we
// already constructed elsewhere.
// Takes ownership of string unlike copyString.
ObjString* takeString(char* chars, int length)
{
    return allocateString(chars, length);
}

// Creates the pure char* string that the ObjString
// object will own, then sends it to allocateString.
// chars parameter points into source string.
ObjString* copyString(const char* chars, int length)
{
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length);
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