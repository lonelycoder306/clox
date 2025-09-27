#include "../include/vm.h"
#include "../include/common.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/value.h"
#include <stdio.h>

VM vm;

static void resetStack()
{
    vm.stackCount = 0; // Runs multiple times.
}

void initVM()
{
    // Both lines run once only.
    vm.stack = NULL;
    vm.stackCapacity = 0;
    resetStack();
}

void freeVM()
{
    FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
}

void push(Value value)
{
    /**(vm.stack.values + vm.stack.count) = value;
    vm.stack.count++;*/
    if (vm.stackCapacity < vm.stackCount + 1)
    {
        int oldCapacity = vm.stackCapacity;
        vm.stackCapacity = GROW_CAPACITY(oldCapacity);
        vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stackCapacity);
    }

    vm.stack[vm.stackCount] = value;
    vm.stackCount++;
}

Value pop()
{
    vm.stackCount--;
    return vm.stack[vm.stackCount];
}

static InterpretResult run()
{
    #define READ_BYTE() (*vm.ip++) // Dereference then increment.
    #define READ_TRIBYTE() ((READ_BYTE() << 16) | \
                          (READ_BYTE() << 8) | \
                          READ_BYTE())
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
    #define READ_CONSTLONG() (vm.chunk->constants.values[READ_TRIBYTE()])
    #define BINARY_OP(op) \
            do \
            { \
                double b = pop(); \
                double a = pop(); \
                push(a op b); \
            } while (false)

    #ifdef DEBUG_TRACE_EXECUTION
        printf("== debug trace == ");
    #endif

    while (true)
    {
        #ifdef DEBUG_TRACE_EXECUTION
            printf("          ");
            // Our stack is empty before the first instruction executes.
            // So this only starts printing after (at least) the first
            // instruction is disassembled.
            Value* stackTop = vm.stack + vm.stackCount;
            for (Value* slot = vm.stack; slot < stackTop; slot++)
            {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            // Disassemble the next instruction we will execute
            // prior to execution.
            disassembleInstruction(vm.chunk, (int) (vm.ip - vm.chunk->code));
        #endif
        
        uint8_t instruction;
        switch (instruction = READ_BYTE())
        {
            case OP_CONSTANT:
            {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_CONSTANT_LONG:
            {
                Value constant = READ_CONSTLONG();
                push(constant);
                break;
            }
            case OP_ADD:        BINARY_OP(+); break;
            case OP_SUBTRACT:   BINARY_OP(-); break;
            case OP_MULTIPLY:   BINARY_OP(*); break;
            case OP_DIVIDE:     BINARY_OP(/); break;
            case OP_NEGATE:
            {
                vm.stack[vm.stackCount - 1] *= -1;
                break;
            }
            case OP_RETURN:
            {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_TRIBYTE
    #undef READ_CONSTANT
    #undef READ_CONSTLONG
    #undef BINARY_OP
}

InterpretResult interpret(Chunk* chunk)
{
    vm.chunk = chunk;
    vm.ip = vm.chunk->code;
    return run();
}