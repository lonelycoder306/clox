#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
// #include "../include/heap.h"

int main(int argc, const char* argv[])
{
    // initHeap(&heap);
    // allocateHeap(&heap, 1000);
    Chunk chunk;
    initChunk(&chunk);

    // for (int i = 0; i < 300; i++)
    //     writeConstant(&chunk, i, i*10);

    writeConstant(&chunk, 1, 123);
    writeConstant(&chunk, 2, 123);

    writeChunk(&chunk, OP_RETURN, 122);

    writeConstant(&chunk, 3, 124);

    disassembleChunk(&chunk, "test chunk");
    freeChunk(&chunk);

    //freeHeap(&heap);

    return 0;
}