#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"

// One-byte (uint8_t) operation code
// for each instruction.
typedef enum {
    OP_RETURN
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
} Chunk;

void initChunk(Chunk* chunk); // Initialize an empty chunk.
void freeChunk(Chunk* chunk); // Deallocate a chunk.
void writeChunk(Chunk* chunk, uint8_t byte); // Add byte to chunk.

#endif