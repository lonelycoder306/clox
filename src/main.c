#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"

int main(int argc, const char* argv[])
{
    Chunk chunk;
    initChunk(&chunk);

    for (int i = 0; i < 300; i++)
        writeConstant(&chunk, i, i*10);

    writeChunk(&chunk, OP_RETURN, 123);

    disassembleChunk(&chunk, "test chunk");
    freeChunk(&chunk);

    return 0;
}