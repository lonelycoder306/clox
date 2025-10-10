#include "../include/memory.h"
#include "../include/object.h"
#include "../include/vm.h"
// #include "../include/heap.h"
#include <stdlib.h>

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
    if (newSize == 0)
    {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(EXIT_FAILURE);
    return result;
}

static void freeObject(Obj* object)
{
    switch (object->type)
    {
        case OBJ_STRING:
        {
            ObjString* string = (ObjString *) object;
            // Was:
            // FREE_ARRAY(char, string->chars, string->length + 1);
            // FREE(ObjString, object);
            // Now:
            reallocate(object, sizeof(ObjString) +  string->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION:
        {
            ObjFunction* function = (ObjFunction *) object;
            freeChunk(&function->chunk);
            FREE(ObjFunction, object);
            // GC handles the function object's ObjString name.
            break;
        }
        case OBJ_NATIVE:
        {
            FREE(ObjNative, object);
            break;
        }
    }
}

void freeObjects()
{
    Obj* object = vm.objects;
    while (object != NULL)
    {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}