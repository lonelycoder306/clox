#ifndef clox_natives_h
#define clox_natives_h

#include "object.h"

Value clockNative(int argCount, Value* args);
Value sqrtNative(int argCount, Value* args);
Value typeNative(int argCount, Value* args);

typedef struct {
    const char* name;
    NativeFn function;
} NativeObj;

static NativeObj natives[] = {
    {"clock", clockNative},
    {"sqrt", sqrtNative},
    {"type", typeNative}
};

static int nativesCount = 3;

#endif