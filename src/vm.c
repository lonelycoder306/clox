#include "../include/vm.h"
#include "../include/common.h"
#include "../include/compiler.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/table.h"
#include "../include/value.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
    vm.objects = NULL;
    initTable(&vm.strings);
    initTable(&vm.globalNames);
    initValueArray(&vm.globalValues);
}

void freeVM()
{
    freeTable(&vm.globalNames);
    freeValueArray(&vm.globalValues);
    freeTable(&vm.strings);
    freeObjects();
    FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
}

static void runtimeError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t offset = vm.ip - vm.chunk->code - 1;
    int line = getLine(vm.chunk, offset);
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void push(Value value)
{
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

static Value peek(int distance)
{
    return vm.stack[vm.stackCount - 1 - distance];
}

static bool isFalsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate()
{
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    ObjString* result = makeString(length + 1);
    memcpy(result->chars, a->chars, a->length);
    memcpy(result->chars + a->length, b->chars, b->length);

    uint32_t hash = hashString(result->chars, result->length);
    ObjString* interned = tableFindString(&vm.strings, result->chars,
                                            result->length, hash);
    
    if (interned != NULL)
    {
        reallocate(result, sizeof(ObjString) + result->length + 1, 0);
        push(OBJ_VAL(interned));
        return;
    }

    result->chars[length] = '\0';
    result->hash = hashString(result->chars, result->length);

    push(OBJ_VAL(result));
}

static InterpretResult run()
{
    #define READ_BYTE() (*vm.ip++) // Dereference then increment.
    #define READ_TRIBYTE() ((READ_BYTE() << 16) | \
                            (READ_BYTE() << 8) | \
                            READ_BYTE())
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
    #define READ_CONSTLONG() (vm.chunk->constants.values[READ_TRIBYTE()])
    #define READ_OPERAND() (READ_BYTE() == OP_LONG ? READ_TRIBYTE() : READ_BYTE())
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define READ_STRING_LONG() AS_STRING(READ_CONSTLONG())
    #define BINARY_OP(valueType, op) \
            do \
            { \
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
                { \
                    runtimeError("Operands must be numbers."); \
                    return INTERPRET_RUNTIME_ERROR; \
                } \
                double b = AS_NUMBER(pop()); \
                double a = AS_NUMBER(pop()); \
                push(valueType(a op b)); \
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
            // Handles OP_ZERO and OP_COMPZERO.
            case OP_ZERO:
            {
                if (*vm.ip == OP_COMPZER0)
                {
                    Value value = pop();
                    (void) READ_BYTE(); // Skip the COMPZERO opcode.
                    push(BOOL_VAL(valuesEqual(value, NUMBER_VAL(0))));
                }
                else
                    push(NUMBER_VAL(0));
                break;
            }
            case OP_ONE:        push(NUMBER_VAL(1)); break;
            case OP_TWO:        push(NUMBER_VAL(2)); break;
            case OP_MINUSONE:   push(NUMBER_VAL(-1)); break;
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
            case OP_NIL:    push(NIL_VAL); break;
            case OP_TRUE:   push(BOOL_VAL(true)); break;
            case OP_FALSE:  push(BOOL_VAL(false)); break;
            case OP_POP:    pop(); break;
            case OP_POPN:
            {
                vm.stackCount -= READ_OPERAND();
                break;
                // No return since this is only for local variables.
                // We don't return the last variable popped.
            }
            case OP_DEFINE_GLOBAL:
            {
                vm.globalValues.values[READ_OPERAND()] = pop();
                break;
            }
            case OP_GET_GLOBAL:
            {
                Value value = vm.globalValues.values[READ_OPERAND()];
                if (IS_UNDEFINED(value))
                {
                    runtimeError("Undefined variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_GET_LOCAL:
            {
                push(vm.stack[READ_OPERAND()]);
                break;
            }
            case OP_SET_GLOBAL:
            {
                int index = READ_OPERAND();
                if (IS_UNDEFINED(vm.globalValues.values[index]))
                {
                    runtimeError("Undefined variable.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                Value value;
                tableGet(&vm.globalAccess, NUMBER_VAL((double) index), &value);
                if ((int) AS_NUMBER(value) == ACCESS_FIX)
                {
                    runtimeError("Fixed variable cannot be reassigned.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.globalValues.values[index] = peek(0);
                break;
            }
            case OP_SET_LOCAL:
            {
                int index = READ_OPERAND();
                Value value;
                tableGet(&vm.localAccess, NUMBER_VAL((double) index), &value);
                if ((int) AS_NUMBER(value) == ACCESS_FIX)
                {
                    runtimeError("Fixed variable cannot be reassigned.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                vm.stack[index] = peek(0);
                break;
            }
            case OP_EQUAL:
            {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:    BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:       BINARY_OP(BOOL_VAL, <); break;
            case OP_INCREMENT:
            {
                if (!IS_NUMBER(peek(0)))
                {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.stack[vm.stackCount - 1].as.number++;
                break;
            }
            case OP_DECREMENT:
            {
                if (!IS_NUMBER(peek(0)))
                {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.stack[vm.stackCount - 1].as.number--;
                break;
            }
            case OP_ADD:
            {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
                    concatenate();
                else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(0)))
                {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                }
                else
                {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT:   BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY:   BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:     BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
            {
                push(BOOL_VAL(isFalsey(pop())));
                break;
            }
            case OP_NEGATE:
            {
                if (!IS_NUMBER(peek(0)))
                {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.stack[vm.stackCount - 1].as.number *= -1;
                break;
            }
            case OP_PRINT:
            {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_RETURN:
                // Exit the interpreter.
                return INTERPRET_OK;
        }
    }

    #undef READ_BYTE
    #undef READ_TRIBYTE
    #undef READ_CONSTANT
    #undef READ_CONSTLONG
    #undef READ_OPERAND
    #undef READ_STRING
    #undef READ_STRING_LONG
    #undef BINARY_OP
}

// Interpret pipeline driver.
InterpretResult interpret(const char* source)
{
    Chunk chunk;
    initChunk(&chunk);

    // Call compiler to scan source code
    // and construct byte-code chunk.
    if(!compile(source, &chunk))
    {
        // Discard unusable chunk if 
        // compile error occurred.
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    };

    // Prepare VM with constructed chunk.
    vm.chunk = &chunk;
    // Instruction pointer starts at beginning
    // of chunk byte-code.
    vm.ip = vm.chunk->code;

    // Run the VM.
    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}