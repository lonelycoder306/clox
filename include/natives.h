#ifndef clox_natives_h
#define clox_natives_h

#include "common.h"
#include "object.h"

bool clockNative(int argCount, Value* args);
bool sqrtNative(int argCount, Value* args);
bool typeNative(int argCount, Value* args);
bool lengthNative(int argCount, Value* args);

ObjNative natives[4];

void defineNatives();

#endif