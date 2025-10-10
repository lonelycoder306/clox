#ifndef clox_natives_h
#define clox_natives_h

#include "object.h"

Value clockNative(int argCount, Value* args);
Value sqrtNative(int argCount, Value* args);
Value typeNative(int argCount, Value* args);
Value lengthNative(int argCount, Value* args);

ObjNative natives[4];

void defineNatives();

#endif