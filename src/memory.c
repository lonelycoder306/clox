#include "../include/memory.h"
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