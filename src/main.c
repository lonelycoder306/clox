#include "../include/chunk.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/vm.h"
#include <stdio.h>
#include <time.h>
// #include "../include/heap.h"

int main(int argc, const char* argv[])
{
    clock_t start, end;
    [[maybe_unused]] double cpu_time_used;
    start = clock();
    initVM();
    
    // initHeap(&heap);
    // allocateHeap(&heap, 1000);
    Chunk chunk;
    initChunk(&chunk);

    // for (int i = 0; i < 300; i++)
    //     writeConstant(&chunk, i, i*10);
    
    // 5 - 3
    writeConstant(&chunk, 5, 123);
    writeConstant(&chunk, 3, 123);
    writeChunk(&chunk, OP_NEGATE, 123);
    writeChunk(&chunk, OP_ADD, 123);
    writeChunk(&chunk, OP_RETURN, 123);

    //disassembleChunk(&chunk, "test chunk");
    interpret(&chunk);
    freeVM();
    freeChunk(&chunk);

    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    #ifdef TIME_RUN
    printf("Time taken: %f seconds\n", cpu_time_used);
    #endif

    //freeHeap(&heap);

    return 0;
}