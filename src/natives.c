#include "../include/natives.h"
#include "../include/value.h"
#include "../include/vm.h"
#include <math.h>
#include <string.h>
#include <time.h>

static ObjNative natives[4];
static const int nativesCount = 4;

static bool clockNative(int argCount, Value* args);
static bool sqrtNative(int argCount, Value* args);
static bool typeNative(int argCount, Value* args);
static bool lengthNative(int argCount, Value* args);

// Directly initializing each struct with {...}
// didn't work for some reason.
static void fillNatives()
{
    natives[0].obj.type = OBJ_NATIVE;
    natives[0].name = "clock";
    natives[0].function = clockNative;
    natives[0].arity = 0;

    natives[1].obj.type = OBJ_NATIVE;
    natives[1].name = "sqrt";
    natives[1].function = sqrtNative;
    natives[1].arity = 1;

    natives[2].obj.type = OBJ_NATIVE;
    natives[2].name = "type";
    natives[2].function = typeNative;
    natives[2].arity = 1;

    natives[3].obj.type = OBJ_NATIVE;
    natives[3].name = "length";
    natives[3].function = lengthNative;
    natives[3].arity = 1;
}

static void defineNative(ObjNative* nativeFunc)
{
    int index = vm.globalValues.count;
    ObjString* identifier = copyString(nativeFunc->name, 
                                    (int) strlen(nativeFunc->name));
    writeValueArray(&vm.globalValues, OBJ_VAL(nativeFunc));
    tableSet(&vm.globalNames, OBJ_VAL(identifier), NUMBER_VAL((double)index));
}

void defineNatives()
{
    fillNatives();
    
    for (int i = 0; i < nativesCount; i++)
        defineNative(&natives[i]);
}

static bool clockNative(int argCount, Value* args)
{    
    // Replace function in stack once computation is done.
    args[-1] = NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
    return true;
}

static bool sqrtNative(int argCount, Value* args)
{
    if (!IS_NUMBER(args[0]))
    {
        ObjString* message = copyString("Invalid input to sqrt().", 24);
        args[-1] = OBJ_VAL(message);
        return false;
    }
    
    args[-1] =  NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
    return true;
}

static bool typeNative(int argCount, Value* args)
{
    ObjString* typeName = NULL; // Dummy initial value.
    
    switch (args[0].type)
    {
        case VAL_BOOL:
            typeName = copyString("<boolean>", 9);
            break;
        case VAL_NIL:
            typeName = copyString("<nil>", 5);
            break;
        case VAL_NUMBER:
            typeName = copyString("<number>", 8);
            break;
        case VAL_OBJ:
        {
            Obj* object = AS_OBJ(args[0]);

            switch (object->type)
            {
                case OBJ_STRING:
                    typeName = copyString("<string>", 8);
                    break;
                case OBJ_FUNCTION:
                    typeName = copyString("<function>", 10);
                    break;
                case OBJ_NATIVE:
                    typeName = copyString("<builtin function>", 18);
                    break;
                case OBJ_UPVALUE:
                    // Temporarily.
                    // We should check the upvalue's specific type.
                    typeName = copyString("<upvalue>", 9);
                    break;
                case OBJ_CLOSURE:
                    typeName = copyString("<closure>", 9);
                    break;
                case OBJ_CLASS:
                    typeName = copyString("<class>", 7);
                    break;
                case OBJ_INSTANCE:
                    typeName = copyString("<class instance>", 16);
            }
            break;
        }
        default:
            typeName = copyString("<unknown type>", 14);
    }

    args[-1] = OBJ_VAL(typeName);
    return true;
}

static bool lengthNative(int argCount, Value* args)
{
    if (!IS_STRING(args[0]))
    {
        ObjString* message = copyString("Invalid input to length().", 26);
        args[-1] = OBJ_VAL(message);
        return false;
    }

    args[-1] = NUMBER_VAL((double) strlen(AS_CSTRING(args[0])));
    return true;
}