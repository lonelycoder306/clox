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
    vm.stackCount = 0;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

void initVM()
{
    vm.stack = NULL;
    vm.stackCapacity = 0;
    resetStack();

    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.strings);
    initTable(&vm.globalNames);
    initValueArray(&vm.globalValues);

    // Null the field first in case
    // GC is triggered before it is
    // properly copied.
    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    initTable(&vm.globalAccess);
    initTable(&vm.localAccess);
}

void freeVM()
{
    freeTable(&vm.globalNames);
    freeValueArray(&vm.globalValues);
    freeTable(&vm.strings);
    vm.initString = NULL;

    freeTable(&vm.globalAccess);
    freeTable(&vm.localAccess);

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
        ObjFunction* function = frame->closure->function;
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

static bool call(ObjClosure* closure, int argCount)
{
    if (argCount != closure->function->arity)
    {
        if (closure->function->arity != 1)
            runtimeError("Expected %d arguments but got %d.",
                closure->function->arity, argCount);
        else
            runtimeError("Expected 1 argument but got %d.", argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX)
    {
        runtimeError("Stack overflow.");
        return false;
    }
    
    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stack + vm.stackCount - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount)
{
    if (IS_OBJ(callee))
    {
        switch (OBJ_TYPE(callee))
        {
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE:
            {
                ObjNative* func = (ObjNative *) AS_OBJ(callee);
                
                if (argCount != func->arity)
                {
                    if (func->arity != 1)
                        runtimeError("Expected %d arguments but got %d.",
                            func->arity, argCount);
                    else
                        runtimeError("Expected 1 argument but got %d.",
                            argCount);
                    return false;
                }

                NativeFn native = AS_NATIVE(callee);
                if (!native(argCount, vm.stack + vm.stackCount - argCount))
                {
                    runtimeError(AS_CSTRING(vm.stack[vm.stackCount - argCount - 1]));
                    return false;
                };
                vm.stackCount -= argCount;
                return true;
            }
            case OBJ_CLASS:
            {
                ObjClass* klass = AS_CLASS(callee);
                vm.stack[vm.stackCount - argCount - 1] = 
                                    OBJ_VAL(newInstance(klass));
                if (klass->init != NULL)
                    return call(klass->init, argCount);
                else if (argCount != 0)
                {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case OBJ_BOUND_METHOD:
            {
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm.stack[vm.stackCount - argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            default:
                break; // Non-callable object type.
        }
    }

    runtimeError("Can only call functions and classes.");
    return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount)
{
    Value method;
    if (!tableGet(&klass->methods, OBJ_VAL(name), &method))
    {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }
    
    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount)
{
    Value receiver = peek(argCount);

    if (!IS_INSTANCE(receiver))
    {
        runtimeError("Only instances have methods.");
        return false;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);

    Value value;
    if (tableGet(&instance->fields, OBJ_VAL(name), &value))
    {
        // If the object is a field (function) on the 
        // instance, we instead load the field on the
        // stack *below* the arguments and call it.
        vm.stack[vm.stackCount - argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

static ObjUpvalue* captureUpvalue(Value* local)
{
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while ((upvalue != NULL) && (upvalue->location > local))
    {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if ((upvalue != NULL) && (upvalue->location = local))
        return upvalue;
    
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    // upvalue will be to the "left" of (i.e., after) 
    // our new upvalue.
    // prevUpvalue will be to the "right" (i.e., before).
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL)
        vm.openUpvalues = createdUpvalue;
    else
        prevUpvalue->next = createdUpvalue;

    return createdUpvalue;
}

static void closeUpvalues(Value* last)
{
    while ((vm.openUpvalues != NULL) && 
            (vm.openUpvalues->location >= last))
    {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString* name)
{
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    if (name == vm.initString)
        klass->init = AS_CLOSURE(method);
    else
        tableSet(&klass->methods, OBJ_VAL(name), method);
    pop();
}

static bool bindMethod(ObjClass* klass, ObjString* name)
{
    Value method;
    if (!tableGet(&klass->methods, OBJ_VAL(name), &method))
        // Error reporting will occur back in run().
        return false;
    
    // We don't pop() the instance directly in case
    // GC runs before we add the instance as a field on the
    // ObjBoundMethod object (since newBoundMethod
    // involves allocation first).
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static bool isFalsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate()
{
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    ObjString* result = makeString(length);
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
    result->hash = hash;

    pop();
    pop();
    push(OBJ_VAL(result));
}

static InterpretResult run()
{
    // Top-most call-frame.
    // Using local variable to be concise and
    // encourage compiler to store frame
    // in a register, accessing IP faster.
    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    register uint8_t* ip = frame->ip;
    
    #define READ_BYTE() (*ip++) // Dereference then increment.
    #define READ_SHORT() \
        (ip += 2, \
            (uint16_t)((ip[-2] << 8) | ip[-1]))
    #define READ_TRIBYTE() \
        (ip += 3, \
            (uint32_t)((ip[-3] << 16) | (ip[-2] << 8) | ip[-1]))
    #define READ_OPERAND() (READ_BYTE() == OP_LONG ? READ_TRIBYTE() : READ_BYTE())

    #define READ_CONSTANT() \
        (frame->closure->function->chunk.constants.values[READ_BYTE()])
    #define READ_CONST_LONG() \
        (frame->closure->function->chunk.constants.values[READ_TRIBYTE()])
    #define READ_VALUE() \
        (READ_BYTE() == OP_CONSTANT ? READ_CONSTANT() : READ_CONST_LONG())
    #define READ_CONST_OPER() (READ_BYTE() == OP_LONG ? READ_CONSTANT() : READ_CONST_LONG())

    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define READ_STRING_LONG() AS_STRING(READ_CONST_LONG())
    #define READ_STRING_OPER() AS_STRING(READ_CONST_OPER())
    #define READ_STRING_VALUE() AS_STRING(READ_VALUE())

    #define BINARY_OP(valueType, op) \
            do \
            { \
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
                { \
                    frame->ip = ip; \
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
            #ifdef DEBUG_TRACE_STACK
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
            #endif
            // Disassemble the next instruction we will execute
            // prior to execution.
            disassembleInstruction(&frame->closure->function->chunk, 
                                    (int) (ip - frame->closure->function->chunk.code));
        #endif
        
        uint8_t instruction = READ_BYTE();
        switch (instruction)
        {
            // Handles OP_ZERO and OP_COMPZERO.
            case OP_ZERO:
            {
                if (*ip == OP_COMPZER0)
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
                    // When we report a runtime error, 
                    // it has to appear at the right instruction.
                    frame->ip = ip;
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
            case OP_GET_UPVALUE:
            {
                READ_BYTE(); // Get rid of OP_SHORT.
                push(*frame->closure->upvalues[READ_BYTE()]->location);
                break;
            }
            case OP_SET_GLOBAL:
            {
                int index = READ_OPERAND();
                if (IS_UNDEFINED(vm.globalValues.values[index]))
                {
                    frame->ip = ip;
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
            case OP_SET_UPVALUE:
            {
                READ_BYTE(); // Get rid of OP_SHORT.
                *frame->closure->upvalues[READ_BYTE()]->location = peek(0);
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
                    frame->ip = ip;
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
                    frame->ip = ip;
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
                    frame->ip = ip;
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
                    frame->ip = ip;
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
                    frame->ip = ip;
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
            case OP_JUMP:
            {
                uint16_t jump = READ_SHORT();
                ip += jump;
                break;
            }
            case OP_JUMP_IF_FALSE:
            {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) ip += offset;
                break;
            }
            case OP_LOOP:
            {
                uint16_t loop = READ_SHORT();
                ip -= loop;
                break;
            }
            case OP_CALL:
            {
                uint8_t argCount = READ_BYTE();
                frame->ip = ip;
                if (!callValue(peek(argCount), argCount))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                ip = frame->ip;
                break;
            }
            case OP_INVOKE:
            {
                ObjString* method = READ_STRING_VALUE();
                int argCount = READ_BYTE();
                frame->ip = ip;
                if (!invoke(method, argCount))
                    return INTERPRET_RUNTIME_ERROR;
                frame = &vm.frames[vm.frameCount - 1];
                ip = frame->ip;
                break;
            }
            case OP_CLOSURE:
            {
                ObjFunction* function = AS_FUNCTION(READ_VALUE());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));

                for (int i = 0; i < closure->upvalueCount; i++)
                {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal)
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    else
                        closure->upvalues[i] = frame->closure->upvalues[index];
                }

                break;
            }
            case OP_CLOSE_UPVALUE:
            {
                // Close the upvalue at top of stack.
                closeUpvalues(vm.stack + vm.stackCount - 1);
                // Pop that stack slot.
                pop();
                break;
            }
            case OP_CLASS:
            {
                push(OBJ_VAL(newClass(READ_STRING_VALUE())));
                break;
            }
            case OP_METHOD:
            {
                defineMethod(READ_STRING_VALUE());
                break;
            }
            case OP_GET_PROPERTY:
            {
                if (!IS_INSTANCE(peek(0)))
                {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING_VALUE();

                Value value;
                // Check for field.
                if (tableGet(&instance->fields, OBJ_VAL(name), &value))
                {
                    pop(); // Instance;
                    push(value);
                    break;
                }

                // Check for method.
                if (!bindMethod(instance->klass, name))
                {
                    runtimeError("Undefined property '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                break;
            }
            case OP_SET_PROPERTY:
            {
                if (!IS_INSTANCE(peek(1)))
                {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, OBJ_VAL(READ_STRING_VALUE()), peek(0));
                Value value = pop(); // Pop stored value.
                pop(); // Pop instance.
                push(value); // Push stored value back on top.
                break;
            }
            case OP_DEL_PROPERTY:
            {
                if (!IS_INSTANCE(peek(0)))
                {
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING_VALUE();

                if (!tableDelete(&instance->fields, OBJ_VAL(name)))
                {
                    runtimeError("Failed to delete field '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                break;
            }
            case OP_RETURN:
            {
                Value result = pop();
                closeUpvalues(frame->slots);
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
                ip = frame->ip;
                break;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_SHORT
    #undef READ_TRIBYTE
    #undef READ_OPERAND

    #undef READ_CONSTANT
    #undef READ_CONST_LONG
    #undef READ_VALUE
    #undef READ_CONST_OPER

    #undef READ_STRING
    #undef READ_STRING_LONG
    #undef READ_STRING_OPER
    #undef READ_STRING_VALUE

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
    ObjClosure* closure = newClosure(function);
    // Push then pop in case GC triggered.
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}