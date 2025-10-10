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
    vm.frameCount = 0;
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
    fprintf(stderr, "Runtime Error: ");
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--)
    {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->function;
        // -1 to point to the previous failed instruction.
        size_t offset = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", getLine(&function->chunk, offset));
        if (function->name == NULL)
            fprintf(stderr, "script\n");
        else
            fprintf(stderr, "%s()\n", function->name->chars);
    }

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

    vm.stack[vm.stackCount++] = value;
}

Value pop()
{
    vm.stackCount--;
    return vm.stack[vm.stackCount];
}

static Value peek(int distance)
{
    return vm.stack[vm.stackCount - distance - 1];
}

static bool call(ObjFunction* function, int argCount)
{
    if (argCount != function->arity)
    {
        if (function->arity != 1)
            runtimeError("Expected %d arguments but got %d.",
                function->arity, argCount);
        else
            runtimeError("Expected 1 argument but got %d.",
                argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX)
    {
        runtimeError("Stack overflow.");
        return false;
    }
    
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->function = function;
    frame->ip = function->chunk.code;
    frame->slots = vm.stack + vm.stackCount - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
            case OBJ_FUNCTION:
                return call(AS_FUNCTION(callee), argCount);
            case OBJ_NATIVE:
            {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stack + vm.stackCount - argCount);
                vm.stackCount -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break; // Non-callable object type.
        }
    }

    runtimeError("Can only call functions and classes.");
    return false;
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
    // Top-most call-frame.
    // Using local variable to be concise and
    // encourage compiler to store frame
    // in a register, accessing IP faster.
    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    
    #define READ_BYTE() (*frame->ip++) // Dereference then increment.
    #define READ_SHORT() \
        (frame->ip += 2, \
            (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
    #define READ_TRIBYTE() \
        (frame->ip += 3, \
            (uint32_t)((frame->ip[-3] << 16) | \
                        (frame->ip[-2] << 8) | frame->ip[-1]))
    #define READ_CONSTANT() \
        (frame->function->chunk.constants.values[READ_BYTE()])
    #define READ_CONST_LONG() \
        (frame->function->chunk.constants.values[READ_TRIBYTE()])
    #define READ_OPERAND() (READ_BYTE() == OP_LONG ? READ_TRIBYTE() : READ_BYTE())
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define READ_STRING_LONG() AS_STRING(READ_CONST_LONG())
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
        printf("== debug trace == \n");
    #endif

    while (true)
    {
        #ifdef DEBUG_TRACE_EXECUTION
            printf("          ");
            // Our stack is empty before the first instruction executes.
            // So this only starts printing after (at least) the first
            // instruction is disassembled.
            for (Value* slot = vm.stack; slot - vm.stack < vm.stackCount; slot++)
            {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            // Disassemble the next instruction we will execute
            // prior to execution.
            disassembleInstruction(&frame->function->chunk, 
                                    (int) (frame->ip - frame->function->chunk.code));
        #endif
        
        uint8_t instruction;
        switch (instruction = READ_BYTE())
        {
            // Handles OP_ZERO and OP_COMPZERO.
            case OP_ZERO:
            {
                if (*frame->ip == OP_COMPZER0)
                {
                    Value value = pop();
                    (void) READ_BYTE(); // Skip the COMPZERO opcode.
                    push(BOOL_VAL(valuesEqual(value, NUMBER_VAL(0))));
                }
                else
                    push(NUMBER_VAL((double) 0));
                break;
            }
            case OP_ONE:        push(NUMBER_VAL((double) 1)); break;
            case OP_TWO:        push(NUMBER_VAL((double) 2)); break;
            case OP_MINUSONE:   push(NUMBER_VAL((double) -1)); break;
            case OP_CONSTANT:
            {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_CONSTANT_LONG:
            {
                Value constant = READ_CONST_LONG();
                push(constant);
                break;
            }
            case OP_DUP:    push(peek(0)); break;
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
                // Access stack slot relative to frame
                // beginning.
                push(frame->slots[READ_OPERAND()]);
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

                vm.globalValues.values[index] = peek(0);
                break;
            }
            case OP_SET_LOCAL:
            {
                frame->slots[READ_OPERAND()] = peek(0);
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
            case OP_DIVIDE:
            {
                if (IS_NUMBER(peek(0)) && AS_NUMBER(peek(0)) == 0)
                {
                    runtimeError("Cannot divide by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                BINARY_OP(NUMBER_VAL, / );
                break;
            }
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
            case OP_JUMP: frame->ip += READ_SHORT(); break;
            case OP_JUMP_IF_FALSE:
            {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: frame->ip -= READ_SHORT(); break;
            case OP_CALL:
            {
                uint8_t argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_RETURN:
            {
                Value result = pop();
                vm.frameCount--;
                if (vm.frameCount == 0)
                {
                    // Pop script function.
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackCount = (int) (frame->slots - vm.stack);
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_TRIBYTE
    #undef READ_CONSTANT
    #undef READ_CONST_LONG
    #undef READ_OPERAND
    #undef READ_STRING
    #undef READ_STRING_LONG
    #undef BINARY_OP
}

// Interpret pipeline driver.
InterpretResult interpret(const char* source)
{
    // Compile returns compiled top-level code.
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    // Stack will hold at least one function
    // object.
    // Goes in the dedicated slot 0.
    push(OBJ_VAL(function));
    call(function, 0);

    return run();
}