#include "../include/chunk.h"
#include "../include/memory.h"
#include "../include/vm.h"
#include <stdlib.h>

static void initLines(LineArray* array)
{
    array->count = 0;
    array->capacity = 0;
    array->lines = NULL;
    array->offsets = NULL;
}

static void freeLines(LineArray* array)
{
    FREE_ARRAY(int, array->lines, array->capacity);
    FREE_ARRAY(int, array->offsets, array->capacity);
    initLines(array);
}

static void insertLine(LineArray* array, int offset, int line)
{
    // If the added instruction has the same line as the 
    // last instruction added, increment the offset to
    // this new instruction.
    if ((array->count > 0) && 
        (line == array->lines[array->count - 1]))
        {
            array->offsets[array->count - 1] = offset;
            return;
        }
    
    // Otherwise, add the new line and offset to the
    // associative array.
    if (array->capacity < array->count + 1)
    {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->lines = GROW_ARRAY(int, array->lines, oldCapacity,
                    array->capacity);
        array->offsets = GROW_ARRAY(int, array->offsets, oldCapacity,
                    array->capacity);
    }

    array->lines[array->count] = line;
    array->offsets[array->count] = offset;
    array->count++;
}

void initChunk(Chunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    // Clear constant pool for chunk.
    initValueArray(&chunk->constants);
    // Clear line array for errors.
    initLines(&chunk->opLines);
}

void freeChunk(Chunk* chunk)
{
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    // Free and reset the constant pool.
    freeValueArray(&chunk->constants);
    // Free and reset the line array.
    freeLines(&chunk->opLines);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line)
{
    if (chunk->capacity < chunk->count + 1)
    {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code,
                    oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    // Insert the line (if needed) before incrementing
    // the count.
    insertLine(&chunk->opLines, chunk->count, line);
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value)
{
    push(value); // Push onto stack temporarily so GC can reach it.
    writeValueArray(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}

void writeConstant(Chunk* chunk, Value value, int line)
{
    int index = addConstant(chunk, value);
    if (index > 255)
    {
        writeChunk(chunk, OP_CONSTANT_LONG, line);
        writeChunk(chunk, (uint8_t) ((index >> 16) & 0xff), line);
        writeChunk(chunk, (uint8_t) ((index >> 8) & 0xff), line);
        writeChunk(chunk, (uint8_t) (index & 0xff), line);
    }
    else
    {
        writeChunk(chunk, OP_CONSTANT, line);
        writeChunk(chunk, (uint8_t) index, line);
    }
}

int getLine(Chunk* chunk, int offset)
{
    int min = chunk->opLines.offsets[0];

    // min is the largest offset for the first line.
    if (offset <= min)
        return chunk->opLines.lines[0];
    
    for (int i = 0; i < chunk->opLines.count - 1; i++)
    {
        if ((offset > chunk->opLines.offsets[i]) &&
            (offset <= chunk->opLines.offsets[i+1]))
                return chunk->opLines.lines[i+1];
    }

    return -1; // Unreachable.
}