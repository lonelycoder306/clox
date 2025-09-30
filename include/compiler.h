#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

// Returns true if compilation succeeded;
// false otherwise.
bool compile(const char* source, Chunk* chunk);

#endif