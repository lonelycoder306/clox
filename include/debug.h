#ifndef clox_debug_h
#define clox_debug_h

#include "chunk.h"

// Function to reverse assembly process, i.e.,
// output instructions given a series of binary
// instructions.
// Useful for language maintainers, not users.
void disassembleChunk(Chunk* chunk, const char* name);
// Disassembles each instruction in the chunk.
// Not static since VM will also use it.
int disassembleInstruction(Chunk* chunk, int offset);

#endif