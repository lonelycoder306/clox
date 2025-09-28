#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// One-byte (uint8_t) operation code
// for each instruction.
typedef enum {
    OP_ZERO,
    OP_ONE,
    OP_TWO,
    OP_MINUSONE,
    OP_CONSTANT, // Opcode | position in constant pool.
    OP_CONSTANT_LONG, // Opcode | position in constant pool.
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_COMPZER0,
    OP_INCREMENT,
    OP_DECREMENT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_RETURN
} OpCode;

typedef struct {
    int* lines;
    int* offsets;
    int count;
    int capacity;
} LineArray;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    LineArray opLines;
    ValueArray constants;
} Chunk;

// Initialize an empty chunk.
void initChunk(Chunk* chunk);
// Deallocate a chunk.
void freeChunk(Chunk* chunk);
// Add single byte to chunk.
void writeChunk(Chunk* chunk, uint8_t byte, int line);
// More constants in chunk.
// The only function we should use to add a constant
// to the chunk constant pool.
void writeConstant(Chunk* chunk, Value value, int line);
// Add constant to chunk pool.
int addConstant(Chunk* chunk, Value value);
// Get line of instruction by offset.
int getLine(Chunk* chunk, int offset);

#endif