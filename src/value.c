#include "../include/value.h"
#include "../include/memory.h"
#include "../include/object.h"
#include <stdio.h>
#include <string.h>

void initValueArray(ValueArray* array)
{
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
}

void writeValueArray(ValueArray* array, Value value)
{
    if (array->capacity < array->count + 1)
    {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(array->capacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity,
                                    array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array)
{
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void markValueArray(ValueArray* array)
{
    for (int i = 0; i < array->count; i++)
        markValue(array->values[i]);
}

static uint32_t hashDouble(double value)
{
    union BitCast {
        double value;
        uint32_t ints[2];
    };

    union BitCast cast;
    cast.value = (value) +  1.0;
    // Just splits the double (+ 1) and adds its parts.
    return cast.ints[0] + cast.ints[1];
}

uint32_t hashValue(Value value)
{
    switch (value.type)
    {
        case VAL_BOOL:      return AS_BOOL(value) ? 3 : 5;
        case VAL_NIL:       return 7;
        case VAL_NUMBER:    return hashDouble(AS_NUMBER(value));
        case VAL_OBJ:       return AS_STRING(value)->hash;
        case VAL_EMPTY:     return 0;
        default:            return 0; // Unreachable.
    }
}

void printValue(Value value)
{
    switch (value.type)
    {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:       printf("nil"); break;
        case VAL_NUMBER:    printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ:       printObject(value); break;
        default:
            ;
    }
}

bool valuesEqual(Value a, Value b)
{
    if (a.type != b.type) return false;
    switch (a.type)
    {
        case VAL_BOOL:      return (AS_BOOL(a) == AS_BOOL(b));
        case VAL_NIL:       return true;
        case VAL_NUMBER:    return (AS_NUMBER(a) == AS_NUMBER(b));
        case VAL_OBJ:       return (AS_OBJ(a) == AS_OBJ(b));
        default:            return false; // Unreachable.
    }
}