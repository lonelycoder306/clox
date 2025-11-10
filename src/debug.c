#include "../include/debug.h"
#include "../include/object.h"
#include "../include/value.h"
#include "../include/vm.h"
#include <stdio.h>

void disassembleChunk(Chunk* chunk, const char* name)
{
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;)
        offset = disassembleInstruction(chunk, offset);
}

static int constantInstruction(const char* name, Chunk* chunk, int offset)
{
    uint8_t index = chunk->code[offset + 1];
    printf("%-20s %4d '", name, index);
    printValue(chunk->constants.values[index]);
    printf("'\n");
    return offset + 2;
}

static int constLongInstruction(const char* name, Chunk* chunk, int offset)
{
    int index = ((chunk->code[offset + 1] << 16) |
                    (chunk->code[offset + 2] << 8) |
                    (chunk->code[offset + 3]));
    printf("%-20s %4d '", name, index);
    printValue(chunk->constants.values[index]);
    printf("'\n");
    return offset + 4;
}

static int simpleInstruction(const char* name, int offset)
{
    printf("%s\n", name);
    return offset + 1;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset)
{
    uint8_t operand = chunk->code[offset + 1];
    printf("%-20s %4d\n", name, operand);
    return offset + 2;
}

static int valueInstruction(const char* name, Chunk* chunk, int offset)
{
    if (chunk->code[++offset] == OP_CONSTANT)
        return constantInstruction(name, chunk, offset);
    else
        return constLongInstruction(name, chunk, offset);
}

// For generic instructions with variable-size operands.
static int operInstruction(const char* name, Chunk* chunk, int offset)
{
    int operand;
    int off;
    if (chunk->code[offset + 1] == OP_LONG)
    {
        operand = ((chunk->code[offset + 2] << 16) |
                    (chunk->code[offset + 3] << 8) |
                    (chunk->code[offset + 4]));
        off = 5;
    }
    else
    {
        operand = (uint8_t)chunk->code[offset + 2];
        off = 3;
    }
    printf("%-20s %4d\n", name, operand);
    return offset + off;
}

// For local and global variable instructions.
static int varInstruction(const char* name, Chunk* chunk, int offset)
{
    int index;
    int off;
    if (chunk->code[offset + 1] == OP_LONG)
    {
        index = ((chunk->code[offset + 2] << 16) |
                (chunk->code[offset + 3] << 8) |
                (chunk->code[offset + 4]));
        off = 5;
    }
    else
    {
        index = (uint8_t) chunk->code[offset + 2];
        off = 3;
    }

    printf("%-20s %4s  %d\n", name, "VAR", index);
    return offset + off;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk,
                            int offset)
{
    uint16_t jump = (uint16_t) (chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    // Print the offset of the instruction we will jump to.
    printf("%-20s %4d -> %d\n", name, offset, 
            offset + 3 + sign * jump);
    return offset + 3;
}

int disassembleInstruction(Chunk* chunk, int offset)
{
    printf("%04d ", offset);
    int line = getLine(chunk, offset);
    if (offset > 0 &&
        line == getLine(chunk, offset - 1))
            printf("   | ");
    else
        printf("%4d ", line);

    uint8_t instruction = chunk->code[offset];
    switch (instruction)
    {
        case OP_ZERO:
            return simpleInstruction("OP_ZERO", offset);
        case OP_ONE:
            return simpleInstruction("OP_ONE", offset);
        case OP_TWO:
            return simpleInstruction("OP_TWO", offset);
        case OP_MINUSONE:
            return simpleInstruction("OP_MINUSONE", offset);
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:
            return constLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_DUP:
            return simpleInstruction("OP_DUP", offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_POPN:
            return operInstruction("OP_POPN", chunk, offset);
        case OP_DEFINE_GLOBAL:
            return varInstruction("OP_DEFINE_GLOBAL", chunk, offset); 
        case OP_GET_GLOBAL:
            return varInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_GET_LOCAL:
            return varInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_GLOBAL:
            return varInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_SET_LOCAL:
            return varInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_UPVALUE:
            return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
            return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_COMPZER0:
            return simpleInstruction("OP_COMPZERO", offset);
        case OP_INCREMENT:
            return simpleInstruction("OP_INCREMENT", offset);
        case OP_DECREMENT:
            return simpleInstruction("OP_DECREMENT", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return byteInstruction("OP_CALL", chunk, offset);
        case OP_CLOSURE:
        {
            int index;
            if (chunk->code[++offset] == OP_CONSTANT)
            {
                index = chunk->code[++offset];
                offset++;
            }
            else
            {
                index = (uint32_t) ((chunk->code[offset + 1] << 16) |
                                    (chunk->code[offset + 2] << 8) |
                                    chunk->code[offset + 3]);
                offset += 4;
            }

            printf("%-20s %4d\n", "OP_CLOSURE", index);
            printValue(chunk->constants.values[index]);
            printf("\n");

            ObjFunction* function = AS_FUNCTION(chunk->constants.values[index]);
            for (int i = 0; i < function->upvalueCount; i++)
            {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d    |                     %s  %d\n",
                    offset - 2, isLocal ? "local" : "upvalue", index);
            }

            return offset;
        }
        case OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_CLASS:
            return valueInstruction("OP_CLASS", chunk, offset);
        case OP_GET_PROPERTY:
            return valueInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_SET_PROPERTY:
            return valueInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("UNKNOWN OPCODE %d\n", instruction);
            return offset + 1;
    }
}